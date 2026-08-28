#pragma once

#include <osal/osal.h>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

/// Timer ISR to optional divider to task wake-up to synchronous sensor read.
/// The owned PeriodicThread is notified directly without an IsrTrigger slot.
class SensorCore {
public:
    using ReadFn = bool (*)(void *arg);  // Runs in task context.

    struct Config {
        const char *name {"sc"};
        osal::PeriodicEntry entry {nullptr};
        void *param {nullptr};
        size_t stack_size {2048};
        void *stack_buffer {nullptr};
        osal::Priority priority {5U};
        uint32_t frequency_hz {1000};
        osal::IrqTimer *timer {nullptr};
        ReadFn read_fn {nullptr};
        void *sensor_arg {nullptr};
        uint32_t divider {1};
    };

    SensorCore() = default;
    explicit SensorCore(const Config &cfg);
    // Owners must call stop() before destruction; teardown never waits here.
    ~SensorCore() = default;

    SensorCore(const SensorCore&) = delete;
    SensorCore& operator=(const SensorCore&) = delete;

    // Reconfiguration is accepted only while fully stopped. This lets a
    // product own SensorCore statically without retaining stale ISR targets.
    [[nodiscard]] bool configure(const Config &cfg);
    [[nodiscard]] int start();
    [[nodiscard]] int stop();
    [[nodiscard]] osal::PeriodicThread* thread() const {
        return thread_.load(std::memory_order_acquire);
    }
    [[nodiscard]] uint32_t fire_count() const {
        return fire_count_.load(std::memory_order_relaxed);
    }
    [[nodiscard]] uint32_t read_error_count() const {
        return read_error_count_.load(std::memory_order_relaxed);
    }

    /// Called from the external trigger ISR, including divider handling.
    void on_sensor_done();

private:
    static void timer_callback(void *arg);
    static void thread_entry(
        void *arg, const osal::PeriodicStats& stats);
    Config cfg_;
    osal::PeriodicThread periodic_thread_;
    std::atomic<osal::PeriodicThread *> thread_ {nullptr};
    std::atomic<bool> timer_attached_ {false};
    std::atomic<uint32_t> fire_count_ {0U};
    std::atomic<uint32_t> read_error_count_ {0U};
};

/**
 * Deterministic multi-sensor DMA batch coordinator.
 *
 * A hardware timer starts every registered source in ISR context. DMA
 * completion callbacks seal frames into two caller-owned buffers. The
 * consumer receives a const batch pointer in task context; that pointer is
 * valid only for the duration of the callback.
 * Sources are launched in registration order during one timer ISR. Hardware
 * edge-level simultaneity requires a peripheral trigger rather than software
 * start callbacks.
 */
class SensorBatchCore {
public:
    static constexpr size_t kBufferCount = 2U;
    static constexpr size_t kMaxSources = 4U;
    static_assert(std::atomic<bool>::is_always_lock_free);
    static_assert(std::atomic<uint8_t>::is_always_lock_free);
    static_assert(std::atomic<uint16_t>::is_always_lock_free);
    static_assert(std::atomic<uint32_t>::is_always_lock_free);

    struct FrameHeader {
        uint32_t sequence {0U};
        uint32_t capture_time_us {0U};
        uint32_t valid_sources {0U};
    };

    enum class SourceResult : uint8_t {
        Success = 0U,
        Failure,
    };

    using CompletionFn = void (*)(void* argument, SourceResult result);
    // prepare/stop run in task context. start runs in the timer ISR and must
    // return immediately; it must not invoke completion inline. completion
    // runs in the source DMA ISR after the destination is no longer writable.
    using PrepareFn = bool (*)(void* context, CompletionFn completion,
                               void* completion_argument);
    using StartFn = bool (*)(void* context, void* destination);
    using StopFn = void (*)(void* context);

    struct Source {
        PrepareFn prepare {nullptr};
        StartFn start {nullptr};
        StopFn stop {nullptr};
        void* context {nullptr};
        size_t frame_offset {0U};
        size_t frame_bytes {0U};
    };

    struct BatchView {
        const void* frames {nullptr};
        size_t frame_count {0U};
        size_t frame_size {0U};
        uint32_t first_sequence {0U};
        uint32_t first_capture_time_us {0U};
        uint32_t last_capture_time_us {0U};
    };

    using BatchEntry = void (*)(void* context, const BatchView& batch);

    struct Config {
        const char* name {"sensor_batch"};
        osal::IrqTimer* timer {nullptr};
        BatchEntry entry {nullptr};
        void* entry_context {nullptr};
        std::array<Source, kMaxSources> sources {};
        size_t source_count {0U};
        std::array<void*, kBufferCount> buffers {};
        size_t frame_size {0U};
        size_t buffer_capacity {0U};
        size_t buffer_bytes {0U};
        uint32_t sample_frequency_hz {0U};
        uint32_t consumer_frequency_hz {0U};
        void* stack_buffer {nullptr};
        size_t stack_size {2048U};
        osal::Priority priority {5U};
    };

    struct Diagnostics {
        uint32_t trigger_count {0U};
        uint32_t sample_count {0U};
        uint32_t source_error_count {0U};
        uint32_t trigger_overrun_count {0U};
        uint32_t batch_overrun_count {0U};
        uint32_t dispatch_error_count {0U};
        uint32_t consumer_missed_count {0U};
    };

    SensorBatchCore() = default;
    // Owners must call stop() before destruction; teardown never waits here.
    ~SensorBatchCore() = default;
    SensorBatchCore(const SensorBatchCore&) = delete;
    SensorBatchCore& operator=(const SensorBatchCore&) = delete;

    [[nodiscard]] bool configure(const Config& config);
    [[nodiscard]] int start();
    [[nodiscard]] int stop();
    [[nodiscard]] Diagnostics diagnostics() const;
    [[nodiscard]] osal::StackStats stack_stats() const
    {
        return consumer_thread_.stack_stats();
    }

private:
    enum class BufferState : uint8_t {
        Free = 0U,
        Filling,
        Ready,
        Reading,
    };

    struct BufferSlot {
        std::atomic<BufferState> state {BufferState::Free};
        uint16_t frame_count {0U};
        uint32_t first_sequence {0U};
        uint32_t first_capture_time_us {0U};
        uint32_t last_capture_time_us {0U};
    };

    struct CompletionToken {
        SensorBatchCore* owner {nullptr};
        uint8_t source_index {0U};
    };

    [[nodiscard]] bool valid_config(const Config& config) const;
    [[nodiscard]] bool prepare_sources();
    void stop_sources();
    void reset_runtime_state();
    [[nodiscard]] uint8_t claim_free_buffer();
    [[nodiscard]] FrameHeader* active_frame_header() const;
    void on_timer_trigger();
    void on_source_complete(uint8_t source_index, SourceResult result);
    void finalize_active_frame();
    void dispatch_full_buffer(uint8_t buffer_index);
    void consume_ready_batch();

    static void timer_callback(void* argument);
    static void source_callback(void* argument, SourceResult result);
    static void thread_entry(
        void* argument, const osal::PeriodicStats& stats);

    static constexpr uint8_t kInvalidBuffer = 0xFFU;
    Config config_ {};
    osal::PeriodicThread consumer_thread_ {};
    std::array<BufferSlot, kBufferCount> slots_ {};
    std::array<CompletionToken, kMaxSources> tokens_ {};
    std::atomic<uint8_t> fill_buffer_ {kInvalidBuffer};
    std::atomic<uint8_t> active_buffer_ {kInvalidBuffer};
    std::atomic<uint16_t> active_frame_ {0U};
    std::atomic<uint8_t> ready_mask_ {0U};
    std::atomic<uint8_t> completion_mask_ {0U};
    std::atomic<uint8_t> error_mask_ {0U};
    std::atomic<bool> transfer_active_ {false};
    std::atomic<bool> running_ {false};
    std::atomic<bool> timer_attached_ {false};
    std::atomic<uint32_t> trigger_count_ {0U};
    std::atomic<uint32_t> sample_count_ {0U};
    std::atomic<uint32_t> source_error_count_ {0U};
    std::atomic<uint32_t> trigger_overrun_count_ {0U};
    std::atomic<uint32_t> batch_overrun_count_ {0U};
    std::atomic<uint32_t> dispatch_error_count_ {0U};
    uint32_t frames_per_batch_ {0U};
    uint32_t expected_source_mask_ {0U};
    uint32_t next_capture_time_us_ {0U};
    uint32_t timestamp_step_us_ {0U};
    uint32_t timestamp_remainder_ {0U};
    uint32_t timestamp_phase_ {0U};
    size_t prepared_source_count_ {0U};
};
