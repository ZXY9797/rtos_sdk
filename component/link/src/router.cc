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
#ifndef CONFIG_LINK_WORKER_STACK
#define CONFIG_LINK_WORKER_STACK 3072
#endif
#ifndef CONFIG_LINK_WORKER_PRIO
#define CONFIG_LINK_WORKER_PRIO 4
#endif

namespace link {
namespace {

constexpr size_t kCommandTypeOffset = 4U;
constexpr size_t kSenderOffset = 5U;
constexpr size_t kReceiverOffset = 6U;
constexpr size_t kCommandSetOffset = 9U;
constexpr size_t kCommandIdOffset = 10U;

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
        || !work_mutex_.is_valid() || !address_mutex_.is_valid()
        || !pending_mutex_.is_valid() || !send_mutex_.is_valid()
        || !callback_mutex_.is_valid() || !security_mutex_.is_valid()
        || !replay_mutex_.is_valid() || !async_mutex_.is_valid()
        || !work_available_.is_valid()) {
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
    if (!validate_handlers()) {
        clear_state();
        return false;
    }
    if (!create_work_queues()) {
        clear_state();
        return false;
    }
    uint32_t epoch = router_epoch_.fetch_add(
        1U, std::memory_order_acq_rel) + 1U;
    if (epoch == 0U) {
        router_epoch_.store(1U, std::memory_order_release);
    }
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
    high_work_queue_.destroy();
    low_work_queue_.destroy();
    while (work_available_.take(0U) == 0) {
    }
    for (size_t i = 0U; i < CONFIG_LINK_MAX_LINKS; ++i) {
        links_[i] = nullptr;
        codecs_[i].reset();
        link_received_bytes_[i].store(0U, std::memory_order_relaxed);
        link_received_frames_[i].store(0U, std::memory_order_relaxed);
        link_codec_errors_[i].store(0U, std::memory_order_relaxed);
        link_transmitted_frames_[i].store(0U, std::memory_order_relaxed);
        link_transmit_failures_[i].store(0U, std::memory_order_relaxed);
        link_ready_[i].store(false, std::memory_order_relaxed);
        link_process_tick_[i] = 0U;
    }
    for (size_t index = 0U; index < CONFIG_LINK_MAX_ROUTES; ++index) {
        routes_[index] = {};
        route_rr_[index] = 0U;
    }
    for (TxPending& pending : pending_) {
        pending = {};
    }
    for (AsyncRequest& request : async_requests_) {
        request = {};
    }
    replay_cache_.clear();
    link_cnt_ = 0U;
    route_cnt_ = 0U;
    self_addr_.store(0U, std::memory_order_release);
    local_address_count_.store(0U, std::memory_order_release);
    for (std::atomic<uint8_t>& address : local_addresses_) {
        address.store(0U, std::memory_order_relaxed);
    }
    tx_seq_ = 0U;
    handlers_ = nullptr;
    handler_cnt_ = 0U;
    high_priority_burst_ = 0U;
    on_timeout_ = nullptr;
    on_timeout_arg_ = nullptr;
    on_timeout_ex_ = nullptr;
    on_timeout_ex_arg_ = nullptr;
    on_event_ = nullptr;
    on_event_arg_ = nullptr;
    on_ack_ = nullptr;
    on_ack_arg_ = nullptr;
    received_frames_.store(0U, std::memory_order_relaxed);
    codec_errors_.store(0U, std::memory_order_relaxed);
    forwarded_frames_.store(0U, std::memory_order_relaxed);
    transmitted_frames_.store(0U, std::memory_order_relaxed);
    security_drops_.store(0U, std::memory_order_relaxed);
    duplicate_requests_.store(0U, std::memory_order_relaxed);
    replay_conflicts_.store(0U, std::memory_order_relaxed);
    ack_timeouts_.store(0U, std::memory_order_relaxed);
    queued_requests_.store(0U, std::memory_order_relaxed);
    completed_requests_.store(0U, std::memory_order_relaxed);
    deferred_requests_.store(0U, std::memory_order_relaxed);
    queue_overflows_.store(0U, std::memory_order_relaxed);
    async_timeouts_.store(0U, std::memory_order_relaxed);
    unhandled_requests_.store(0U, std::memory_order_relaxed);
    transmit_failures_.store(0U, std::memory_order_relaxed);
}

bool Router::create_work_queues()
{
    if (!high_work_queue_.create(
            CONFIG_LINK_WORK_QUEUE_DEPTH, sizeof(QueuedRequest),
            high_work_storage_, sizeof(high_work_storage_))) {
        return false;
    }
    if (!low_work_queue_.create(
            CONFIG_LINK_WORK_QUEUE_DEPTH, sizeof(QueuedRequest),
            low_work_storage_, sizeof(low_work_storage_))) {
        high_work_queue_.destroy();
        return false;
    }
    return true;
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
        queued_requests_.load(std::memory_order_relaxed),
        completed_requests_.load(std::memory_order_relaxed),
        deferred_requests_.load(std::memory_order_relaxed),
        queue_overflows_.load(std::memory_order_relaxed),
        async_timeouts_.load(std::memory_order_relaxed),
        unhandled_requests_.load(std::memory_order_relaxed),
        transmit_failures_.load(std::memory_order_relaxed),
    };
}

DiagnosticSnapshot Router::diagnostics()
{
    DiagnosticSnapshot snapshot {};
    OperationGuard operation(*this);
    if (!operation) {
        return snapshot;
    }
#if defined(CONFIG_LINK_SECURITY)
    snapshot.flags |= DIAGNOSTICS_FLAG_SECURITY;
#endif
#if defined(CONFIG_LINK_DIAGNOSTICS_REQUIRE_SECURE)
    snapshot.flags |= DIAGNOSTICS_FLAG_SECURE_ONLY;
#endif
    const osal::MessageQueueStats high_stats = high_work_queue_.stats();
    const osal::MessageQueueStats low_stats = low_work_queue_.stats();
    snapshot.high_queue_depth = static_cast<uint16_t>(
        high_stats.current_depth > UINT16_MAX
            ? UINT16_MAX : high_stats.current_depth);
    snapshot.low_queue_depth = static_cast<uint16_t>(
        low_stats.current_depth > UINT16_MAX
            ? UINT16_MAX : low_stats.current_depth);
    snapshot.counters = stats();
    snapshot.link_count = link_cnt_;
    uint16_t async_count = 0U;
    {
        osal::LockGuard lock(async_mutex_);
        if (lock.owns_lock()) {
            for (const AsyncRequest& request : async_requests_) {
                async_count = static_cast<uint16_t>(
                    async_count + (request.used ? 1U : 0U));
            }
        }
    }
    snapshot.async_active = async_count;
    for (size_t index = 0U; index < link_cnt_; ++index) {
        LinkDiagnosticCounters& link = snapshot.links[index];
        link.link_id = links_[index] == nullptr ? 0U : links_[index]->id();
        link.is_ready = link_ready_[index].load(std::memory_order_relaxed);
        link.process_divider = links_[index] == nullptr
            ? 1U : links_[index]->process_divider();
        link.received_bytes =
            link_received_bytes_[index].load(std::memory_order_relaxed);
        link.received_frames =
            link_received_frames_[index].load(std::memory_order_relaxed);
        link.codec_errors =
            link_codec_errors_[index].load(std::memory_order_relaxed);
        link.transmitted_frames =
            link_transmitted_frames_[index].load(std::memory_order_relaxed);
        link.transmit_failures =
            link_transmit_failures_[index].load(std::memory_order_relaxed);
    }
    return snapshot;
}

bool Router::set_local_addresses(const uint8_t* addresses, size_t count)
{
    OperationGuard operation(*this);
    if (!operation || count > CONFIG_LINK_MAX_LOCAL_ADDRESSES
        || (count > 0U && addresses == nullptr)) {
        return false;
    }
    for (size_t index = 0U; index < count; ++index) {
        if (!is_unicast_addr(addresses[index])) {
            return false;
        }
        for (size_t previous = 0U; previous < index; ++previous) {
            if (addresses[previous] == addresses[index]) {
                return false;
            }
        }
    }

    osal::LockGuard address_lock(address_mutex_);
    if (!address_lock.owns_lock()) {
        return false;
    }

    local_address_count_.store(0U, std::memory_order_release);
    for (size_t index = 0U;
         index < CONFIG_LINK_MAX_LOCAL_ADDRESSES; ++index) {
        const uint8_t address = index < count ? addresses[index] : 0U;
        local_addresses_[index].store(address, std::memory_order_relaxed);
    }
    self_addr_.store(count == 0U ? 0U : addresses[0],
                     std::memory_order_release);
    local_address_count_.store(count, std::memory_order_release);
    return true;
}

void Router::set_self_addr(uint8_t addr)
{
    if (addr == 0U) {
        (void)set_local_addresses(nullptr, 0U);
        return;
    }
    (void)set_local_addresses(&addr, 1U);
}

bool Router::is_local_address(uint8_t address) const
{
    const size_t count =
        local_address_count_.load(std::memory_order_acquire);
    for (size_t index = 0U; index < count; ++index) {
        if (local_addresses_[index].load(std::memory_order_relaxed)
            == address) {
            return true;
        }
    }
    return false;
}

bool Router::has_local_address() const
{
    return local_address_count_.load(std::memory_order_acquire) > 0U;
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
            || entries[i].out_cnt == 0U || entries[i].out_cnt > 4U
            || (entries[i].mode == RouteMode::ByHost
                && entries[i].match_addr > UINT8_MAX)
            || (entries[i].mode == RouteMode::Direct
                && entries[i].match_addr != 0U
                && find_unique_link_unlocked(entries[i].match_addr)
                    == nullptr)) {
            return false;
        }
        for (uint8_t output = 0U; output < entries[i].out_cnt; ++output) {
            const uint16_t link_id = entries[i].out_links[output];
            if (link_id == 0U
                || find_unique_link_unlocked(link_id) == nullptr
                || (entries[i].mode == RouteMode::Direct
                    && entries[i].match_addr != 0U
                    && entries[i].match_addr == link_id)) {
                return false;
            }
            for (uint8_t previous = 0U; previous < output; ++previous) {
                if (entries[i].out_links[previous] == link_id) {
                    return false;
                }
            }
        }
        for (size_t previous = 0U; previous < i; ++previous) {
            if (route_entries_ambiguous(entries[previous], entries[i])) {
                return false;
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
    for (size_t index = 0U; index < CONFIG_LINK_MAX_ROUTES; ++index) {
        route_rr_[index] = 0U;
    }
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

int16_t Router::link_index(const Link* link) const
{
    if (link == nullptr) {
        return -1;
    }
    for (size_t index = 0U; index < link_cnt_; ++index) {
        if (links_[index] == link) {
            return static_cast<int16_t>(index);
        }
    }
    return -1;
}

Link* Router::select_out_link(const RouteEntry& entry, size_t route_index,
                              uint16_t excluded_link_id)
{
    if (entry.out_cnt == 0U || entry.out_cnt > 4U
        || route_index >= CONFIG_LINK_MAX_ROUTES) {
        return nullptr;
    }
    for (uint8_t offset = 0U; offset < entry.out_cnt; ++offset) {
        const uint8_t index =
            static_cast<uint8_t>(
                (route_rr_[route_index] + offset) % entry.out_cnt);
        Link* const candidate =
            find_unique_link_unlocked(entry.out_links[index]);
        if (candidate != nullptr && candidate->id() != excluded_link_id
            && candidate->is_ready()) {
            route_rr_[route_index] =
                static_cast<uint8_t>((index + 1U) % entry.out_cnt);
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
    int16_t best_score = -1;
    size_t best_index = route_cnt_;
    for (size_t index = 0U; index < route_cnt_; ++index) {
        const int16_t score =
            route_match_score(routes_[index], receiver, in_link_id);
        if (score > best_score) {
            best_score = score;
            best_index = index;
        }
    }
    return best_index < route_cnt_
        ? select_out_link(routes_[best_index], best_index, in_link_id)
        : nullptr;
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
    record_transmit(out, sent == static_cast<int>(len));
    return sent;
}

void Router::record_transmit(Link* link, bool succeeded)
{
    const int16_t index = link_index(link);
    if (succeeded) {
        transmitted_frames_.fetch_add(1U, std::memory_order_relaxed);
        if (index >= 0) {
            link_transmitted_frames_[static_cast<size_t>(index)].fetch_add(
                1U, std::memory_order_relaxed);
        }
        return;
    }
    transmit_failures_.fetch_add(1U, std::memory_order_relaxed);
    if (index >= 0) {
        link_transmit_failures_[static_cast<size_t>(index)].fetch_add(
            1U, std::memory_order_relaxed);
    }
}

void Router::emit_event(const RouterEvent& event)
{
    EventCallback callback = nullptr;
    void* callback_arg = nullptr;
    {
        osal::LockGuard lock(callback_mutex_);
        if (lock.owns_lock()) {
            callback = on_event_;
            callback_arg = on_event_arg_;
        }
    }
    if (callback != nullptr) {
        callback(event, callback_arg);
    }
}

void Router::check_link_state(size_t index)
{
    if (index >= link_cnt_ || links_[index] == nullptr) {
        return;
    }
    const bool ready = links_[index]->is_ready();
    const bool previous =
        link_ready_[index].exchange(ready, std::memory_order_acq_rel);
    if (ready == previous) {
        return;
    }
    RouterEvent event {};
    event.type = ready ? RouterEventType::LinkUp
                       : RouterEventType::LinkDown;
    event.link_id = links_[index]->id();
    emit_event(event);
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
    for (size_t index = 0U; index < link_cnt_; ++index) {
        process_link(index);
    }
    check_timeout();
    check_async_timeouts();
}

void Router::process_link(size_t index)
{
    check_link_state(index);
    Link* const input = links_[index];
    if (input == nullptr || !input->is_ready()) {
        return;
    }
    const uint8_t divider = input->process_divider();
    link_process_tick_[index] = static_cast<uint8_t>(
        link_process_tick_[index] + 1U);
    if (link_process_tick_[index] < divider) {
        return;
    }
    link_process_tick_[index] = 0U;
    input->poll();
    size_t rx_budget = CONFIG_LINK_PROCESS_RX_BUDGET;
    while (rx_budget > 0U) {
        uint8_t byte = 0U;
        if (input->recv(&byte, 1U) != 1) {
            break;
        }
        --rx_budget;
        link_received_bytes_[index].fetch_add(
            1U, std::memory_order_relaxed);
        const int result = codecs_[index].feed(byte);
        if (result < 0) {
            codec_errors_.fetch_add(1U, std::memory_order_relaxed);
            link_codec_errors_[index].fetch_add(
                1U, std::memory_order_relaxed);
            continue;
        }
        if (result == 0) {
            continue;
        }
        received_frames_.fetch_add(1U, std::memory_order_relaxed);
        link_received_frames_[index].fetch_add(
            1U, std::memory_order_relaxed);
        handle_frame(index, codecs_[index].frame(), codecs_[index].raw(),
                     codecs_[index].raw_len());
    }
}

void Router::handle_frame(size_t index, const Frame& frame,
                          const uint8_t* raw, size_t raw_len)
{
    Link* const input = links_[index];
    if (!is_unicast_addr(frame.sender_id)
        || frame.receiver_id == ADDR_RESERVED
        || (frame.is_ack() && !is_unicast_addr(frame.receiver_id))
        || (!frame.is_ack() && frame.receiver_id == ADDR_BROADCAST
            && frame.ack_mode() != AckMode::No)) {
        codec_errors_.fetch_add(1U, std::memory_order_relaxed);
        link_codec_errors_[index].fetch_add(
            1U, std::memory_order_relaxed);
        return;
    }
    if (!has_local_address()
        && frame.receiver_id == ADDR_BROADCAST) {
        return;
    }
    const bool for_local = is_local_address(frame.receiver_id)
        || frame.receiver_id == ADDR_BROADCAST;
    if (!for_local) {
        Link* const output = route_lookup(frame.receiver_id, input->id());
        if (output != nullptr
            && send_raw(output, raw, raw_len) == static_cast<int>(raw_len)) {
            forwarded_frames_.fetch_add(1U, std::memory_order_relaxed);
        }
        return;
    }
    if (frame.is_ack()) {
        Frame plaintext {};
        if (unprotect_frame(frame, plaintext)) {
            handle_ack(input, plaintext);
        } else {
            security_drops_.fetch_add(1U, std::memory_order_relaxed);
        }
        return;
    }
    handle_request(index, frame);
}

void Router::handle_request(size_t index, const Frame& frame)
{
    Link* const input = links_[index];
    const bool acknowledged = frame.receiver_id != ADDR_BROADCAST
        && frame.ack_mode() != AckMode::No;
    if (acknowledged && handle_replay(input, frame)) {
        return;
    }

    Frame plaintext {};
    if (!unprotect_frame(frame, plaintext)) {
        security_drops_.fetch_add(1U, std::memory_order_relaxed);
        return;
    }

    WorkKind kind {};
    const Handler* handler = nullptr;
    const uint8_t rejection_status =
        resolve_request(input, plaintext, kind, handler);
    if (rejection_status != HANDLER_STATUS_OK) {
        if (acknowledged) {
            const bool remembered = remember_request(
                frame, true, rejection_status);
            if (remembered) {
                send_ack(input, frame, rejection_status, nullptr, 0U);
            } else {
                reject_queue_request(input, frame);
            }
        }
        return;
    }
    enqueue_request(index, frame, plaintext, handler, kind);
}

bool Router::handle_replay(Link* input, const Frame& frame)
{
    ReplayResult replay {};
    size_t response_size = 0U;
    {
        osal::LockGuard lock(replay_mutex_);
        if (!lock.owns_lock()) {
            return true;
        }
        replay = replay_cache_.classify(
            frame, get_tick_ms(), CONFIG_LINK_REPLAY_RETENTION_MS);
        if (replay.decision == ReplayDecision::Duplicate
            && replay.response_available
            && !replay_cache_.copy_response(
                frame, replay_response_buffer_,
                sizeof(replay_response_buffer_), response_size)) {
            replay.response_available = false;
        }
    }
    if (replay.decision == ReplayDecision::Duplicate) {
        duplicate_requests_.fetch_add(1U, std::memory_order_relaxed);
        if (replay.response_available) {
            send_ack(input, frame, replay.ack_status,
                     response_size == 0U
                         ? nullptr : replay_response_buffer_,
                     response_size);
        }
        return true;
    }
    if (replay.decision != ReplayDecision::Conflict) {
        return false;
    }
    replay_conflicts_.fetch_add(1U, std::memory_order_relaxed);
    if (frame.enc_mode() == EncMode::None) {
        send_ack(input, frame, HANDLER_STATUS_REPLAY_CONFLICT,
                 nullptr, 0U);
    } else {
        security_drops_.fetch_add(1U, std::memory_order_relaxed);
    }
    return true;
}

uint8_t Router::resolve_request(Link* input, const Frame& frame,
                                WorkKind& kind,
                                const Handler*& handler)
{
    kind = WorkKind::Handler;
#if defined(CONFIG_LINK_DIAGNOSTICS)
    if (frame.cmd_set == CONFIG_LINK_DIAGNOSTICS_COMMAND_SET
        && frame.cmd_id == DIAGNOSTICS_STATS_COMMAND) {
        kind = WorkKind::Diagnostics;
    } else
#endif
    {
        handler = find_handler(frame);
    }
    if (kind == WorkKind::Handler && handler == nullptr) {
        unhandled_requests_.fetch_add(1U, std::memory_order_relaxed);
        RouterEvent event {};
        event.type = RouterEventType::UnhandledRequest;
        event.link_id = input->id();
        event.sequence = frame.seq;
        event.sender = frame.sender_id;
        event.receiver = frame.receiver_id;
        event.command_set = frame.cmd_set;
        event.command_id = frame.cmd_id;
        emit_event(event);
        return HANDLER_STATUS_NOT_FOUND;
    }
    bool secure_required = handler != nullptr
        && (handler->flags & HANDLER_REQUIRE_SECURE) != 0U;
#if defined(CONFIG_LINK_REQUIRE_SECURE_COMMANDS)
    secure_required = true;
#endif
#if defined(CONFIG_LINK_DIAGNOSTICS_REQUIRE_SECURE)
    secure_required = secure_required || kind == WorkKind::Diagnostics;
#endif
    if (secure_required && frame.enc_mode() == EncMode::None) {
        return HANDLER_STATUS_AUTH_REQUIRED;
    }
    const bool requires_completion = kind == WorkKind::Diagnostics
        || (handler != nullptr && handler->async_cb != nullptr);
    if (requires_completion
        && (frame.receiver_id == ADDR_BROADCAST
            || (frame.ack_mode() != AckMode::Finish
                && frame.ack_mode() != AckMode::Progress))) {
        return HANDLER_STATUS_INVALID_RESULT;
    }
    if (kind == WorkKind::Diagnostics && frame.data_len != 0U) {
        return HANDLER_STATUS_INVALID_RESULT;
    }
    return HANDLER_STATUS_OK;
}

void Router::enqueue_request(size_t index,
                             const Frame& protected_frame,
                             const Frame& plaintext,
                             const Handler* handler,
                             WorkKind kind)
{
    Link* const input = links_[index];
    const bool acknowledged =
        protected_frame.receiver_id != ADDR_BROADCAST
        && protected_frame.ack_mode() != AckMode::No;

    osal::MessageQueue& queue =
        plaintext.priority() == Priority::High
            ? high_work_queue_ : low_work_queue_;
    if (queue.free_slots() == 0U) {
        reject_queue_request(input, protected_frame);
        return;
    }

    if (acknowledged) {
        const bool has_early_response =
            protected_frame.ack_mode() == AckMode::Now
            || protected_frame.ack_mode() == AckMode::Progress;
        const uint8_t initial_status =
            protected_frame.ack_mode() == AckMode::Progress
                ? HANDLER_STATUS_IN_PROGRESS : HANDLER_STATUS_OK;
        if (!remember_request(protected_frame, has_early_response,
                              initial_status)) {
            reject_queue_request(input, protected_frame);
            return;
        }
    }
    if (!queue_request(index, plaintext, handler, kind)) {
        reject_queue_request(input, protected_frame);
        return;
    }
    if (acknowledged
        && (protected_frame.ack_mode() == AckMode::Now
            || protected_frame.ack_mode() == AckMode::Progress)) {
        const uint8_t initial_status =
            protected_frame.ack_mode() == AckMode::Progress
                ? HANDLER_STATUS_IN_PROGRESS : HANDLER_STATUS_OK;
        send_ack(input, protected_frame, initial_status, nullptr, 0U);
    }
    queued_requests_.fetch_add(1U, std::memory_order_relaxed);
    (void)work_available_.release();
}

void Router::reject_queue_request(Link* input, const Frame& frame)
{
    queue_overflows_.fetch_add(1U, std::memory_order_relaxed);
    const bool acknowledged = frame.receiver_id != ADDR_BROADCAST
        && frame.ack_mode() != AckMode::No;
    if (acknowledged) {
        (void)remember_request(frame, true, HANDLER_STATUS_QUEUE_FULL);
        send_ack(input, frame, HANDLER_STATUS_QUEUE_FULL, nullptr, 0U);
    }
    RouterEvent event {};
    event.type = RouterEventType::QueueOverflow;
    event.link_id = input == nullptr ? 0U : input->id();
    event.sequence = frame.seq;
    event.sender = frame.sender_id;
    event.receiver = frame.receiver_id;
    event.command_set = frame.cmd_set;
    event.command_id = frame.cmd_id;
    emit_event(event);
}

const Handler* Router::find_handler(const Frame& frame) const
{
    const Handler* match = nullptr;
    uint16_t best_specificity = 0U;
    for (size_t index = 0U; index < handler_cnt_; ++index) {
        const Handler& candidate = handlers_[index];
        if (!handler_matches(candidate, frame)) {
            continue;
        }
        const uint16_t specificity = handler_specificity(candidate);
        if (match == nullptr || specificity > best_specificity) {
            match = &candidate;
            best_specificity = specificity;
        }
    }
    return match;
}

bool Router::validate_handlers() const
{
    for (size_t index = 0U; index < handler_cnt_; ++index) {
        const Handler& handler = handlers_[index];
        const bool has_sync = handler.cb != nullptr;
        const bool has_async = handler.async_cb != nullptr;
        if (has_sync == has_async
            || (has_async && handler.finish_timeout_ms == 0U)
            || (has_async
                && handler.finish_timeout_ms
                    >= CONFIG_LINK_REPLAY_RETENTION_MS)
            || (handler.flags & ~HANDLER_REQUIRE_SECURE) != 0U) {
            return false;
        }
#if defined(CONFIG_LINK_DIAGNOSTICS)
        Frame diagnostics_frame {};
        diagnostics_frame.receiver_id = handler.receiver;
        diagnostics_frame.cmd_set = CONFIG_LINK_DIAGNOSTICS_COMMAND_SET;
        diagnostics_frame.cmd_id = DIAGNOSTICS_STATS_COMMAND;
        if (handler_matches(handler, diagnostics_frame)) {
            return false;
        }
#endif
        for (size_t previous = 0U; previous < index; ++previous) {
            if (handlers_ambiguous(handlers_[previous], handler)) {
                return false;
            }
        }
    }
    return true;
}

bool Router::queue_request(size_t source_link_index,
                           const Frame& frame,
                           const Handler* handler,
                           WorkKind kind)
{
    if (source_link_index >= link_cnt_ || frame.data_len > kPayloadCapacity
        || (frame.data_len > 0U && frame.data == nullptr)) {
        return false;
    }
    QueuedRequest request {};
    request.kind = kind;
    request.handler = handler;
    request.source_link_index = static_cast<uint8_t>(source_link_index);
    request.command_type = frame.cmd_type;
    request.sender = frame.sender_id;
    request.receiver = frame.receiver_id;
    request.sequence = frame.seq;
    request.command_set = frame.cmd_set;
    request.command_id = frame.cmd_id;
    request.data_len = frame.data_len;
    if (frame.data_len > 0U) {
        std::memcpy(request.data, frame.data, frame.data_len);
    }
    osal::MessageQueue& queue = frame.priority() == Priority::High
        ? high_work_queue_ : low_work_queue_;
    return queue.send(&request, 0U);
}

bool Router::remember_request(const Frame& frame,
                              bool response_available,
                              uint8_t status,
                              const uint8_t* data,
                              size_t data_len)
{
    osal::LockGuard lock(replay_mutex_);
    if (!lock.owns_lock()) {
        return false;
    }
    return replay_cache_.remember(
        frame, response_available, status, data, data_len,
        get_tick_ms(), CONFIG_LINK_REPLAY_RETENTION_MS);
}

void Router::update_replay_response(const CompletionToken& token,
                                    uint8_t status,
                                    const uint8_t* data,
                                    size_t data_len)
{
    osal::LockGuard lock(replay_mutex_);
    if (lock.owns_lock()) {
        (void)replay_cache_.update_response(
            token, status, data, data_len, get_tick_ms());
    }
}

bool Router::process_work(osal::Milliseconds timeout_ms)
{
    OperationGuard operation(*this);
    if (!operation) {
        return false;
    }
    osal::LockGuard work_lock(work_mutex_, 0U);
    if (!work_lock.owns_lock()) {
        return false;
    }
    (void)work_available_.take(timeout_ms);
    QueuedRequest request {};
    bool received = false;
    if (high_priority_burst_ < CONFIG_LINK_HIGH_PRIORITY_BURST) {
        received = high_work_queue_.receive(&request, 0U);
        if (received) {
            ++high_priority_burst_;
        }
    }
    if (!received && low_work_queue_.receive(&request, 0U)) {
        received = true;
        high_priority_burst_ = 0U;
    }
    if (!received && high_work_queue_.receive(&request, 0U)) {
        received = true;
        high_priority_burst_ = 1U;
    }
    if (!received) {
        return false;
    }
    execute_request(request);
    return true;
}

CompletionToken Router::make_completion_token(
    const QueuedRequest& request, uint32_t token_id) const
{
    CompletionToken token {};
    token.id = token_id;
    token.router_epoch = router_epoch_.load(std::memory_order_acquire);
    token.sequence = request.sequence;
    token.source_link_index = request.source_link_index;
    if (request.source_link_index < link_cnt_
        && links_[request.source_link_index] != nullptr) {
        token.source_link_id = links_[request.source_link_index]->id();
    }
    token.sender = request.sender;
    token.receiver = request.receiver;
    token.command_set = request.command_set;
    token.command_id = request.command_id;
    token.request_ack_mode = cmd_type_ack_mode(request.command_type);
    token.encryption_mode = cmd_type_enc_mode(request.command_type);
    token.priority = cmd_type_priority(request.command_type);
    return token;
}

bool Router::reserve_completion(const QueuedRequest& request,
                                uint32_t timeout_ms,
                                CompletionToken& token)
{
    if (timeout_ms == 0U) {
        return false;
    }
    uint32_t token_id = next_completion_id_.fetch_add(
        1U, std::memory_order_acq_rel);
    if (token_id == 0U) {
        token_id = next_completion_id_.fetch_add(
            1U, std::memory_order_acq_rel);
    }
    const CompletionToken candidate =
        make_completion_token(request, token_id);
    if (!candidate.is_valid() || candidate.source_link_id == 0U) {
        return false;
    }
    osal::LockGuard lock(async_mutex_);
    if (!lock.owns_lock()) {
        return false;
    }
    for (AsyncRequest& entry : async_requests_) {
        if (!entry.used) {
            entry.token = candidate;
            entry.started_ms = get_tick_ms();
            entry.timeout_ms = timeout_ms;
            entry.used = true;
            token = candidate;
            return true;
        }
    }
    return false;
}

bool Router::take_completion(const CompletionToken& token)
{
    if (!token.is_valid()) {
        return false;
    }
    osal::LockGuard lock(async_mutex_);
    if (!lock.owns_lock()) {
        return false;
    }
    for (AsyncRequest& entry : async_requests_) {
        const CompletionToken& active = entry.token;
        if (entry.used && completion_tokens_equal(active, token)) {
            entry.used = false;
            return true;
        }
    }
    return false;
}

bool Router::completion_tokens_equal(const CompletionToken& first,
                                     const CompletionToken& second)
{
    return first.id == second.id
        && first.router_epoch == second.router_epoch
        && first.sequence == second.sequence
        && first.source_link_id == second.source_link_id
        && first.source_link_index == second.source_link_index
        && first.sender == second.sender
        && first.receiver == second.receiver
        && first.command_set == second.command_set
        && first.command_id == second.command_id
        && first.request_ack_mode == second.request_ack_mode
        && first.encryption_mode == second.encryption_mode
        && first.priority == second.priority;
}

void Router::execute_request(const QueuedRequest& request)
{
    if (request.source_link_index >= link_cnt_
        || links_[request.source_link_index] == nullptr
        || request.data_len > kPayloadCapacity) {
        return;
    }
    const Frame frame = frame_from_request(request);
    if (request.kind == WorkKind::Diagnostics) {
        execute_diagnostics(request);
        return;
    }
    if (request.handler == nullptr) {
        return;
    }

    CompletionToken token = make_completion_token(request, 0U);
    bool completion_reserved = false;
    if (request.handler->async_cb != nullptr
        && frame.receiver_id != ADDR_BROADCAST
        && (frame.ack_mode() == AckMode::Finish
            || frame.ack_mode() == AckMode::Progress)) {
        completion_reserved = reserve_completion(
            request, request.handler->finish_timeout_ms, token);
        if (!completion_reserved) {
            token = make_completion_token(request, 0U);
            finish_request(token, HANDLER_STATUS_QUEUE_FULL, nullptr, 0U);
            return;
        }
    }

    HandlerResult result = HandlerResult::completed();
    if (request.handler->cb != nullptr) {
        request.handler->cb(frame, request.handler->arg);
    } else if (request.handler->async_cb != nullptr) {
        result = request.handler->async_cb(
            frame, token, request.handler->arg);
    }

    if (result.disposition == HandlerDisposition::Deferred) {
        if (completion_reserved && token.is_valid()) {
            if (completion_is_active(token)) {
                deferred_requests_.fetch_add(
                    1U, std::memory_order_relaxed);
            }
            return;
        }
        result = HandlerResult::completed(
            HANDLER_STATUS_INVALID_RESULT);
    } else if (result.disposition != HandlerDisposition::Completed) {
        result = HandlerResult::completed(
            HANDLER_STATUS_INVALID_RESULT);
    }

    if (completion_reserved && !take_completion(token)) {
        return;
    }
    constexpr size_t response_capacity = LINK_REPLAY_RESPONSE_CAPACITY;
    if (result.status == HANDLER_STATUS_IN_PROGRESS
        || result.status >= HANDLER_STATUS_INVALID_RESULT
        || result.data_len > response_capacity
        || (result.data_len > 0U && result.data == nullptr)) {
        result = HandlerResult::completed(
            HANDLER_STATUS_INVALID_RESULT);
    }
    completed_requests_.fetch_add(1U, std::memory_order_relaxed);
    if (frame.receiver_id != ADDR_BROADCAST
        && (frame.ack_mode() == AckMode::Finish
            || frame.ack_mode() == AckMode::Progress)) {
        finish_request(token, result.status, result.data, result.data_len);
    }
}

Frame Router::frame_from_request(const QueuedRequest& request) const
{
    Frame frame {};
    frame.sof = SOF;
    frame.ver = PROTOCOL_VERSION;
    frame.len = static_cast<uint16_t>(
        HEADER_SIZE + request.data_len + CRC_SIZE);
    frame.cmd_type = request.command_type;
    frame.sender_id = request.sender;
    frame.receiver_id = request.receiver;
    frame.seq = request.sequence;
    frame.cmd_set = request.command_set;
    frame.cmd_id = request.command_id;
    frame.data = request.data_len == 0U ? nullptr : request.data;
    frame.data_len = request.data_len;
    return frame;
}

bool Router::completion_is_active(const CompletionToken& token)
{
    osal::LockGuard lock(async_mutex_);
    if (!lock.owns_lock()) {
        return false;
    }
    for (const AsyncRequest& entry : async_requests_) {
        if (entry.used && completion_tokens_equal(entry.token, token)) {
            return true;
        }
    }
    return false;
}

void Router::execute_diagnostics(const QueuedRequest& request)
{
    uint8_t response[LINK_REPLAY_RESPONSE_CAPACITY] {};
    const DiagnosticSnapshot snapshot = diagnostics();
    const size_t response_size =
        encode_diagnostics(snapshot, response, sizeof(response));
    CompletionToken token = make_completion_token(request, 0U);
    const uint8_t status = response_size == 0U
        ? HANDLER_STATUS_INVALID_RESULT : HANDLER_STATUS_OK;
    finish_request(token, status,
                   response_size == 0U ? nullptr : response,
                   response_size);
    completed_requests_.fetch_add(1U, std::memory_order_relaxed);
}

void Router::finish_request(const CompletionToken& token,
                            uint8_t status,
                            const uint8_t* data,
                            size_t data_len)
{
    if (token.source_link_index >= link_cnt_
        || links_[token.source_link_index] == nullptr
        || links_[token.source_link_index]->id() != token.source_link_id) {
        return;
    }
    Frame request {};
    request.cmd_type = pack_cmd_type(
        false, token.request_ack_mode, token.encryption_mode,
        token.priority, false);
    request.sender_id = token.sender;
    request.receiver_id = token.receiver;
    request.seq = token.sequence;
    request.cmd_set = token.command_set;
    request.cmd_id = token.command_id;
    // Publish the terminal result before transmitting it. A concurrent v1
    // retransmission can then only replay the terminal response, never an
    // older progress state.
    update_replay_response(token, status, data, data_len);
    send_ack(links_[token.source_link_index], request, status, data, data_len);
}

bool Router::complete_request(const CompletionToken& token,
                              uint8_t status,
                              const uint8_t* data,
                              size_t data_len)
{
    OperationGuard operation(*this);
    constexpr size_t response_capacity = LINK_REPLAY_RESPONSE_CAPACITY;
    if (!operation || status == HANDLER_STATUS_IN_PROGRESS
        || status >= HANDLER_STATUS_INVALID_RESULT
        || data_len > response_capacity
        || (data_len > 0U && data == nullptr)
        || !take_completion(token)) {
        return false;
    }
    finish_request(token, status, data, data_len);
    completed_requests_.fetch_add(1U, std::memory_order_relaxed);
    return true;
}

bool Router::report_progress(const CompletionToken& token)
{
    OperationGuard operation(*this);
    if (!operation || !token.is_valid()
        || token.request_ack_mode != AckMode::Progress) {
        return false;
    }
    {
        osal::LockGuard lock(async_mutex_);
        if (!lock.owns_lock()) {
            return false;
        }
        bool found = false;
        for (AsyncRequest& entry : async_requests_) {
            if (entry.used
                && completion_tokens_equal(entry.token, token)) {
                entry.started_ms = get_tick_ms();
                found = true;
                break;
            }
        }
        if (!found) {
            return false;
        }
    }
    if (token.source_link_index >= link_cnt_
        || links_[token.source_link_index] == nullptr
        || links_[token.source_link_index]->id() != token.source_link_id) {
        return false;
    }
    Frame request {};
    request.cmd_type = pack_cmd_type(
        false, token.request_ack_mode, token.encryption_mode,
        token.priority, false);
    request.sender_id = token.sender;
    request.receiver_id = token.receiver;
    request.seq = token.sequence;
    request.cmd_set = token.command_set;
    request.cmd_id = token.command_id;
    update_replay_response(token, HANDLER_STATUS_IN_PROGRESS);
    send_ack(links_[token.source_link_index], request,
             HANDLER_STATUS_IN_PROGRESS, nullptr, 0U);
    return true;
}

void Router::check_async_timeouts()
{
    const uint32_t now = get_tick_ms();
    for (size_t index = 0U;
         index < CONFIG_LINK_MAX_ASYNC_REQUESTS; ++index) {
        CompletionToken token {};
        {
            osal::LockGuard lock(async_mutex_);
            if (!lock.owns_lock()) {
                return;
            }
            AsyncRequest& request = async_requests_[index];
            if (!request.used
                || (now - request.started_ms) < request.timeout_ms) {
                continue;
            }
            token = request.token;
            request.used = false;
        }
        finish_request(token, HANDLER_STATUS_TIMEOUT, nullptr, 0U);
        async_timeouts_.fetch_add(1U, std::memory_order_relaxed);
        RouterEvent event {};
        event.type = RouterEventType::AsyncTimeout;
        event.link_id = token.source_link_id;
        event.sequence = token.sequence;
        event.sender = token.sender;
        event.receiver = token.receiver;
        event.command_set = token.command_set;
        event.command_id = token.command_id;
        emit_event(event);
    }
}

int Router::send(const SendRequest& request)
{
    OperationGuard operation(*this);
    if (!operation) {
        return -1;
    }
    if (!validate_send_request(request)) {
        return -2;
    }
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
                                    request,
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
    }
    record_transmit(output, sent == static_cast<int>(frame_len));
    return sent;
}

bool Router::validate_send_request(const SendRequest& request) const
{
    if ((request.data_len > 0U && request.data == nullptr)
        || request.receiver == ADDR_RESERVED
        || (request.receiver == ADDR_BROADCAST
            && request.ack_mode != AckMode::No)
        || static_cast<uint8_t>(request.ack_mode)
            > static_cast<uint8_t>(AckMode::Progress)
        || static_cast<uint8_t>(request.enc_mode)
            > static_cast<uint8_t>(EncMode::ChaCha20)
        || static_cast<uint8_t>(request.priority)
            > static_cast<uint8_t>(Priority::High)
        || ((request.ack_mode == AckMode::Finish
             || request.ack_mode == AckMode::Progress)
            && (request.finish_timeout_ms == 0U
                || request.finish_timeout_ms
                    >= CONFIG_LINK_REPLAY_RETENTION_MS))
        || ((request.ack_mode == AckMode::No
             || request.ack_mode == AckMode::Now)
            && request.finish_timeout_ms != 0U)
        || (request.ack_callback == nullptr && request.ack_arg != nullptr)
        || (request.ack_mode == AckMode::No
            && request.ack_callback != nullptr)) {
        return false;
    }
#if defined(CONFIG_LINK_REQUIRE_SECURE_COMMANDS)
    return request.enc_mode != EncMode::None;
#else
    return true;
#endif
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
        || !is_local_address(frame.receiver_id)) {
        return;
    }
    bool matched = false;
    AckMode request_mode = AckMode::No;
    AckCallback request_callback = nullptr;
    void* request_callback_arg = nullptr;
    {
        osal::LockGuard lock(pending_mutex_);
        if (!lock.owns_lock()) {
            return;
        }
        for (TxPending& pending : pending_) {
            if (!pending.used || pending.seq != frame.seq
                || pending.raw_len < HEADER_SIZE
                || pending.out_link_idx >= link_cnt_
                || links_[pending.out_link_idx] != src
                || pending.raw_frame[kSenderOffset] != frame.receiver_id
                || pending.raw_frame[kReceiverOffset] != frame.sender_id
                || pending.raw_frame[kCommandSetOffset] != frame.cmd_set
                || pending.raw_frame[kCommandIdOffset] != frame.cmd_id
                || cmd_type_enc_mode(
                    pending.raw_frame[kCommandTypeOffset])
                    != frame.enc_mode()
                || frame.priority() != Priority::Low
                || frame.retransmit()) {
                continue;
            }
            const AckMode response_mode = frame.ack_mode();
            const AckResponseDisposition disposition =
                classify_ack_response(
                    pending.ack_mode, response_mode, frame.data[0]);
            if (disposition == AckResponseDisposition::Invalid) {
                break;
            }
            if (disposition == AckResponseDisposition::Accepted) {
                pending.accepted = true;
                pending.completion_wait_tick = get_tick_ms();
                pending.retry_cnt = 0U;
            } else {
                pending.used = false;
            }
            matched = true;
            request_mode = pending.ack_mode;
            request_callback = pending.ack_callback;
            request_callback_arg = pending.ack_arg;
            break;
        }
    }
    if (!matched) {
        return;
    }

    AckCallback callback = nullptr;
    void* callback_arg = nullptr;
    {
        osal::LockGuard lock(callback_mutex_);
        if (lock.owns_lock()) {
            callback = on_ack_;
            callback_arg = on_ack_arg_;
        }
    }
    AckEvent event {};
    event.kind = AckEventKind::Response;
    event.sequence = frame.seq;
    event.responder = frame.sender_id;
    event.local_address = frame.receiver_id;
    event.command_set = frame.cmd_set;
    event.command_id = frame.cmd_id;
    event.request_mode = request_mode;
    event.status = frame.data[0];
    event.data = frame.data_len > 1U ? &frame.data[1] : nullptr;
    event.data_len = frame.data_len - 1U;
    if (request_callback != nullptr) {
        request_callback(event, request_callback_arg);
    }
    if (callback != nullptr) {
        callback(event, callback_arg);
    }
}

void Router::send_ack(Link* out, const Frame& req, uint8_t status,
                      const uint8_t* data, size_t data_len)
{
    constexpr size_t payload_capacity =
        CONFIG_LINK_MAX_FRAME_SIZE - HEADER_SIZE - CRC_SIZE;
    if (out == nullptr || !out->is_ready()
        || !is_local_address(req.receiver_id)
        || (data_len > 0U && data == nullptr)
        || data_len > (payload_capacity - 1U)) {
        return;
    }
    osal::LockGuard send_lock(send_mutex_);
    if (!send_lock.owns_lock()) {
        return;
    }
    tx_buffer_[HEADER_SIZE] = status;
    if (data_len > 0U) {
        std::memcpy(&tx_buffer_[HEADER_SIZE + 1U], data, data_len);
    }

    PackArgs args {};
    args.sender = req.receiver_id;
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
    size_t payload_size = data_len + 1U;
    if (!protect_payload(security_context, payload, payload_size,
                         payload, payload_size)) {
        security_drops_.fetch_add(1U, std::memory_order_relaxed);
        return;
    }
    const size_t frame_len = FrameCodec::pack(tx_buffer_, sizeof(tx_buffer_),
                                              args, payload, payload_size);
    const bool sent = frame_len > 0U
        && out->send(tx_buffer_, frame_len) == static_cast<int>(frame_len);
    record_transmit(out, sent);
}

int Router::add_pending(uint16_t seq, const SendRequest& request,
                        uint8_t link_idx, const uint8_t* raw,
                        uint16_t raw_len)
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
            pending.ack_mode = request.ack_mode;
            pending.send_tick = get_tick_ms();
            pending.completion_wait_tick = pending.send_tick;
            pending.retry_cnt = 0U;
            pending.out_link_idx = link_idx;
            pending.raw_len = raw_len;
            pending.finish_timeout_ms = request.finish_timeout_ms;
            pending.ack_callback = request.ack_callback;
            pending.ack_arg = request.ack_arg;
            pending.accepted = false;
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
        uint8_t local_address = 0U;
        uint8_t receiver = 0U;
        uint8_t cmd_set = 0U;
        uint8_t cmd_id = 0U;
        AckMode timeout_mode = AckMode::No;
        AckCallback request_callback = nullptr;
        void* request_callback_arg = nullptr;

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
            bool retry = false;
            if (pending.ack_mode == AckMode::No) {
                pending.used = false;
                continue;
            }
            if (pending.ack_mode == AckMode::Progress
                && pending.accepted) {
                if ((now - pending.completion_wait_tick)
                    < pending.finish_timeout_ms) {
                    continue;
                }
                timeout_mode = AckMode::Progress;
                notify_timeout = true;
            } else if (pending.ack_mode == AckMode::Finish
                       || pending.ack_mode == AckMode::Progress) {
                if ((now - pending.completion_wait_tick)
                    >= pending.finish_timeout_ms) {
                    timeout_mode = pending.ack_mode;
                    notify_timeout = true;
                } else if ((now - pending.send_tick)
                               >= CONFIG_LINK_ACK_NOW_TIMEOUT_MS
                           && pending.retry_cnt
                               < CONFIG_LINK_ACK_NOW_RETRIES) {
                    retry = true;
                } else {
                    continue;
                }
            } else {
                if ((now - pending.send_tick)
                    < CONFIG_LINK_ACK_NOW_TIMEOUT_MS) {
                    continue;
                }
                if (pending.retry_cnt < CONFIG_LINK_ACK_NOW_RETRIES) {
                    retry = true;
                } else {
                    timeout_mode = pending.ack_mode;
                    notify_timeout = true;
                }
            }

            if (retry) {
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
                    const bool sent = output->send(
                        pending.raw_frame, pending.raw_len)
                        == static_cast<int>(pending.raw_len);
                    record_transmit(output, sent);
                }
                pending.retry_cnt++;
                pending.send_tick = now;
                continue;
            }
            if (notify_timeout && pending.raw_len >= HEADER_SIZE) {
                seq = pending.seq;
                local_address = pending.raw_frame[kSenderOffset];
                receiver = pending.raw_frame[kReceiverOffset];
                cmd_set = pending.raw_frame[kCommandSetOffset];
                cmd_id = pending.raw_frame[kCommandIdOffset];
                request_callback = pending.ack_callback;
                request_callback_arg = pending.ack_arg;
            }
            pending.used = false;
            ack_timeouts_.fetch_add(1U, std::memory_order_relaxed);
        }

        TimeoutCallback callback = nullptr;
        void* callback_arg = nullptr;
        TimeoutCallbackEx callback_ex = nullptr;
        void* callback_ex_arg = nullptr;
        AckCallback ack_callback = nullptr;
        void* ack_callback_arg = nullptr;
        {
            osal::LockGuard callback_lock(callback_mutex_);
            if (callback_lock.owns_lock()) {
                callback = on_timeout_;
                callback_arg = on_timeout_arg_;
                callback_ex = on_timeout_ex_;
                callback_ex_arg = on_timeout_ex_arg_;
                ack_callback = on_ack_;
                ack_callback_arg = on_ack_arg_;
            }
        }
        if (!notify_timeout) {
            continue;
        }
        AckEvent event {};
        event.kind = AckEventKind::Timeout;
        event.sequence = seq;
        event.responder = receiver;
        event.local_address = local_address;
        event.command_set = cmd_set;
        event.command_id = cmd_id;
        event.request_mode = timeout_mode;
        event.status = HANDLER_STATUS_TIMEOUT;
        if (request_callback != nullptr) {
            request_callback(event, request_callback_arg);
        }
        if (ack_callback != nullptr) {
            ack_callback(event, ack_callback_arg);
        }
        if (callback_ex != nullptr) {
            callback_ex(seq, receiver, cmd_set, cmd_id, timeout_mode,
                        callback_ex_arg);
        }
        if (callback != nullptr) {
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
osal::Thread g_link_worker;
alignas(std::max_align_t)
    uint8_t g_link_worker_stack[CONFIG_LINK_WORKER_STACK] {};
bool g_link_worker_created = false;

void link_process_entry(void*, const osal::PeriodicStats&)
{
    link::Router::instance().process();
}

void link_worker_entry(void*)
{
    while (!g_link_worker.stop_requested()) {
        (void)link::Router::instance().process_work(20U);
    }
}

int link_sys_init()
{
    if (g_link_thread_created) {
        return 0;
    }
    if (!link::Router::instance().init()) {
        return -1;
    }
    osal::ThreadConfig worker_config {};
    worker_config.name = "link_work";
    worker_config.priority =
        static_cast<osal::Priority>(CONFIG_LINK_WORKER_PRIO);
    worker_config.stack_buffer = g_link_worker_stack;
    worker_config.stack_size_bytes = sizeof(g_link_worker_stack);
    if (!g_link_worker.start(link_worker_entry, nullptr, worker_config)
        || g_link_worker.startup() != 0) {
        g_link_worker.destroy();
        (void)link::Router::instance().deinit();
        return -1;
    }
    g_link_worker_created = true;
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
        g_link_worker.destroy();
        g_link_worker_created = false;
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
    if (g_link_worker_created) {
        g_link_worker.destroy();
        g_link_worker_created = false;
    }
    return link::Router::instance().deinit() ? 0 : -1;
}

} // namespace

SYS_INIT_ROLLBACK(link_sys_init, link_sys_deinit,
                  INITCALL_LEVEL_APPLICATION, CONFIG_LINK_INIT_PRIORITY);
