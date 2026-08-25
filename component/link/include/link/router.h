#pragma once

#include "codec.h"
#include "frame.h"
#include "link.h"

#include <osal.h>

#include <atomic>
#include <cstddef>
#include <cstdint>

#ifndef CONFIG_LINK_MAX_LINKS
#define CONFIG_LINK_MAX_LINKS 8
#endif
#ifndef CONFIG_LINK_MAX_ROUTES
#define CONFIG_LINK_MAX_ROUTES 16
#endif
#ifndef CONFIG_LINK_MAX_PENDING
#define CONFIG_LINK_MAX_PENDING 8
#endif

namespace link {

enum class RouteMode : uint8_t { ByHost, Direct };

struct RouteEntry {
    RouteMode mode {};
    uint16_t match_addr {};
    uint8_t mask {};
    uint8_t out_cnt {};
    uint16_t out_links[4] {};
};

struct RouteBuilder {
    RouteMode mode {};
    uint16_t match_addr {};
    uint8_t mask {};
    uint16_t out_links[4] {};
    uint8_t out_cnt {};

    RouteBuilder& to(uint16_t link_id)
    {
        if (out_cnt < 4U) {
            out_links[out_cnt++] = link_id;
        }
        return *this;
    }
};

inline RouteBuilder route_by_host(uint8_t host_id, uint8_t mask = 0xF0U)
{
    return {RouteMode::ByHost, host_id, mask, {}, 0U};
}

inline RouteBuilder route_by_host_idx(uint8_t addr, uint8_t mask = 0xFFU)
{
    return {RouteMode::ByHost, addr, mask, {}, 0U};
}

inline RouteBuilder route_direct(uint16_t from_link_id = 0U)
{
    return {RouteMode::Direct, from_link_id, 0xFFU, {}, 0U};
}

inline RouteEntry make_route(const RouteBuilder& builder)
{
    RouteEntry entry {};
    entry.mode = builder.mode;
    entry.match_addr = builder.match_addr;
    entry.mask = builder.mask;
    entry.out_cnt = builder.out_cnt;
    for (uint8_t i = 0U; i < builder.out_cnt; ++i) {
        entry.out_links[i] = builder.out_links[i];
    }
    return entry;
}

struct TxPending {
    uint16_t seq {};
    uint8_t ack_mode {};
    uint32_t send_tick {};
    uint8_t retry_cnt {};
    uint8_t out_link_idx {};
    uint8_t raw_frame[CONFIG_LINK_MAX_FRAME_SIZE] {};
    uint16_t raw_len {};
    bool used {false};
};

class Router {
public:
    using TimeoutCallback = void (*)(uint16_t seq, uint8_t receiver,
                                     uint8_t cmd_set, uint8_t cmd_id, void* arg);

    static Router& instance();
    [[nodiscard]] bool set_routes(const RouteEntry* entries, size_t count);
    void set_self_addr(uint8_t addr)
    {
        self_addr_.store(addr, std::memory_order_release);
    }
    void set_on_timeout(TimeoutCallback cb, void* arg = nullptr)
    {
        osal::LockGuard lock(callback_mutex_);
        if (lock.owns_lock()) {
            on_timeout_ = cb;
            on_timeout_arg_ = arg;
        }
    }
    void process();
    int send(uint8_t receiver, uint8_t cmd_set, uint8_t cmd_id,
             const uint8_t* data, size_t data_len,
             uint8_t ack_mode = 0U, uint8_t enc_mode = 0U,
             uint8_t priority = 0U);
    Link* find_link(uint16_t link_id);
    Link* find_link_by_index(size_t idx)
    {
        return (idx < link_cnt_) ? links_[idx] : nullptr;
    }
    [[nodiscard]] size_t link_count() const { return link_cnt_; }
    void init();
    void deinit();

private:
    Router() = default;

    Link* links_[CONFIG_LINK_MAX_LINKS] {};
    size_t link_cnt_ {0U};
    FrameCodec codecs_[CONFIG_LINK_MAX_LINKS] {};
    RouteEntry routes_[CONFIG_LINK_MAX_ROUTES] {};
    size_t route_cnt_ {0U};
    uint8_t rr_idx_ {0U};
    osal::Mutex route_mutex_;
    std::atomic<uint8_t> self_addr_ {0U};
    uint16_t tx_seq_ {0U};
    const Handler* handlers_ {nullptr};
    size_t handler_cnt_ {0U};
    TimeoutCallback on_timeout_ {nullptr};
    void* on_timeout_arg_ {nullptr};
    osal::Mutex callback_mutex_;
    TxPending pending_[CONFIG_LINK_MAX_PENDING] {};
    osal::Mutex pending_mutex_;
    osal::Mutex send_mutex_;

    Link* route_lookup(uint8_t receiver, uint16_t in_link_id);
    Link* select_out_link(const RouteEntry& entry);
    int send_raw(Link* out, const uint8_t* data, size_t len);
    void dispatch(Link* src, const Frame& frame);
    void handle_ack(const Frame& frame);
    void send_ack(Link* out, const Frame& req, uint8_t status,
                  const uint8_t* data, size_t data_len);
    void check_timeout();
    int add_pending(uint16_t seq, uint8_t ack_mode, uint8_t link_idx,
                    const uint8_t* raw, uint16_t raw_len);
    void remove_pending(uint16_t seq);
    static uint32_t get_tick_ms();
};

} // namespace link
