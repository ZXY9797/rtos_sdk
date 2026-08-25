#include "link/router.h"

#include <init.h>
#include <osal.h>

#include <cstring>

#ifndef CONFIG_LINK_PROCESS_RX_BUDGET
#define CONFIG_LINK_PROCESS_RX_BUDGET 1024
#endif
#ifndef CONFIG_LINK_INIT_PRIORITY
#define CONFIG_LINK_INIT_PRIORITY 95
#endif

namespace link {
namespace {

constexpr uint8_t kProgressStatus = 0x01U;
constexpr uint8_t kHandlerOkStatus = 0x00U;
constexpr uint8_t kReplayConflictStatus = 0xFDU;
constexpr uint8_t kAuthenticationRequiredStatus = 0xFEU;
constexpr uint8_t kHandlerNotFoundStatus = 0xFFU;
constexpr size_t kCommandTypeOffset = 4U;
constexpr size_t kReceiverOffset = 6U;
constexpr size_t kCommandSetOffset = 9U;
constexpr size_t kCommandIdOffset = 10U;

bool retry_policy(AckMode mode, uint32_t& timeout_ms, uint8_t& max_retry)
{
    switch (mode) {
    case AckMode::Now:
        timeout_ms = 10U;
        max_retry = 3U;
        return true;
    case AckMode::Finish:
        timeout_ms = 500U;
        max_retry = 2U;
        return true;
    case AckMode::Progress:
        timeout_ms = 2000U;
        max_retry = 1U;
        return true;
    default:
        return false;
    }
}

} // namespace

extern "C" {
extern const Handler __link_handler_start;
extern const Handler __link_handler_end;
}

Router& Router::instance()
{
    static Router router;
    return router;
}

Router::OperationGuard::OperationGuard(Router& router)
    : router_(router), active_(router_.begin_operation())
{
}

Router::OperationGuard::~OperationGuard()
{
    if (active_) {
        router_.end_operation();
    }
}

bool Router::begin_operation()
{
    if (!initialized_.load(std::memory_order_acquire)) {
        return false;
    }
    active_operations_.fetch_add(1U, std::memory_order_acq_rel);
    if (!initialized_.load(std::memory_order_acquire)) {
        active_operations_.fetch_sub(1U, std::memory_order_release);
        return false;
    }
    return true;
}

void Router::end_operation()
{
    active_operations_.fetch_sub(1U, std::memory_order_release);
}

bool Router::init()
{
    osal::Deadline deadline(100U);
    osal::LockGuard lifecycle_lock(lifecycle_mutex_, deadline.remaining());
    if (!lifecycle_lock.owns_lock()) {
        return false;
    }
    initialized_.store(false, std::memory_order_release);
    if (!wait_for_quiescence(deadline)) {
        return false;
    }
    clear_state();
    if (!route_mutex_.is_valid() || !process_mutex_.is_valid()
        || !pending_mutex_.is_valid() || !send_mutex_.is_valid()
        || !callback_mutex_.is_valid() || !security_mutex_.is_valid()) {
        return false;
    }
#if defined(CONFIG_LINK_SECURITY)
    if (security_provider_ == nullptr) {
        security_provider_ = product_security_provider();
    }
    if (security_provider_ == nullptr) {
        return false;
    }
#endif
    link_cnt_ = (g_link_registry.cnt <= CONFIG_LINK_MAX_LINKS)
        ? g_link_registry.cnt : CONFIG_LINK_MAX_LINKS;
    for (size_t i = 0U; i < link_cnt_; ++i) {
        links_[i] = g_link_registry.links[i];
    }
    handlers_ = &__link_handler_start;
    handler_cnt_ = static_cast<size_t>(
        &__link_handler_end - &__link_handler_start);
    initialized_.store(true, std::memory_order_release);
    return true;
}

bool Router::deinit(osal::Milliseconds timeout_ms)
{
    osal::Deadline deadline(timeout_ms);
    osal::LockGuard lifecycle_lock(lifecycle_mutex_, deadline.remaining());
    if (!lifecycle_lock.owns_lock()) {
        return false;
    }
    initialized_.store(false, std::memory_order_release);
    if (!wait_for_quiescence(deadline)) {
        return false;
    }
    clear_state();
    return true;
}

bool Router::wait_for_quiescence(osal::Deadline& deadline)
{
    while (active_operations_.load(std::memory_order_acquire) != 0U) {
        if (deadline.expired()) {
            return false;
        }
        if (!osal::Kernel::is_running()) {
            return false;
        }
        osal::this_thread::sleep_for(1U);
    }
    return true;
}

void Router::clear_state()
{
    for (size_t i = 0U; i < CONFIG_LINK_MAX_LINKS; ++i) {
        links_[i] = nullptr;
        codecs_[i].reset();
    }
    for (RouteEntry& route : routes_) {
        route = {};
    }
    for (TxPending& pending : pending_) {
        pending = {};
    }
    replay_cache_.clear();
    link_cnt_ = 0U;
    route_cnt_ = 0U;
    rr_idx_ = 0U;
    self_addr_.store(0U, std::memory_order_release);
    tx_seq_ = 0U;
    handlers_ = nullptr;
    handler_cnt_ = 0U;
    on_timeout_ = nullptr;
    on_timeout_arg_ = nullptr;
    received_frames_.store(0U, std::memory_order_relaxed);
    codec_errors_.store(0U, std::memory_order_relaxed);
    forwarded_frames_.store(0U, std::memory_order_relaxed);
    transmitted_frames_.store(0U, std::memory_order_relaxed);
    security_drops_.store(0U, std::memory_order_relaxed);
    duplicate_requests_.store(0U, std::memory_order_relaxed);
    replay_conflicts_.store(0U, std::memory_order_relaxed);
    ack_timeouts_.store(0U, std::memory_order_relaxed);
}

bool Router::set_security_provider(SecurityProvider* provider)
{
    osal::LockGuard lock(lifecycle_mutex_);
    if (!lock.owns_lock()
        || initialized_.load(std::memory_order_acquire)
        || active_operations_.load(std::memory_order_acquire) != 0U) {
        return false;
    }
    security_provider_ = provider;
    return true;
}

RouterStats Router::stats() const
{
    return {
        received_frames_.load(std::memory_order_relaxed),
        codec_errors_.load(std::memory_order_relaxed),
        forwarded_frames_.load(std::memory_order_relaxed),
        transmitted_frames_.load(std::memory_order_relaxed),
        security_drops_.load(std::memory_order_relaxed),
        duplicate_requests_.load(std::memory_order_relaxed),
        replay_conflicts_.load(std::memory_order_relaxed),
        ack_timeouts_.load(std::memory_order_relaxed),
    };
}

bool Router::set_routes(const RouteEntry* entries, size_t count)
{
    OperationGuard operation(*this);
    if (!operation) {
        return false;
    }
    if ((count > 0U && entries == nullptr) || count > CONFIG_LINK_MAX_ROUTES) {
        return false;
    }
    for (size_t i = 0U; i < count; ++i) {
        if ((entries[i].mode != RouteMode::ByHost
             && entries[i].mode != RouteMode::Direct)
            || entries[i].out_cnt == 0U || entries[i].out_cnt > 4U) {
            return false;
        }
        for (uint8_t output = 0U; output < entries[i].out_cnt; ++output) {
            const uint16_t link_id = entries[i].out_links[output];
            if (link_id == 0U
                || find_unique_link_unlocked(link_id) == nullptr) {
                return false;
            }
            for (uint8_t previous = 0U; previous < output; ++previous) {
                if (entries[i].out_links[previous] == link_id) {
                    return false;
                }
            }
        }
    }

    osal::LockGuard lock(route_mutex_);
    if (!lock.owns_lock()) {
        return false;
    }
    for (size_t i = 0U; i < count; ++i) {
        routes_[i] = entries[i];
    }
    route_cnt_ = count;
    rr_idx_ = 0U;
    return true;
}

Link* Router::find_unique_link_unlocked(uint16_t link_id)
{
    Link* match = nullptr;
    for (size_t i = 0U; i < link_cnt_; ++i) {
        if (links_[i] != nullptr && links_[i]->id() == link_id) {
            if (match != nullptr) {
                return nullptr;
            }
            match = links_[i];
        }
    }
    return match;
}

Link* Router::select_out_link(const RouteEntry& entry)
{
    if (entry.out_cnt == 0U || entry.out_cnt > 4U) {
        return nullptr;
    }
    for (uint8_t offset = 0U; offset < entry.out_cnt; ++offset) {
        const uint8_t index =
            static_cast<uint8_t>((rr_idx_ + offset) % entry.out_cnt);
        Link* const candidate =
            find_unique_link_unlocked(entry.out_links[index]);
        if (candidate != nullptr && candidate->is_ready()) {
            rr_idx_ = static_cast<uint8_t>((index + 1U) % entry.out_cnt);
            return candidate;
        }
    }
    return nullptr;
}

Link* Router::route_lookup(uint8_t receiver, uint16_t in_link_id)
{
    osal::LockGuard lock(route_mutex_);
    if (!lock.owns_lock()) {
        return nullptr;
    }
    for (size_t i = 0U; i < route_cnt_; ++i) {
        const RouteEntry& route = routes_[i];
        const bool matches = (route.mode == RouteMode::Direct)
            ? (route.match_addr == 0U || route.match_addr == in_link_id)
            : ((receiver & route.mask)
               == (static_cast<uint8_t>(route.match_addr) & route.mask));
        if (matches) {
            return select_out_link(route);
        }
    }
    return nullptr;
}

int Router::send_raw(Link* out, const uint8_t* data, size_t len)
{
    if (out == nullptr || data == nullptr || len == 0U || !out->is_ready()) {
        return -1;
    }
    osal::LockGuard lock(send_mutex_);
    if (!lock.owns_lock()) {
        return -1;
    }
    const int sent = out->send(data, len);
    if (sent == static_cast<int>(len)) {
        transmitted_frames_.fetch_add(1U, std::memory_order_relaxed);
    }
    return sent;
}

void Router::process()
{
    OperationGuard operation(*this);
    if (!operation) {
        return;
    }
    osal::LockGuard process_lock(process_mutex_);
    if (!process_lock.owns_lock()) {
        return;
    }
    uint8_t byte = 0U;
    for (size_t i = 0U; i < link_cnt_; ++i) {
        Link* const input = links_[i];
        if (input == nullptr || !input->is_ready()) {
            continue;
        }
        input->poll();
        size_t rx_budget = CONFIG_LINK_PROCESS_RX_BUDGET;
        while (rx_budget > 0U && input->recv(&byte, 1U) == 1) {
            --rx_budget;
            const int result = codecs_[i].feed(byte);
            if (result < 0) {
                codec_errors_.fetch_add(1U, std::memory_order_relaxed);
                continue;
            }
            if (result == 0) {
                continue;
            }
            const Frame& frame = codecs_[i].frame();
            received_frames_.fetch_add(1U, std::memory_order_relaxed);
            if (!is_unicast_addr(frame.sender_id)
                || frame.receiver_id == ADDR_RESERVED) {
                codec_errors_.fetch_add(1U, std::memory_order_relaxed);
                continue;
            }
            const uint8_t self_addr =
                self_addr_.load(std::memory_order_acquire);
            if (!is_unicast_addr(self_addr)
                && frame.receiver_id == ADDR_BROADCAST) {
                continue;
            }
            const bool for_me = frame.receiver_id
                                  == self_addr
                             || frame.receiver_id == ADDR_BROADCAST;
            if (!for_me) {
                Link* const output =
                    route_lookup(frame.receiver_id, input->id());
                if (output != nullptr) {
                    const int sent = send_raw(output, codecs_[i].raw(),
                                              codecs_[i].raw_len());
                    if (sent == static_cast<int>(codecs_[i].raw_len())) {
                        forwarded_frames_.fetch_add(
                            1U, std::memory_order_relaxed);
                    }
                }
            } else if (frame.is_ack()) {
                Frame plaintext {};
                if (unprotect_frame(frame, plaintext)) {
                    handle_ack(input, plaintext);
                } else {
                    security_drops_.fetch_add(
                        1U, std::memory_order_relaxed);
                }
            } else {
                const bool acknowledged = frame.receiver_id != ADDR_BROADCAST
                    && frame.ack_mode() != AckMode::No;
                if (acknowledged) {
                    const ReplayResult replay = replay_cache_.classify(
                        frame, get_tick_ms(),
                        CONFIG_LINK_REPLAY_RETENTION_MS);
                    if (replay.decision == ReplayDecision::Duplicate) {
                        duplicate_requests_.fetch_add(
                            1U, std::memory_order_relaxed);
                        send_ack(input, frame, replay.ack_status, nullptr, 0U);
                        continue;
                    }
                    if (replay.decision == ReplayDecision::Conflict) {
                        replay_conflicts_.fetch_add(
                            1U, std::memory_order_relaxed);
                        // A different encrypted frame with the same sender
                        // and sequence must not cause a second response under
                        // the original AEAD nonce. Plaintext deployments can
                        // still report the protocol conflict explicitly.
                        if (frame.enc_mode() == EncMode::None) {
                            send_ack(input, frame, kReplayConflictStatus,
                                     nullptr, 0U);
                        } else {
                            security_drops_.fetch_add(
                                1U, std::memory_order_relaxed);
                        }
                        continue;
                    }
                }

                Frame plaintext {};
                if (!unprotect_frame(frame, plaintext)) {
                    security_drops_.fetch_add(
                        1U, std::memory_order_relaxed);
                    continue;
                }
                const uint8_t status = dispatch(input, plaintext);
                if (acknowledged) {
                    replay_cache_.remember(frame, status, get_tick_ms());
                }
            }
        }
    }
    check_timeout();
}

uint8_t Router::dispatch(Link* src, const Frame& frame)
{
    bool handled = false;
    bool secure_required = false;
    for (size_t i = 0U; i < handler_cnt_; ++i) {
        const Handler& handler = handlers_[i];
        if (handler.cmd_set == frame.cmd_set &&
            handler.cmd_id == frame.cmd_id) {
            secure_required = (handler.flags & HANDLER_REQUIRE_SECURE) != 0U;
#if defined(CONFIG_LINK_REQUIRE_SECURE_COMMANDS)
            secure_required = true;
#endif
            if (handler.cb != nullptr
                && (!secure_required || frame.enc_mode() != EncMode::None)) {
                handler.cb(frame, handler.arg);
                handled = true;
            }
            break;
        }
    }
    const uint8_t status = secure_required
        && frame.enc_mode() == EncMode::None
        ? kAuthenticationRequiredStatus
        : (handled ? kHandlerOkStatus : kHandlerNotFoundStatus);
    if (frame.receiver_id != ADDR_BROADCAST
        && frame.ack_mode() != AckMode::No) {
        send_ack(src, frame, status, nullptr, 0U);
    }
    return status;
}

int Router::send(const SendRequest& request)
{
    OperationGuard operation(*this);
    if (!operation) {
        return -1;
    }
    if ((request.data_len > 0U && request.data == nullptr)
        || request.receiver == ADDR_RESERVED
        || static_cast<uint8_t>(request.ack_mode)
            > static_cast<uint8_t>(AckMode::Progress)
        || static_cast<uint8_t>(request.enc_mode)
            > static_cast<uint8_t>(EncMode::ChaCha20)
        || static_cast<uint8_t>(request.priority)
            > static_cast<uint8_t>(Priority::High)) {
        return -2;
    }
#if defined(CONFIG_LINK_REQUIRE_SECURE_COMMANDS)
    if (request.enc_mode == EncMode::None) {
        return -2;
    }
#endif
    Link* const output = route_lookup(request.receiver, 0U);
    if (output == nullptr) {
        return -1;
    }

    osal::LockGuard send_lock(send_mutex_);
    if (!send_lock.owns_lock()) {
        return -1;
    }
    PackArgs args {};
    args.sender = self_addr_.load(std::memory_order_acquire);
    if (!is_unicast_addr(args.sender)) {
        return -2;
    }
    args.receiver = request.receiver;
    args.cmd_set = request.cmd_set;
    args.cmd_id = request.cmd_id;
    args.ack_mode = request.ack_mode;
    args.enc_mode = request.enc_mode;
    args.priority = request.priority;
    if (!next_sequence(args.seq)) {
        return -3;
    }

    const SecurityContext security_context {
        args.sender,
        args.receiver,
        args.seq,
        args.cmd_set,
        args.cmd_id,
        args.ack_mode,
        args.enc_mode,
        args.priority,
        false,
    };
    const uint8_t* payload = request.data;
    size_t payload_size = request.data_len;
    if (!protect_payload(security_context, request.data, request.data_len,
                         payload, payload_size)) {
        return -2;
    }
    const size_t frame_len = FrameCodec::pack(tx_buffer_, sizeof(tx_buffer_),
                                              args, payload, payload_size);
    if (frame_len == 0U) {
        return -2;
    }

    int pending_index = -1;
    if (args.ack_mode != AckMode::No) {
        size_t link_index = link_cnt_;
        for (size_t i = 0U; i < link_cnt_; ++i) {
            if (links_[i] == output) {
                link_index = i;
                break;
            }
        }
        if (link_index >= link_cnt_ || link_index > UINT8_MAX) {
            return -1;
        }
        pending_index = add_pending(args.seq,
                                    request.ack_mode,
                                    static_cast<uint8_t>(link_index),
                                    tx_buffer_,
                                    static_cast<uint16_t>(frame_len));
        if (pending_index < 0) {
            return -3;
        }
    }
    const int sent = output->send(tx_buffer_, frame_len);
    if (sent != static_cast<int>(frame_len) && pending_index >= 0) {
        remove_pending(args.seq);
    } else if (sent == static_cast<int>(frame_len)) {
        transmitted_frames_.fetch_add(1U, std::memory_order_relaxed);
    }
    return sent;
}

bool Router::protect_payload(const SecurityContext& context,
                             const uint8_t* plaintext,
                             size_t plaintext_size,
                             const uint8_t*& payload,
                             size_t& payload_size)
{
    payload = plaintext;
    payload_size = plaintext_size;
    if (context.mode == EncMode::None) {
        return true;
    }
    if (security_provider_ == nullptr) {
        return false;
    }
    osal::LockGuard lock(security_mutex_);
    if (!lock.owns_lock()) {
        return false;
    }
    size_t protected_size = 0U;
    const SecurityStatus status = security_provider_->protect(
        context, plaintext, plaintext_size, security_tx_buffer_,
        sizeof(security_tx_buffer_), protected_size);
    if (status != SecurityStatus::Ok
        || protected_size > sizeof(security_tx_buffer_)) {
        return false;
    }
    payload = security_tx_buffer_;
    payload_size = protected_size;
    return true;
}

bool Router::unprotect_frame(const Frame& frame, Frame& plaintext)
{
    plaintext = frame;
    if (frame.enc_mode() == EncMode::None) {
        return true;
    }
    if (security_provider_ == nullptr) {
        return false;
    }
    osal::LockGuard lock(security_mutex_);
    if (!lock.owns_lock()) {
        return false;
    }
    const SecurityContext context {
        frame.sender_id,
        frame.receiver_id,
        frame.seq,
        frame.cmd_set,
        frame.cmd_id,
        frame.ack_mode(),
        frame.enc_mode(),
        frame.priority(),
        frame.is_ack(),
    };
    size_t plaintext_size = 0U;
    const SecurityStatus status = security_provider_->unprotect(
        context, frame.data, frame.data_len, security_rx_buffer_,
        sizeof(security_rx_buffer_), plaintext_size);
    if (status != SecurityStatus::Ok
        || plaintext_size > sizeof(security_rx_buffer_)
        || plaintext_size > UINT16_MAX) {
        return false;
    }
    plaintext.data = plaintext_size == 0U ? nullptr : security_rx_buffer_;
    plaintext.data_len = static_cast<uint16_t>(plaintext_size);
    return true;
}

void Router::handle_ack(Link* src, const Frame& frame)
{
    if (src == nullptr || frame.data == nullptr || frame.data_len == 0U
        || frame.receiver_id
            != self_addr_.load(std::memory_order_acquire)) {
        return;
    }
    const uint8_t status = frame.data[0];
    osal::LockGuard lock(pending_mutex_);
    if (!lock.owns_lock()) {
        return;
    }
    for (TxPending& pending : pending_) {
        if (!pending.used || pending.seq != frame.seq
            || pending.raw_len < HEADER_SIZE
            || pending.out_link_idx >= link_cnt_
            || links_[pending.out_link_idx] != src
            || pending.raw_frame[kReceiverOffset] != frame.sender_id
            || pending.raw_frame[kCommandSetOffset] != frame.cmd_set
            || pending.raw_frame[kCommandIdOffset] != frame.cmd_id) {
            continue;
        }
        if (status == kProgressStatus
            && pending.ack_mode == AckMode::Progress) {
            pending.send_tick = get_tick_ms();
            pending.retry_cnt = 0U;
        } else {
            pending.used = false;
        }
        return;
    }
}

void Router::send_ack(Link* out, const Frame& req, uint8_t status,
                      const uint8_t* data, size_t data_len)
{
    if (out == nullptr || !out->is_ready()
        || (data_len > 0U && data == nullptr)) {
        return;
    }
    osal::LockGuard send_lock(send_mutex_);
    if (!send_lock.owns_lock()) {
        return;
    }
    constexpr size_t payload_capacity =
        CONFIG_LINK_MAX_FRAME_SIZE - HEADER_SIZE - CRC_SIZE;
    const size_t copy_len = data_len < (payload_capacity - 1U)
        ? data_len : (payload_capacity - 1U);
    tx_buffer_[HEADER_SIZE] = status;
    if (copy_len > 0U) {
        std::memcpy(&tx_buffer_[HEADER_SIZE + 1U], data, copy_len);
    }

    PackArgs args {};
    args.sender = self_addr_.load(std::memory_order_acquire);
    args.receiver = req.sender_id;
    args.cmd_set = req.cmd_set;
    args.cmd_id = req.cmd_id;
    args.is_ack = true;
    args.enc_mode = req.enc_mode();
    args.seq = req.seq;
    const SecurityContext security_context {
        args.sender,
        args.receiver,
        args.seq,
        args.cmd_set,
        args.cmd_id,
        args.ack_mode,
        args.enc_mode,
        args.priority,
        true,
    };
    const uint8_t* payload = &tx_buffer_[HEADER_SIZE];
    size_t payload_size = copy_len + 1U;
    if (!protect_payload(security_context, payload, payload_size,
                         payload, payload_size)) {
        security_drops_.fetch_add(1U, std::memory_order_relaxed);
        return;
    }
    const size_t frame_len = FrameCodec::pack(tx_buffer_, sizeof(tx_buffer_),
                                              args, payload, payload_size);
    if (frame_len > 0U
        && out->send(tx_buffer_, frame_len) == static_cast<int>(frame_len)) {
        transmitted_frames_.fetch_add(1U, std::memory_order_relaxed);
    }
}

int Router::add_pending(uint16_t seq, AckMode ack_mode, uint8_t link_idx,
                        const uint8_t* raw, uint16_t raw_len)
{
    if (raw == nullptr || raw_len < MIN_FRAME_SIZE
        || raw_len > CONFIG_LINK_MAX_FRAME_SIZE || link_idx >= link_cnt_) {
        return -1;
    }
    osal::LockGuard lock(pending_mutex_);
    if (!lock.owns_lock()) {
        return -1;
    }
    for (const TxPending& pending : pending_) {
        if (pending.used && pending.seq == seq) {
            return -1;
        }
    }
    for (size_t i = 0U; i < CONFIG_LINK_MAX_PENDING; ++i) {
        TxPending& pending = pending_[i];
        if (!pending.used) {
            pending.seq = seq;
            pending.ack_mode = ack_mode;
            pending.send_tick = get_tick_ms();
            pending.retry_cnt = 0U;
            pending.out_link_idx = link_idx;
            pending.raw_len = raw_len;
            std::memcpy(pending.raw_frame, raw, raw_len);
            pending.used = true;
            return static_cast<int>(i);
        }
    }
    return -1;
}

bool Router::next_sequence(uint16_t& sequence)
{
    osal::LockGuard lock(pending_mutex_);
    if (!lock.owns_lock()) {
        return false;
    }
    constexpr uint32_t sequence_count =
        static_cast<uint32_t>(UINT16_MAX) + 1U;
    for (uint32_t attempt = 0U; attempt < sequence_count; ++attempt) {
        const uint16_t candidate = tx_seq_++;
        bool used = false;
        for (const TxPending& pending : pending_) {
            used = used || (pending.used && pending.seq == candidate);
        }
        if (!used) {
            sequence = candidate;
            return true;
        }
    }
    return false;
}

void Router::remove_pending(uint16_t seq)
{
    osal::LockGuard lock(pending_mutex_);
    if (!lock.owns_lock()) {
        return;
    }
    for (TxPending& pending : pending_) {
        if (pending.used && pending.seq == seq) {
            pending.used = false;
            return;
        }
    }
}

void Router::check_timeout()
{
    const uint32_t now = get_tick_ms();
    for (size_t i = 0U; i < CONFIG_LINK_MAX_PENDING; ++i) {
        bool notify_timeout = false;
        uint16_t seq = 0U;
        uint8_t receiver = 0U;
        uint8_t cmd_set = 0U;
        uint8_t cmd_id = 0U;

        {
            // Keep the same global lock order as send(): send -> pending.
            osal::LockGuard send_lock(send_mutex_);
            osal::LockGuard pending_lock(pending_mutex_);
            if (!send_lock.owns_lock() || !pending_lock.owns_lock()) {
                return;
            }
            TxPending& pending = pending_[i];
            if (!pending.used) {
                continue;
            }
            uint32_t timeout_ms = 0U;
            uint8_t max_retry = 0U;
            if (!retry_policy(pending.ack_mode,
                              timeout_ms, max_retry)) {
                pending.used = false;
                continue;
            }
            if ((now - pending.send_tick) < timeout_ms) {
                continue;
            }
            if (pending.retry_cnt >= max_retry) {
                if (pending.raw_len >= HEADER_SIZE) {
                    seq = pending.seq;
                    receiver = pending.raw_frame[kReceiverOffset];
                    cmd_set = pending.raw_frame[kCommandSetOffset];
                    cmd_id = pending.raw_frame[kCommandIdOffset];
                    notify_timeout = true;
                }
                pending.used = false;
                ack_timeouts_.fetch_add(1U, std::memory_order_relaxed);
            } else {
                pending.raw_frame[kCommandTypeOffset] |= static_cast<uint8_t>(
                    1U << CMD_TYPE_RETRANSMIT_SHIFT);
                const size_t crc_offset = pending.raw_len - CRC_SIZE;
                const uint16_t crc = crc16_ccitt(pending.raw_frame, crc_offset);
                pending.raw_frame[crc_offset] =
                    static_cast<uint8_t>(crc & 0xFFU);
                pending.raw_frame[crc_offset + 1U] =
                    static_cast<uint8_t>(crc >> 8U);
                Link* output = nullptr;
                if (pending.out_link_idx < link_cnt_) {
                    output = links_[pending.out_link_idx];
                }
                if (output != nullptr && output->is_ready()) {
                    if (output->send(pending.raw_frame, pending.raw_len)
                        == static_cast<int>(pending.raw_len)) {
                        transmitted_frames_.fetch_add(
                            1U, std::memory_order_relaxed);
                    }
                }
                pending.retry_cnt++;
                pending.send_tick = now;
            }
        }

        TimeoutCallback callback = nullptr;
        void* callback_arg = nullptr;
        {
            osal::LockGuard callback_lock(callback_mutex_);
            if (callback_lock.owns_lock()) {
                callback = on_timeout_;
                callback_arg = on_timeout_arg_;
            }
        }
        if (notify_timeout && callback != nullptr) {
            callback(seq, receiver, cmd_set, cmd_id, callback_arg);
        }
    }
}

uint32_t Router::get_tick_ms()
{
    return osal::Kernel::uptime_ms();
}

} // namespace link

namespace {

osal::PeriodicThread g_link_thread;
alignas(std::max_align_t)
    uint8_t g_link_stack[CONFIG_LINK_PROCESS_STACK] {};
bool g_link_thread_created = false;

void link_process_entry(void*, const osal::PeriodicStats&)
{
    link::Router::instance().process();
}

int link_sys_init()
{
    if (g_link_thread_created) {
        return 0;
    }
    if (!link::Router::instance().init()) {
        return -1;
    }
    osal::PeriodicThreadConfig config {};
    config.name = "link";
    config.entry = link_process_entry;
    config.stack_buffer = g_link_stack;
    config.stack_size_bytes = CONFIG_LINK_PROCESS_STACK;
    config.priority =
        static_cast<osal::Priority>(CONFIG_LINK_PROCESS_PRIO);
    config.frequency_hz = CONFIG_LINK_PROCESS_HZ;
    if (!g_link_thread.start(config) || g_link_thread.startup() != 0) {
        g_link_thread.destroy();
        (void)link::Router::instance().deinit();
        return -1;
    }
    g_link_thread_created = true;
    return 0;
}

int link_sys_deinit()
{
    if (g_link_thread_created) {
        g_link_thread.destroy();
        g_link_thread_created = false;
    }
    return link::Router::instance().deinit() ? 0 : -1;
}

} // namespace

SYS_INIT_ROLLBACK(link_sys_init, link_sys_deinit,
                  INITCALL_LEVEL_APPLICATION, CONFIG_LINK_INIT_PRIORITY);
