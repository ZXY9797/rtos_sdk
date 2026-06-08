#pragma once

#include <osal_types.h>

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
    static void suspend_scheduler();
    static bool resume_scheduler();
};

class Semaphore {
public:
    explicit Semaphore(uint32_t initial = 0U, uint32_t max_count = kSemaphoreMaxCount);
    ~Semaphore();

    Semaphore(const Semaphore&) = delete;
    Semaphore& operator=(const Semaphore&) = delete;

    [[nodiscard]] int take(Milliseconds timeout_ms = kWaitForever);
    [[nodiscard]] int release();
    [[nodiscard]] uint32_t count() const;
    [[nodiscard]] uint32_t max_count() const { return max_count_; }
    [[nodiscard]] bool is_valid() const { return handle_ != nullptr; }
    [[nodiscard]] osal_sem_t handle() const { return handle_; }

private:
    osal_sem_t handle_ {};
    uint32_t max_count_ {};
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
};

class LockGuard {
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

    [[nodiscard]] static Thread* create(const char* name,
                                        Entry entry,
                                        void* param,
                                        size_t stack_size,
                                        int32_t priority,
                                        int32_t time_slice_ticks = 0);

    [[nodiscard]] bool start(Entry entry, void* context, const ThreadConfig& config = {});
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

    [[nodiscard]] bool is_valid() const { return handle_.handle != nullptr; }
    [[nodiscard]] osal_thread_t* handle() { return &handle_; }
    [[nodiscard]] const osal_thread_t* handle() const { return &handle_; }

private:
    struct PrivateTag {};
    explicit Thread(PrivateTag) : handle_{} {}

    osal_thread_t handle_ {};
};

struct PeriodicStats {
    uint32_t sequence {0};
    uint32_t missed {0};
};

using PeriodicEntry = void (*)(void* param, const PeriodicStats& stats);

enum class PeriodicTrigger {
    Tick,
    External,
};

class PeriodicThread {
public:
    ~PeriodicThread();

    PeriodicThread(const PeriodicThread&) = delete;
    PeriodicThread& operator=(const PeriodicThread&) = delete;

    [[nodiscard]] static PeriodicThread* create(const char* name,
                                                PeriodicEntry entry,
                                                void* param,
                                                size_t stack_size,
                                                int32_t priority,
                                                uint32_t frequency_hz,
                                                PeriodicTrigger trigger);

    [[nodiscard]] int startup();
    [[nodiscard]] int stop();
    [[nodiscard]] int notify_from_isr(uint32_t events = 1U);
    [[nodiscard]] uint32_t missed() const { return missed_; }

private:
    struct PrivateTag {};
    explicit PeriodicThread(PrivateTag);
    static void threadEntry(void* parameter);
    static uint32_t nextDelayTicks(uint32_t tick_rate, uint32_t frequency_hz,
                                   uint32_t& phase);
    void callEntry();

    osal_thread_t thread_ {};
    osal_sem_t sem_ {nullptr};
    PeriodicEntry entry_ {nullptr};
    void* param_ {nullptr};
    uint32_t frequency_hz_ {0};
    PeriodicTrigger trigger_ {PeriodicTrigger::Tick};
    volatile uint32_t sequence_ {0};
    volatile uint32_t missed_ {0};
    volatile uint32_t pending_ {0};
    volatile bool running_ {false};
    bool started_ {false};
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

class MessageQueue {
public:
    MessageQueue() = default;
    ~MessageQueue();

    MessageQueue(const MessageQueue&) = delete;
    MessageQueue& operator=(const MessageQueue&) = delete;

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
    [[nodiscard]] bool is_valid() const { return native_handle_ != nullptr; }

private:
    void* native_handle_ {};
    void* control_block_buffer_ {};
    size_t item_size_ {};
    bool owns_control_block_buffer_ {};
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
