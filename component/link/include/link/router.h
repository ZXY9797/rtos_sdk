#pragma once

#include "codec.h"
#include "frame.h"
#include "link.h"
#include "replay_cache.h"
#include "security.h"

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
#ifndef CONFIG_LINK_REPLAY_RETENTION_MS
#define CONFIG_LINK_REPLAY_RETENTION_MS 10000
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
    bool overflowed {false};

    RouteBuilder& to(uint16_t link_id)
    {
        if (out_cnt < 4U) {
            out_links[out_cnt++] = link_id;
        } else {
            overflowed = true;
        }
        return *this;
    }
};

inline RouteBuilder route_by_host(uint8_t host_id, uint8_t mask = 0xF0U)
{
    return {RouteMode::ByHost, host_id, mask, {}, 0U, false};
}

inline RouteBuilder route_by_host_idx(uint8_t addr, uint8_t mask = 0xFFU)
{
    return {RouteMode::ByHost, addr, mask, {}, 0U, false};
}

inline RouteBuilder route_direct(uint16_t from_link_id = 0U)
{
    return {RouteMode::Direct, from_link_id, 0xFFU, {}, 0U, false};
}

inline RouteEntry make_route(const RouteBuilder& builder)
{
    RouteEntry entry {};
    entry.mode = builder.mode;
    entry.match_addr = builder.match_addr;
    entry.mask = builder.mask;
    const bool count_valid = !builder.overflowed && builder.out_cnt <= 4U;
    entry.out_cnt = count_valid ? builder.out_cnt : UINT8_MAX;
    if (count_valid) {
        for (uint8_t i = 0U; i < builder.out_cnt; ++i) {
            entry.out_links[i] = builder.out_links[i];
        }
    }
    return entry;
}

struct TxPending {
    uint16_t seq {};
    AckMode ack_mode {AckMode::No};
    uint32_t send_tick {};
    uint8_t retry_cnt {};
    uint8_t out_link_idx {};
    uint8_t raw_frame[CONFIG_LINK_MAX_FRAME_SIZE] {};
    uint16_t raw_len {};
    bool used {false};
};

struct SendRequest {
    uint8_t receiver {};
    uint8_t cmd_set {};
    uint8_t cmd_id {};
    const uint8_t* data {nullptr};
    size_t data_len {};
    AckMode ack_mode {AckMode::No};
    EncMode enc_mode {EncMode::None};
    Priority priority {Priority::Low};
};

struct RouterStats {
    uint32_t received_frames {};
    uint32_t codec_errors {};
    uint32_t forwarded_frames {};
    uint32_t transmitted_frames {};
    uint32_t security_drops {};
    uint32_t duplicate_requests {};
    uint32_t replay_conflicts {};
    uint32_t ack_timeouts {};
};

class Router {
public:
    using TimeoutCallback = void (*)(uint16_t seq, uint8_t receiver,
                                     uint8_t cmd_set, uint8_t cmd_id, void* arg);

    static Router& instance();
    [[nodiscard]] bool set_routes(const RouteEntry* entries, size_t count);
    [[nodiscard]] bool set_security_provider(SecurityProvider* provider);
    [[nodiscard]] RouterStats stats() const;
    void set_self_addr(uint8_t addr)
    {
        OperationGuard operation(*this);
        if (!operation) {
            return;
        }
        self_addr_.store(addr, std::memory_order_release);
    }
    void set_on_timeout(TimeoutCallback cb, void* arg = nullptr)
    {
        OperationGuard operation(*this);
        if (!operation) {
            return;
        }
        osal::LockGuard lock(callback_mutex_);
        if (lock.owns_lock()) {
            on_timeout_ = cb;
            on_timeout_arg_ = arg;
        }
    }
    void process();
    int send(const SendRequest& request);
    [[nodiscard]] bool init();
    [[nodiscard]] bool deinit(osal::Milliseconds timeout_ms = 100U);

private:
    class OperationGuard {
    public:
        explicit OperationGuard(Router& router);
        ~OperationGuard();
        OperationGuard(const OperationGuard&) = delete;
        OperationGuard& operator=(const OperationGuard&) = delete;
        explicit operator bool() const { return active_; }
    private:
        Router& router_;
        bool active_;
    };

    Router() = default;

    [[nodiscard]] bool begin_operation();
    void end_operation();
    [[nodiscard]] bool wait_for_quiescence(osal::Deadline& deadline);
    void clear_state();
    Link* find_unique_link_unlocked(uint16_t link_id);

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
    ReplayCache replay_cache_ {};
    SecurityProvider* security_provider_ {nullptr};
    osal::Mutex lifecycle_mutex_;
    osal::Mutex process_mutex_;
    osal::Mutex pending_mutex_;
    osal::Mutex send_mutex_;
    osal::Mutex security_mutex_;
    uint8_t tx_buffer_[CONFIG_LINK_MAX_FRAME_SIZE] {};
    uint8_t security_tx_buffer_[CONFIG_LINK_MAX_FRAME_SIZE] {};
    uint8_t security_rx_buffer_[CONFIG_LINK_MAX_FRAME_SIZE] {};
    std::atomic<bool> initialized_ {false};
    std::atomic<uint32_t> active_operations_ {0U};
    std::atomic<uint32_t> received_frames_ {0U};
    std::atomic<uint32_t> codec_errors_ {0U};
    std::atomic<uint32_t> forwarded_frames_ {0U};
    std::atomic<uint32_t> transmitted_frames_ {0U};
    std::atomic<uint32_t> security_drops_ {0U};
    std::atomic<uint32_t> duplicate_requests_ {0U};
    std::atomic<uint32_t> replay_conflicts_ {0U};
    std::atomic<uint32_t> ack_timeouts_ {0U};

    Link* route_lookup(uint8_t receiver, uint16_t in_link_id);
    Link* select_out_link(const RouteEntry& entry);
    int send_raw(Link* out, const uint8_t* data, size_t len);
    [[nodiscard]] uint8_t dispatch(Link* src, const Frame& frame);
    void handle_ack(Link* src, const Frame& frame);
    void send_ack(Link* out, const Frame& req, uint8_t status,
                  const uint8_t* data, size_t data_len);
    void check_timeout();
    int add_pending(uint16_t seq, AckMode ack_mode, uint8_t link_idx,
                    const uint8_t* raw, uint16_t raw_len);
    [[nodiscard]] bool next_sequence(uint16_t& sequence);
    void remove_pending(uint16_t seq);
    [[nodiscard]] bool unprotect_frame(const Frame& frame,
                                       Frame& plaintext);
    [[nodiscard]] bool protect_payload(const SecurityContext& context,
                                       const uint8_t* plaintext,
                                       size_t plaintext_size,
                                       const uint8_t*& payload,
                                       size_t& payload_size);
    static uint32_t get_tick_ms();
};

} // namespace link
