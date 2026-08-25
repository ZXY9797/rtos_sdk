#pragma once

#include <osal_types.h>

#include <atomic>
#include <cstddef>
#include <cstdint>

inline constexpr uint32_t OSAL_WAITING_FOREVER = UINT32_MAX;

int osal_init(void);
int osal_start(void (*entry)(void));

namespace osal {

using Milliseconds = uint32_t;
using Priority = uint8_t;

inline constexpr Milliseconds kWaitForever = OSAL_WAITING_FOREVER;
inline constexpr Priority kPriorityMin = 0U;

inline constexpr Priority kDefaultThreadPriority =
    static_cast<Priority>(kPriorityMax / 3U);

struct MemoryStats {
    bool available {false};
    size_t free_bytes {0U};
    size_t minimum_free_bytes {0U};
};

struct StackStats {
    bool available {false};
    size_t total_bytes {0U};
    size_t minimum_free_bytes {0U};
};

struct ThreadConfig {
    const char* name = "thread";
    Priority priority = kDefaultThreadPriority;
    size_t stack_size_bytes = kDefaultThreadStackBytes;
    void* stack_buffer = nullptr;
    uint32_t time_slice_ticks = 20U;
};

class Kernel {
public:
    enum class SchedulerState : uint8_t {
        NotStarted = 0U,
        Running = 1U,
        Suspended = 2U,
    };

    static void start();
    static bool is_running();
    static SchedulerState get_scheduler_state();
    static bool in_isr();
    static uint32_t tick_count();
    static uint32_t uptime_ms();
    [[nodiscard]] static MemoryStats memory_stats();
    static void suspend_scheduler();
    static bool resume_scheduler();
};

class Deadline {
public:
    explicit Deadline(Milliseconds timeout_ms)
        : started_ms_(Kernel::uptime_ms()), timeout_ms_(timeout_ms) {}

    [[nodiscard]] Milliseconds remaining() const
    {
        if (timeout_ms_ == kWaitForever) {
            return kWaitForever;
        }
        const Milliseconds elapsed = Kernel::uptime_ms() - started_ms_;
        return elapsed >= timeout_ms_ ? 0U : timeout_ms_ - elapsed;
    }

    [[nodiscard]] bool expired() const
    {
        return timeout_ms_ != kWaitForever && remaining() == 0U;
    }

private:
    Milliseconds started_ms_;
    Milliseconds timeout_ms_;
};

class IsrContext {
public:
    IsrContext()
    {
        osal_interrupt_enter();
    }

    ~IsrContext()
    {
        osal_interrupt_leave();
        osal_yield_from_isr(reschedule_ ? 1 : 0);
    }

    IsrContext(const IsrContext&) = delete;
    IsrContext& operator=(const IsrContext&) = delete;

    void request_reschedule(bool required = true)
    {
        reschedule_ = reschedule_ || required;
    }

    [[nodiscard]] bool reschedule_requested() const
    {
        return reschedule_;
    }

private:
    bool reschedule_ {false};
};

class Semaphore {
public:
    explicit Semaphore(uint32_t initial = 0U, uint32_t max_count = kSemaphoreMaxCount);
    ~Semaphore();

    Semaphore(const Semaphore&) = delete;
    Semaphore& operator=(const Semaphore&) = delete;

    [[nodiscard]] int take(Milliseconds timeout_ms = kWaitForever);
    [[nodiscard]] int release();
    [[nodiscard]] int release_from_isr(IsrContext& context);
    [[nodiscard]] uint32_t count() const;
    [[nodiscard]] uint32_t max_count() const { return max_count_; }
    [[nodiscard]] bool is_valid() const { return handle_ != nullptr; }
    [[nodiscard]] osal_sem_t handle() const { return handle_; }

private:
    osal_sem_t handle_ {};
    alignas(std::max_align_t)
        std::byte control_storage_[kSemaphoreControlBytes] {};
    uint32_t max_count_ {};
    std::atomic<uint32_t> token_count_ {0U};
    bool is_static_ {false};
};

class Mutex {
public:
    Mutex();
    ~Mutex();

    Mutex(const Mutex&) = delete;
    Mutex& operator=(const Mutex&) = delete;

    [[nodiscard]] bool create();
    void destroy();

    [[nodiscard]] int lock(Milliseconds timeout_ms = kWaitForever);
    [[nodiscard]] int try_lock();
    [[nodiscard]] int unlock();
    [[nodiscard]] bool is_valid() const { return handle_ != nullptr; }
    [[nodiscard]] osal_mutex_t handle() const { return handle_; }

private:
    osal_mutex_t handle_ {};
    alignas(std::max_align_t)
        std::byte control_storage_[kMutexControlBytes] {};
    bool is_static_ {false};
};

class [[nodiscard]] LockGuard {
public:
    explicit LockGuard(Mutex& mutex, Milliseconds timeout_ms = kWaitForever)
        : mutex_(mutex), locked_(mutex_.lock(timeout_ms) == 0) {}

    ~LockGuard()
    {
        if (locked_) {
            (void)mutex_.unlock();
        }
    }

    LockGuard(const LockGuard&) = delete;
    LockGuard& operator=(const LockGuard&) = delete;

    [[nodiscard]] bool owns_lock() const { return locked_; }

private:
    Mutex& mutex_;
    bool locked_ {};
};

class Thread {
public:
    using Entry = void (*)(void*);

    enum class State : uint8_t {
        Running = 0U,
        Ready = 1U,
        Blocked = 2U,
        Suspended = 3U,
        Deleted = 4U,
        Invalid = 255U,
    };

    Thread();
    ~Thread();

    Thread(const Thread&) = delete;
    Thread& operator=(const Thread&) = delete;

    [[nodiscard]] static Thread* create(
        Entry entry, void* context, const ThreadConfig& config = {});

    [[nodiscard]] bool start(Entry entry, void* context, const ThreadConfig& config = {});
    void request_stop();
    [[nodiscard]] bool stop_requested() const {
        return stop_requested_.load(std::memory_order_acquire);
    }
    [[nodiscard]] bool join(Milliseconds timeout_ms = 100U);
    [[nodiscard]] bool shutdown(Milliseconds timeout_ms = 100U);
    void destroy();

    [[nodiscard]] int startup();
    [[nodiscard]] int suspend();
    [[nodiscard]] int resume();
    [[nodiscard]] int yield();

    [[nodiscard]] bool set_priority(Priority priority);
    [[nodiscard]] Priority get_priority() const;
    [[nodiscard]] State get_state() const;
    [[nodiscard]] const char* get_name() const;
    [[nodiscard]] bool abort_delay();
    [[nodiscard]] StackStats stack_stats() const;

    [[nodiscard]] bool is_valid() const { return handle_.handle != nullptr; }
    [[nodiscard]] osal_thread_t* handle() { return &handle_; }
    [[nodiscard]] const osal_thread_t* handle() const { return &handle_; }

private:
    struct PrivateTag {};
    explicit Thread(PrivateTag) : handle_{} {}
    static void threadEntry(void* context);

    osal_thread_t handle_ {};
    Entry entry_ {nullptr};
    void* context_ {nullptr};
    std::atomic<bool> stop_requested_ {false};
    std::atomic<bool> started_ {false};
    std::atomic<bool> exited_ {false};
    size_t stack_size_bytes_ {0U};
};

struct PeriodicStats {
    uint32_t sequence {0};
    uint32_t missed {0};
};

using PeriodicEntry = void (*)(void* param, const PeriodicStats& stats);

class IrqTimer {
public:
    virtual ~IrqTimer() = default;
    using IrqCallback = void (*)(void *arg);
    virtual bool enable_update_irq(IrqCallback cb, void *arg) = 0;
};

enum class PeriodicTrigger : uint8_t {
    Tick,
    External,
};

struct PeriodicThreadConfig {
    const char* name {"periodic"};
    PeriodicEntry entry {nullptr};
    void* context {nullptr};
    size_t stack_size_bytes {kDefaultThreadStackBytes};
    // Optional caller-owned stack. When provided, no task stack/TCB heap
    // allocation is performed and the buffer must outlive the thread.
    void* stack_buffer {nullptr};
    Priority priority {kDefaultThreadPriority};
    uint32_t frequency_hz {0U};
    PeriodicTrigger trigger {PeriodicTrigger::Tick};
    IrqTimer* timer {nullptr};
    bool register_isr_trigger {true};
};

/// ISR 静态触发表 — 通过 ID 在 ISR 中触发回调，无需持有任务 handle。
/// 适用于 app 层创建任务、core 层 ISR 触发的跨层场景。
class IsrTrigger {
public:
    using Callback = void (*)(void *arg);

    /// 注册一个触发槽位（线程上下文调用）
    /// @return 分配的 slot ID，失败返回 -1
    [[nodiscard]] static int register_slot(Callback cb, void *arg);

    /// Release a slot before its callback owner is destroyed.
    static void unregister_slot(int id);

    /// 通过 ID 触发回调（ISR-safe）
    static void fire(int id);

private:
    static constexpr int kMaxSlots = 8;
    struct Slot {
        Callback cb {nullptr};
        void *arg {nullptr};
    };
    static Slot s_slots[kMaxSlots];
};

class PeriodicThread {
public:
    PeriodicThread();
    ~PeriodicThread();

    PeriodicThread(const PeriodicThread&) = delete;
    PeriodicThread& operator=(const PeriodicThread&) = delete;

    [[nodiscard]] static PeriodicThread* create(
        const PeriodicThreadConfig& config);
    [[nodiscard]] bool start(const PeriodicThreadConfig& config);

    [[nodiscard]] int startup();
    [[nodiscard]] int stop();
    [[nodiscard]] bool shutdown(Milliseconds timeout_ms = 100U);
    void destroy();
    [[nodiscard]] int notify_from_isr(uint32_t events = 1U);
    [[nodiscard]] uint32_t missed() const {
        return missed_.load(std::memory_order_relaxed);
    }
    [[nodiscard]] StackStats stack_stats() const;

    /// External 模式下分配的 IsrTrigger slot ID，供 core 层 ISR 通过 fire(id) 触发。
    /// Tick 模式返回 -1。
    [[nodiscard]] int trigger_id() const { return trigger_id_; }

private:
    static void threadEntry(void* parameter);
    static uint32_t nextDelayTicks(uint32_t tick_rate, uint32_t frequency_hz,
                                   uint32_t& phase);
    void callEntry();
    static void timer_isr_callback(void *arg);

    osal_thread_t thread_ {};
    osal_sem_t sem_ {nullptr};
    alignas(std::max_align_t)
        std::byte sem_storage_[kSemaphoreControlBytes] {};
    bool sem_is_static_ {false};
    PeriodicEntry entry_ {nullptr};
    void* param_ {nullptr};
    IrqTimer *timer_ {nullptr};
    bool timer_attached_ {false};
    uint32_t frequency_hz_ {0};
    int trigger_id_ {-1};
    PeriodicTrigger trigger_ {PeriodicTrigger::Tick};
    std::atomic<uint32_t> sequence_ {0U};
    std::atomic<uint32_t> missed_ {0U};
    std::atomic<uint32_t> pending_ {0U};
    std::atomic<bool> running_ {false};
    std::atomic<bool> terminate_ {false};
    std::atomic<bool> exited_ {false};
    bool started_ {false};
    size_t stack_size_bytes_ {0U};
};

class EventGroup {
public:
    enum class WaitMode : uint8_t {
        Any = 0U,
        All = 1U,
    };

    EventGroup() = default;
    ~EventGroup();

    EventGroup(const EventGroup&) = delete;
    EventGroup& operator=(const EventGroup&) = delete;

    [[nodiscard]] bool create();
    void destroy();

    [[nodiscard]] uint32_t set_bits(uint32_t bits);
    [[nodiscard]] uint32_t clear_bits(uint32_t bits);
    [[nodiscard]] uint32_t get_bits() const;
    [[nodiscard]] uint32_t wait_bits(uint32_t bits_to_wait_for,
                                     bool clear_on_exit,
                                     WaitMode wait_mode,
                                     Milliseconds timeout_ms = kWaitForever);
    [[nodiscard]] uint32_t sync(uint32_t bits_to_set,
                                uint32_t bits_to_wait_for,
                                Milliseconds timeout_ms = kWaitForever);
    [[nodiscard]] bool is_valid() const { return native_handle_ != nullptr; }

private:
    void* native_handle_ {};
};

struct MessageQueueStats {
    uint32_t capacity {0U};
    uint32_t current_depth {0U};
    uint32_t high_water_mark {0U};
    uint32_t send_failures {0U};
};

class MessageQueue {
public:
    MessageQueue() = default;
    ~MessageQueue();

    MessageQueue(const MessageQueue&) = delete;
    MessageQueue& operator=(const MessageQueue&) = delete;

    template <typename Item, size_t Length>
    [[nodiscard]] static constexpr size_t storage_size()
    {
        static_assert(kMessageQueueItemAlignment > 0U);
        static_assert(
            (kMessageQueueItemAlignment
             & (kMessageQueueItemAlignment - 1U)) == 0U);
        static_assert(
            sizeof(Item) <= SIZE_MAX - (kMessageQueueItemAlignment - 1U));
        constexpr size_t stride =
            (sizeof(Item) + kMessageQueueItemAlignment - 1U)
            & ~(kMessageQueueItemAlignment - 1U);
        static_assert(stride <= SIZE_MAX - kMessageQueueItemOverhead);
        constexpr size_t slot_size =
            stride + kMessageQueueItemOverhead;
        static_assert(slot_size > 0U && Length <= SIZE_MAX / slot_size);
        return Length * slot_size;
    }

    [[nodiscard]] bool create(uint32_t length,
                              size_t item_size,
                              void* storage_buffer = nullptr,
                              size_t storage_buffer_size_bytes = 0U);
    void destroy();

    [[nodiscard]] bool send(const void* item, Milliseconds timeout_ms = kWaitForever);
    [[nodiscard]] bool receive(void* item, Milliseconds timeout_ms = kWaitForever);
    [[nodiscard]] bool reset();
    [[nodiscard]] uint32_t count() const;
    [[nodiscard]] uint32_t free_slots() const;
    [[nodiscard]] MessageQueueStats stats() const;
    void reset_stats();
    [[nodiscard]] bool is_valid() const { return native_handle_ != nullptr; }

private:
    void record_send(bool succeeded);
    void* native_handle_ {};
    void* control_block_buffer_ {};
    alignas(std::max_align_t)
        std::byte control_storage_[kMessageQueueControlBytes] {};
    size_t item_size_ {};
    uint32_t length_ {};
    std::atomic<uint32_t> high_water_mark_ {0U};
    std::atomic<uint32_t> send_failures_ {0U};
    bool owns_control_block_buffer_ {};
};

class StreamBuffer {
public:
    StreamBuffer() = default;
    ~StreamBuffer();

    /// Creates a dynamically allocated byte stream.
    [[nodiscard]] bool create(size_t buf_size,
                              size_t trigger_level = 1);
    /// Uses caller-owned storage; no hidden data/control allocation occurs.
    /// One byte is reserved to distinguish full from empty, so the usable
    /// capacity is storage_size - 1.
    [[nodiscard]] bool create(uint8_t *storage, size_t storage_size,
                              size_t trigger_level = 1);
    void destroy();

    StreamBuffer(const StreamBuffer&) = delete;
    StreamBuffer& operator=(const StreamBuffer&) = delete;

    /// Sends bytes, waiting for space until the timeout expires.
    ///
    /// As with native FreeRTOS stream buffers, each instance supports one
    /// producer and one consumer. Multiple producers or consumers require
    /// external serialization.
    [[nodiscard]] size_t send(const uint8_t *data, size_t len,
                              Milliseconds timeout_ms = kWaitForever);
    /// ISR-safe send for the instance's single producer.
    [[nodiscard]] size_t send_from_isr(const uint8_t *data, size_t len,
                                       IsrContext& context);

    /// Receives bytes. A receiver that blocks on an empty stream is woken
    /// after trigger_level bytes become available, or on timeout.
    [[nodiscard]] size_t receive(uint8_t *data, size_t len,
                                 Milliseconds timeout_ms = kWaitForever);
    /// ISR-safe receive for the instance's single consumer.
    [[nodiscard]] size_t receive_from_isr(uint8_t *data, size_t len,
                                          IsrContext& context);

    [[nodiscard]] size_t bytes_available() const;
    [[nodiscard]] size_t space_available() const;
    void reset();
    [[nodiscard]] bool is_valid() const { return handle_ != nullptr; }

private:
    void *handle_ {};
    void *control_block_ {};
    alignas(std::max_align_t)
        std::byte control_storage_[kStreamBufferControlBytes] {};
    bool owns_storage_ {};
};

class SoftTimer {
public:
    using Callback = void (*)(void* context);

    SoftTimer() = default;
    ~SoftTimer();

    SoftTimer(const SoftTimer&) = delete;
    SoftTimer& operator=(const SoftTimer&) = delete;

    [[nodiscard]] bool create(const char* name,
                              Milliseconds period_ms,
                              bool auto_reload,
                              Callback callback,
                              void* context = nullptr);
    void destroy();

    [[nodiscard]] bool start();
    [[nodiscard]] bool stop();
    [[nodiscard]] bool reset();
    [[nodiscard]] bool change_period(Milliseconds period_ms);
    [[nodiscard]] bool is_active() const;
    [[nodiscard]] bool is_valid() const { return native_handle_ != nullptr; }

private:
    static void dispatch(void* timer);

    void* native_handle_ {};
    Callback callback_ {};
    void* context_ {};
};

class CriticalSectionGuard {
public:
    CriticalSectionGuard();
    ~CriticalSectionGuard();

    CriticalSectionGuard(const CriticalSectionGuard&) = delete;
    CriticalSectionGuard& operator=(const CriticalSectionGuard&) = delete;

private:
    uintptr_t saved_state_ {};
};

class IsrCriticalSectionGuard {
public:
    IsrCriticalSectionGuard();
    ~IsrCriticalSectionGuard();

    IsrCriticalSectionGuard(const IsrCriticalSectionGuard&) = delete;
    IsrCriticalSectionGuard& operator=(const IsrCriticalSectionGuard&) = delete;

private:
    uintptr_t saved_state_ {};
};

namespace this_thread {

void sleep_for(Milliseconds timeout_ms);
bool sleep_until(uint32_t* previous_wake_tick, Milliseconds period_ms);
void yield();
void suspend();

} // namespace this_thread

} // namespace osal
