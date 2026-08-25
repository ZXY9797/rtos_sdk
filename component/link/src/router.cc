#include "link/router.h"

#include <init.h>
#include <osal.h>

#include <cstring>

namespace link {
namespace {

constexpr uint8_t kProgressStatus = 0x01U;

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

void Router::init()
{
    deinit();
    link_cnt_ = (g_link_registry.cnt <= CONFIG_LINK_MAX_LINKS)
        ? g_link_registry.cnt : CONFIG_LINK_MAX_LINKS;
    for (size_t i = 0U; i < link_cnt_; ++i) {
        links_[i] = g_link_registry.links[i];
    }
    handlers_ = &__link_handler_start;
    handler_cnt_ = static_cast<size_t>(&__link_handler_end - &__link_handler_start);
}

void Router::deinit()
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
    link_cnt_ = 0U;
    route_cnt_ = 0U;
    rr_idx_ = 0U;
    self_addr_.store(0U, std::memory_order_release);
    tx_seq_ = 0U;
    handlers_ = nullptr;
    handler_cnt_ = 0U;
    on_timeout_ = nullptr;
    on_timeout_arg_ = nullptr;
}

bool Router::set_routes(const RouteEntry* entries, size_t count)
{
    if ((count > 0U && entries == nullptr) || count > CONFIG_LINK_MAX_ROUTES) {
        return false;
    }
    for (size_t i = 0U; i < count; ++i) {
        if (entries[i].out_cnt == 0U || entries[i].out_cnt > 4U) {
            return false;
        }
        for (uint8_t output = 0U; output < entries[i].out_cnt; ++output) {
            const uint16_t link_id = entries[i].out_links[output];
            if (link_id == 0U || find_link(link_id) == nullptr) {
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

Link* Router::find_link(uint16_t link_id)
{
    for (size_t i = 0U; i < link_cnt_; ++i) {
        if (links_[i] != nullptr && links_[i]->id() == link_id) {
            return links_[i];
        }
    }
    return nullptr;
}

Link* Router::select_out_link(const RouteEntry& entry)
{
    if (entry.out_cnt == 0U || entry.out_cnt > 4U) {
        return nullptr;
    }
    for (uint8_t offset = 0U; offset < entry.out_cnt; ++offset) {
        const uint8_t index = static_cast<uint8_t>((rr_idx_ + offset) % entry.out_cnt);
        Link* const candidate = find_link(entry.out_links[index]);
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
    return lock.owns_lock() ? out->send(data, len) : -1;
}

void Router::process()
{
    uint8_t byte = 0U;
    for (size_t i = 0U; i < link_cnt_; ++i) {
        Link* const input = links_[i];
        if (input == nullptr || !input->is_ready()) {
            continue;
        }
        while (input->recv(&byte, 1U) == 1) {
            const int result = codecs_[i].feed(byte);
            if (result <= 0) {
                continue;
            }
            const Frame& frame = codecs_[i].frame();
            const bool for_me = frame.receiver_id
                                  == self_addr_.load(std::memory_order_acquire)
                             || frame.receiver_id == ADDR_BROADCAST;
            if (!for_me) {
                Link* const output = route_lookup(frame.receiver_id, input->id());
                if (output != nullptr) {
                    (void)send_raw(output, codecs_[i].raw(), codecs_[i].raw_len());
                }
            } else if (frame.enc_mode() != EncMode::None) {
                continue;
            } else if (frame.is_ack()) {
                handle_ack(frame);
            } else {
                dispatch(input, frame);
            }
        }
    }
    check_timeout();
}

void Router::dispatch(Link* src, const Frame& frame)
{
    (void)src;
    for (size_t i = 0U; i < handler_cnt_; ++i) {
        const Handler& handler = handlers_[i];
        if (handler.cmd_set == frame.cmd_set && handler.cmd_id == frame.cmd_id) {
            if (handler.cb != nullptr) {
                handler.cb(frame, handler.arg);
            }
            return;
        }
    }
}

int Router::send(uint8_t receiver, uint8_t cmd_set, uint8_t cmd_id,
                 const uint8_t* data, size_t data_len, uint8_t ack_mode,
                 uint8_t enc_mode, uint8_t priority)
{
    if ((data_len > 0U && data == nullptr)
        || ack_mode > static_cast<uint8_t>(AckMode::Progress)
        || enc_mode != static_cast<uint8_t>(EncMode::None)
        || priority > static_cast<uint8_t>(Priority::High)) {
        return -2;
    }
    Link* const output = route_lookup(receiver, 0U);
    if (output == nullptr) {
        return -1;
    }

    osal::LockGuard send_lock(send_mutex_);
    if (!send_lock.owns_lock()) {
        return -1;
    }
    PackArgs args {};
    args.sender = self_addr_.load(std::memory_order_acquire);
    args.receiver = receiver;
    args.cmd_set = cmd_set;
    args.cmd_id = cmd_id;
    args.ack_mode = static_cast<AckMode>(ack_mode);
    args.enc_mode = static_cast<EncMode>(enc_mode);
    args.priority = static_cast<Priority>(priority);
    args.seq = tx_seq_++;

    uint8_t frame_buf[CONFIG_LINK_MAX_FRAME_SIZE] {};
    const size_t frame_len = FrameCodec::pack(frame_buf, sizeof(frame_buf),
                                              args, data, data_len);
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
        pending_index = add_pending(args.seq, ack_mode,
                                    static_cast<uint8_t>(link_index), frame_buf,
                                    static_cast<uint16_t>(frame_len));
        if (pending_index < 0) {
            return -3;
        }
    }
    const int sent = output->send(frame_buf, frame_len);
    if (sent != static_cast<int>(frame_len) && pending_index >= 0) {
        remove_pending(args.seq);
    }
    return sent;
}

void Router::handle_ack(const Frame& frame)
{
    const uint8_t status = (frame.data_len > 0U && frame.data != nullptr)
        ? frame.data[0] : 0U;
    osal::LockGuard lock(pending_mutex_);
    if (!lock.owns_lock()) {
        return;
    }
    for (TxPending& pending : pending_) {
        if (!pending.used || pending.seq != frame.seq) {
            continue;
        }
        if (status == kProgressStatus
            && pending.ack_mode == static_cast<uint8_t>(AckMode::Progress)) {
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
    if (out == nullptr || (data_len > 0U && data == nullptr)) {
        return;
    }
    uint8_t ack_data[CONFIG_LINK_MAX_FRAME_SIZE - HEADER_SIZE - CRC_SIZE] {};
    const size_t copy_len = (data_len < (sizeof(ack_data) - 1U))
        ? data_len : (sizeof(ack_data) - 1U);
    ack_data[0] = status;
    if (copy_len > 0U) {
        std::memcpy(&ack_data[1], data, copy_len);
    }

    PackArgs args {};
    args.sender = self_addr_.load(std::memory_order_acquire);
    args.receiver = req.sender_id;
    args.cmd_set = req.cmd_set;
    args.cmd_id = req.cmd_id;
    args.is_ack = true;
    args.seq = req.seq;
    uint8_t frame_buf[CONFIG_LINK_MAX_FRAME_SIZE] {};
    const size_t frame_len = FrameCodec::pack(frame_buf, sizeof(frame_buf), args,
                                              ack_data, copy_len + 1U);
    if (frame_len > 0U) {
        (void)send_raw(out, frame_buf, frame_len);
    }
}

int Router::add_pending(uint16_t seq, uint8_t ack_mode, uint8_t link_idx,
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
            if (!retry_policy(static_cast<AckMode>(pending.ack_mode),
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
                    receiver = pending.raw_frame[6];
                    cmd_set = pending.raw_frame[9];
                    cmd_id = pending.raw_frame[10];
                    notify_timeout = true;
                }
                pending.used = false;
            } else {
                pending.raw_frame[4] |= static_cast<uint8_t>(
                    1U << CMD_TYPE_RETRANSMIT_SHIFT);
                const size_t crc_offset = pending.raw_len - CRC_SIZE;
                const uint16_t crc = crc16_ccitt(pending.raw_frame, crc_offset);
                pending.raw_frame[crc_offset] = static_cast<uint8_t>(crc & 0xFFU);
                pending.raw_frame[crc_offset + 1U] = static_cast<uint8_t>(crc >> 8U);
                Link* output = nullptr;
                if (pending.out_link_idx < link_cnt_) {
                    output = links_[pending.out_link_idx];
                }
                if (output != nullptr && output->is_ready()) {
                    (void)output->send(pending.raw_frame, pending.raw_len);
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

osal::PeriodicThread* g_link_thread = nullptr;

void link_process_entry(void*, const osal::PeriodicStats&)
{
    link::Router& router = link::Router::instance();
    for (size_t i = 0U; i < router.link_count(); ++i) {
        link::Link* const physical_link = router.find_link_by_index(i);
        if (physical_link != nullptr) {
            physical_link->poll();
        }
    }
    router.process();
}

int link_sys_init()
{
    if (g_link_thread != nullptr) {
        return 0;
    }
    link::Router::instance().init();
    g_link_thread = osal::PeriodicThread::create(
        "link", link_process_entry, nullptr,
        CONFIG_LINK_PROCESS_STACK, CONFIG_LINK_PROCESS_PRIO,
        CONFIG_LINK_PROCESS_HZ, osal::PeriodicTrigger::Tick);
    if (g_link_thread == nullptr || g_link_thread->startup() != 0) {
        delete g_link_thread;
        g_link_thread = nullptr;
        link::Router::instance().deinit();
        return -1;
    }
    return 0;
}

int link_sys_deinit()
{
    delete g_link_thread;
    g_link_thread = nullptr;
    link::Router::instance().deinit();
    return 0;
}

} // namespace

SYS_INIT_ROLLBACK(link_sys_init, link_sys_deinit,
                  INITCALL_LEVEL_POST_KERNEL, 20);
