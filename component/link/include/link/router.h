#pragma once

#include "codec.h"
#include "diagnostics.h"
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
#ifndef CONFIG_LINK_MAX_LOCAL_ADDRESSES
#define CONFIG_LINK_MAX_LOCAL_ADDRESSES 4
#endif
#ifndef CONFIG_LINK_WORK_QUEUE_DEPTH
#define CONFIG_LINK_WORK_QUEUE_DEPTH 4
#endif
#ifndef CONFIG_LINK_MAX_ASYNC_REQUESTS
#define CONFIG_LINK_MAX_ASYNC_REQUESTS 4
#endif
#ifndef CONFIG_LINK_HIGH_PRIORITY_BURST
#define CONFIG_LINK_HIGH_PRIORITY_BURST 4
#endif
#ifndef CONFIG_LINK_ACK_NOW_TIMEOUT_MS
#define CONFIG_LINK_ACK_NOW_TIMEOUT_MS 50
#endif
#ifndef CONFIG_LINK_ACK_NOW_RETRIES
#define CONFIG_LINK_ACK_NOW_RETRIES 2
#endif
#ifndef CONFIG_LINK_DIAGNOSTICS_COMMAND_SET
#define CONFIG_LINK_DIAGNOSTICS_COMMAND_SET 254
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

[[nodiscard]] inline constexpr uint8_t mask_bit_count(uint8_t value)
{
    uint8_t count = 0U;
    for (uint8_t bit = 0U; bit < 8U; ++bit) {
        count = static_cast<uint8_t>(
            count + ((value >> bit) & 0x01U));
    }
    return count;
}

/** Returns a larger value for a more specific match, or -1 for no match. */
[[nodiscard]] inline int16_t route_match_score(
    const RouteEntry& route, uint8_t receiver, uint16_t in_link_id)
{
    if (route.mode == RouteMode::Direct) {
        if (route.match_addr == 0U) {
            return 0;
        }
        return route.match_addr == in_link_id ? 32 : -1;
    }
    if (route.mode != RouteMode::ByHost
        || (receiver & route.mask)
            != (static_cast<uint8_t>(route.match_addr) & route.mask)) {
        return -1;
    }
    return static_cast<int16_t>(8U + mask_bit_count(route.mask));
}

[[nodiscard]] inline bool route_entries_ambiguous(
    const RouteEntry& first, const RouteEntry& second)
{
    if (first.mode != second.mode) {
        return false;
    }
    if (first.mode == RouteMode::Direct) {
        return first.match_addr == second.match_addr;
    }
    const uint8_t common_mask =
        static_cast<uint8_t>(first.mask & second.mask);
    const bool overlaps =
        ((static_cast<uint8_t>(first.match_addr)
          ^ static_cast<uint8_t>(second.match_addr))
         & common_mask) == 0U;
    return overlaps
        && mask_bit_count(first.mask) == mask_bit_count(second.mask);
}

[[nodiscard]] inline bool handler_matches(const Handler& handler,
                                          const Frame& frame)
{
    return (frame.receiver_id & handler.receiver_mask)
            == (handler.receiver & handler.receiver_mask)
        && (frame.cmd_set & handler.cmd_set_mask)
            == (handler.cmd_set & handler.cmd_set_mask)
        && (frame.cmd_id & handler.cmd_id_mask)
            == (handler.cmd_id & handler.cmd_id_mask);
}

[[nodiscard]] inline uint16_t handler_specificity(const Handler& handler)
{
    return static_cast<uint16_t>(mask_bit_count(handler.receiver_mask))
        + static_cast<uint16_t>(mask_bit_count(handler.cmd_set_mask))
        + static_cast<uint16_t>(mask_bit_count(handler.cmd_id_mask));
}

[[nodiscard]] inline bool handlers_ambiguous(const Handler& first,
                                             const Handler& second)
{
    const uint8_t receiver_mask =
        static_cast<uint8_t>(first.receiver_mask & second.receiver_mask);
    const uint8_t set_mask =
        static_cast<uint8_t>(first.cmd_set_mask & second.cmd_set_mask);
    const uint8_t id_mask =
        static_cast<uint8_t>(first.cmd_id_mask & second.cmd_id_mask);
    const bool overlaps = ((first.receiver ^ second.receiver)
                           & receiver_mask) == 0U
        && ((first.cmd_set ^ second.cmd_set) & set_mask) == 0U
        && ((first.cmd_id ^ second.cmd_id) & id_mask) == 0U;
    return overlaps
        && handler_specificity(first) == handler_specificity(second);
}

enum class AckEventKind : uint8_t {
    Response = 0U,
    Timeout,
};

enum class AckResponseDisposition : uint8_t {
    Invalid = 0U,
    Accepted,
    Terminal,
};

/** Interprets an ACK without changing the deployed Link v1 wire contract. */
[[nodiscard]] inline constexpr AckResponseDisposition classify_ack_response(
    AckMode request_mode, AckMode response_mode, uint8_t status)
{
    // Link v1 ACK frames leave ack_mode at No. The original request mode and
    // the response status together define whether the request is still active.
    if (request_mode == AckMode::No || response_mode != AckMode::No) {
        return AckResponseDisposition::Invalid;
    }

    switch (request_mode) {
    case AckMode::Now:
        return status == HANDLER_STATUS_IN_PROGRESS
            ? AckResponseDisposition::Invalid
            : AckResponseDisposition::Terminal;
    case AckMode::Finish:
        return status == HANDLER_STATUS_IN_PROGRESS
            ? AckResponseDisposition::Invalid
            : AckResponseDisposition::Terminal;
    case AckMode::Progress:
        return status == HANDLER_STATUS_IN_PROGRESS
            ? AckResponseDisposition::Accepted
            : AckResponseDisposition::Terminal;
    case AckMode::No:
    default:
        return AckResponseDisposition::Invalid;
    }
}

struct AckEvent {
    AckEventKind kind {AckEventKind::Response};
    uint16_t sequence {};
    uint8_t responder {};
    uint8_t local_address {};
    uint8_t command_set {};
    uint8_t command_id {};
    AckMode request_mode {AckMode::No};
    uint8_t status {};
    const uint8_t* data {nullptr};
    size_t data_len {};
};

using AckCallback = void (*)(const AckEvent& event, void* arg);

struct TxPending {
    uint16_t seq {};
    AckMode ack_mode {AckMode::No};
    uint32_t send_tick {};
    uint32_t completion_wait_tick {};
    uint8_t retry_cnt {};
    uint8_t out_link_idx {};
    uint8_t raw_frame[CONFIG_LINK_MAX_FRAME_SIZE] {};
    uint16_t raw_len {};
    uint32_t finish_timeout_ms {};
    AckCallback ack_callback {nullptr};
    void* ack_arg {nullptr};
    bool accepted {false};
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
    uint32_t finish_timeout_ms {};
    AckCallback ack_callback {nullptr};
    void* ack_arg {nullptr};
};

using RouterStats = DiagnosticCounters;

enum class RouterEventType : uint8_t {
    LinkUp = 0U,
    LinkDown,
    QueueOverflow,
    UnhandledRequest,
    AsyncTimeout,
};

struct RouterEvent {
    RouterEventType type {RouterEventType::LinkDown};
    uint16_t link_id {};
    uint16_t sequence {};
    uint8_t sender {};
    uint8_t receiver {};
    uint8_t command_set {};
    uint8_t command_id {};
    uint8_t detail {};
};

class Router {
public:
    using TimeoutCallback = void (*)(
        uint16_t seq, uint8_t receiver, uint8_t cmd_set, uint8_t cmd_id,
        void* arg);
    using TimeoutCallbackEx = void (*)(
        uint16_t seq, uint8_t receiver, uint8_t cmd_set, uint8_t cmd_id,
        AckMode request_mode, void* arg);
    using EventCallback = void (*)(const RouterEvent& event, void* arg);
    using AckCallback = link::AckCallback;

    static Router& instance();
    [[nodiscard]] bool set_routes(const RouteEntry* entries, size_t count);
    [[nodiscard]] bool set_security_provider(SecurityProvider* provider);
    [[nodiscard]] RouterStats stats() const;
    [[nodiscard]] DiagnosticSnapshot diagnostics();
    [[nodiscard]] bool set_local_addresses(const uint8_t* addresses,
                                           size_t count);
    void set_self_addr(uint8_t addr);
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
    void set_on_timeout_ex(TimeoutCallbackEx cb, void* arg = nullptr)
    {
        OperationGuard operation(*this);
        if (!operation) {
            return;
        }
        osal::LockGuard lock(callback_mutex_);
        if (lock.owns_lock()) {
            on_timeout_ex_ = cb;
            on_timeout_ex_arg_ = arg;
        }
    }
    void set_on_event(EventCallback cb, void* arg = nullptr)
    {
        OperationGuard operation(*this);
        if (!operation) {
            return;
        }
        osal::LockGuard lock(callback_mutex_);
        if (lock.owns_lock()) {
            on_event_ = cb;
            on_event_arg_ = arg;
        }
    }
    void set_on_ack(AckCallback cb, void* arg = nullptr)
    {
        OperationGuard operation(*this);
        if (!operation) {
            return;
        }
        osal::LockGuard lock(callback_mutex_);
        if (lock.owns_lock()) {
            on_ack_ = cb;
            on_ack_arg_ = arg;
        }
    }
    void process();
    [[nodiscard]] bool process_work(
        osal::Milliseconds timeout_ms = 20U);
    int send(const SendRequest& request);
    [[nodiscard]] bool complete_request(
        const CompletionToken& token,
        uint8_t status,
        const uint8_t* data = nullptr,
        size_t data_len = 0U);
    [[nodiscard]] bool report_progress(const CompletionToken& token);
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

    static constexpr size_t kPayloadCapacity =
        CONFIG_LINK_MAX_FRAME_SIZE - HEADER_SIZE - CRC_SIZE;

    enum class WorkKind : uint8_t {
        Handler = 0U,
        Diagnostics,
    };

    struct QueuedRequest {
        WorkKind kind {WorkKind::Handler};
        const Handler* handler {nullptr};
        uint8_t source_link_index {};
        uint8_t command_type {};
        uint8_t sender {};
        uint8_t receiver {};
        uint16_t sequence {};
        uint8_t command_set {};
        uint8_t command_id {};
        uint16_t data_len {};
        uint8_t data[kPayloadCapacity] {};
    };

    struct AsyncRequest {
        CompletionToken token {};
        uint32_t started_ms {};
        uint32_t timeout_ms {};
        bool used {false};
    };

    [[nodiscard]] bool begin_operation();
    void end_operation();
    [[nodiscard]] bool wait_for_quiescence(osal::Deadline& deadline);
    void clear_state();
    [[nodiscard]] bool create_work_queues();
    Link* find_unique_link_unlocked(uint16_t link_id);
    [[nodiscard]] int16_t link_index(const Link* link) const;

    Link* links_[CONFIG_LINK_MAX_LINKS] {};
    size_t link_cnt_ {0U};
    FrameCodec codecs_[CONFIG_LINK_MAX_LINKS] {};
    RouteEntry routes_[CONFIG_LINK_MAX_ROUTES] {};
    size_t route_cnt_ {0U};
    uint8_t route_rr_[CONFIG_LINK_MAX_ROUTES] {};
    osal::Mutex route_mutex_;
    std::atomic<uint8_t> self_addr_ {0U};
    std::atomic<uint8_t>
        local_addresses_[CONFIG_LINK_MAX_LOCAL_ADDRESSES] {};
    std::atomic<size_t> local_address_count_ {0U};
    osal::Mutex address_mutex_;
    uint16_t tx_seq_ {0U};
    const Handler* handlers_ {nullptr};
    size_t handler_cnt_ {0U};
    TimeoutCallback on_timeout_ {nullptr};
    void* on_timeout_arg_ {nullptr};
    TimeoutCallbackEx on_timeout_ex_ {nullptr};
    void* on_timeout_ex_arg_ {nullptr};
    EventCallback on_event_ {nullptr};
    void* on_event_arg_ {nullptr};
    AckCallback on_ack_ {nullptr};
    void* on_ack_arg_ {nullptr};
    osal::Mutex callback_mutex_;
    TxPending pending_[CONFIG_LINK_MAX_PENDING] {};
    ReplayCache replay_cache_ {};
    SecurityProvider* security_provider_ {nullptr};
    osal::MessageQueue high_work_queue_;
    osal::MessageQueue low_work_queue_;
    static constexpr size_t kWorkQueueStorageBytes =
        osal::MessageQueue::storage_size<
            QueuedRequest, CONFIG_LINK_WORK_QUEUE_DEPTH>();
    alignas(std::max_align_t)
        std::byte high_work_storage_[kWorkQueueStorageBytes] {};
    alignas(std::max_align_t)
        std::byte low_work_storage_[kWorkQueueStorageBytes] {};
    osal::Semaphore work_available_ {
        0U, CONFIG_LINK_WORK_QUEUE_DEPTH * 2U};
    AsyncRequest async_requests_[CONFIG_LINK_MAX_ASYNC_REQUESTS] {};
    uint8_t high_priority_burst_ {0U};
    osal::Mutex lifecycle_mutex_;
    osal::Mutex process_mutex_;
    osal::Mutex work_mutex_;
    osal::Mutex pending_mutex_;
    osal::Mutex send_mutex_;
    osal::Mutex security_mutex_;
    osal::Mutex replay_mutex_;
    osal::Mutex async_mutex_;
    uint8_t tx_buffer_[CONFIG_LINK_MAX_FRAME_SIZE] {};
    uint8_t security_tx_buffer_[CONFIG_LINK_MAX_FRAME_SIZE] {};
    uint8_t security_rx_buffer_[CONFIG_LINK_MAX_FRAME_SIZE] {};
    uint8_t replay_response_buffer_[LINK_REPLAY_RESPONSE_CAPACITY] {};
    std::atomic<bool> initialized_ {false};
    std::atomic<uint32_t> active_operations_ {0U};
    std::atomic<uint32_t> router_epoch_ {0U};
    std::atomic<uint32_t> next_completion_id_ {1U};
    std::atomic<uint32_t> received_frames_ {0U};
    std::atomic<uint32_t> codec_errors_ {0U};
    std::atomic<uint32_t> forwarded_frames_ {0U};
    std::atomic<uint32_t> transmitted_frames_ {0U};
    std::atomic<uint32_t> security_drops_ {0U};
    std::atomic<uint32_t> duplicate_requests_ {0U};
    std::atomic<uint32_t> replay_conflicts_ {0U};
    std::atomic<uint32_t> ack_timeouts_ {0U};
    std::atomic<uint32_t> queued_requests_ {0U};
    std::atomic<uint32_t> completed_requests_ {0U};
    std::atomic<uint32_t> deferred_requests_ {0U};
    std::atomic<uint32_t> queue_overflows_ {0U};
    std::atomic<uint32_t> async_timeouts_ {0U};
    std::atomic<uint32_t> unhandled_requests_ {0U};
    std::atomic<uint32_t> transmit_failures_ {0U};
    std::atomic<uint32_t> link_received_bytes_[CONFIG_LINK_MAX_LINKS] {};
    std::atomic<uint32_t> link_received_frames_[CONFIG_LINK_MAX_LINKS] {};
    std::atomic<uint32_t> link_codec_errors_[CONFIG_LINK_MAX_LINKS] {};
    std::atomic<uint32_t> link_transmitted_frames_[CONFIG_LINK_MAX_LINKS] {};
    std::atomic<uint32_t> link_transmit_failures_[CONFIG_LINK_MAX_LINKS] {};
    std::atomic<bool> link_ready_[CONFIG_LINK_MAX_LINKS] {};
    uint8_t link_process_tick_[CONFIG_LINK_MAX_LINKS] {};

    Link* route_lookup(uint8_t receiver, uint16_t in_link_id);
    Link* select_out_link(const RouteEntry& entry, size_t route_index,
                          uint16_t excluded_link_id);
    int send_raw(Link* out, const uint8_t* data, size_t len);
    [[nodiscard]] bool validate_send_request(
        const SendRequest& request) const;
    void process_link(size_t link_index);
    void handle_frame(size_t link_index, const Frame& frame,
                      const uint8_t* raw, size_t raw_len);
    void handle_request(size_t link_index, const Frame& frame);
    [[nodiscard]] bool handle_replay(Link* input, const Frame& frame);
    [[nodiscard]] uint8_t resolve_request(
        Link* input, const Frame& frame, WorkKind& kind,
        const Handler*& handler);
    void enqueue_request(size_t link_index, const Frame& protected_frame,
                         const Frame& plaintext, const Handler* handler,
                         WorkKind kind);
    void reject_queue_request(Link* input, const Frame& frame);
    [[nodiscard]] const Handler* find_handler(const Frame& frame) const;
    [[nodiscard]] bool validate_handlers() const;
    [[nodiscard]] bool queue_request(size_t link_index,
                                     const Frame& frame,
                                     const Handler* handler,
                                     WorkKind kind);
    void execute_request(const QueuedRequest& request);
    [[nodiscard]] Frame frame_from_request(
        const QueuedRequest& request) const;
    [[nodiscard]] bool completion_is_active(
        const CompletionToken& token);
    void execute_diagnostics(const QueuedRequest& request);
    [[nodiscard]] bool reserve_completion(
        const QueuedRequest& request,
        uint32_t timeout_ms,
        CompletionToken& token);
    [[nodiscard]] CompletionToken make_completion_token(
        const QueuedRequest& request, uint32_t token_id) const;
    [[nodiscard]] bool take_completion(const CompletionToken& token);
    [[nodiscard]] static bool completion_tokens_equal(
        const CompletionToken& first,
        const CompletionToken& second);
    void check_async_timeouts();
    void handle_ack(Link* src, const Frame& frame);
    void send_ack(Link* out, const Frame& req, uint8_t status,
                  const uint8_t* data, size_t data_len);
    void finish_request(const CompletionToken& token, uint8_t status,
                        const uint8_t* data, size_t data_len);
    [[nodiscard]] bool remember_request(
        const Frame& frame, bool response_available,
        uint8_t status, const uint8_t* data = nullptr,
        size_t data_len = 0U);
    void update_replay_response(const CompletionToken& token,
                                uint8_t status,
                                const uint8_t* data = nullptr,
                                size_t data_len = 0U);
    [[nodiscard]] bool is_local_address(uint8_t address) const;
    [[nodiscard]] bool has_local_address() const;
    void check_link_state(size_t index);
    void record_transmit(Link* link, bool succeeded);
    void emit_event(const RouterEvent& event);
    void check_timeout();
    int add_pending(uint16_t seq, const SendRequest& request,
                    uint8_t link_idx, const uint8_t* raw,
                    uint16_t raw_len);
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
