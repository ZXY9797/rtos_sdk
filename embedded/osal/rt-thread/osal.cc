#include "rtdef.h"
#include "sys/util.h"

#include <mem.h>
#include <osal.h>
#include <arch/arm/cortex_m/fault.h>
#include <soc.h>
#include <rthw.h>
#include <rtthread.h>

#include <cstddef>
#include <cstdint>

static_assert(CONFIG_NUM_IRQ_PRIO_BITS == __NVIC_PRIO_BITS,
              "Kconfig/CMSIS NVIC priority-bit mismatch");
#include <cstring>

ALIGN(8)
static rt_uint8_t main_stack[RT_MAIN_THREAD_STACK_SIZE];
static struct rt_thread main_thread;
static void (*g_user_entry)(void);

static constexpr rt_uint8_t native_priority(osal::Priority priority)
{
    return static_cast<rt_uint8_t>(
        (RT_THREAD_PRIORITY_MAX - 1U) - priority);
}

static void main_thread_wrapper(void* parameter)
{
    (void)parameter;
    g_user_entry();
}

#ifdef RT_USING_HEAP
static rt_uint8_t rt_heap_pool[CONFIG_RTTHREAD_HEAP_SIZE];
#endif

int osal_init()
{
    rt_hw_interrupt_disable();

#ifdef RT_USING_HEAP
    rt_system_heap_init(rt_heap_pool, rt_heap_pool + sizeof(rt_heap_pool));
#endif

    rt_system_timer_init();
    rt_system_scheduler_init();

#ifdef RT_USING_SIGNALS
    rt_system_signal_init();
#endif

    rt_system_timer_thread_init();
    rt_thread_idle_init();

#ifdef RT_USING_SMP
    rt_hw_spin_lock(&_cpus_lock);
#endif

    return 0;
}

extern "C" int cpu_usage_init(void);

int osal_start(void (*entry)(void))
{
    if (entry == nullptr) {
        return -1;
    }
    g_user_entry = entry;

    rt_thread_t tid = &main_thread;
    const rt_err_t init_result = rt_thread_init(
        tid, "main", main_thread_wrapper, nullptr,
        main_stack, sizeof(main_stack),
        native_priority(osal::kDefaultThreadPriority), 20U);
    if (init_result != RT_EOK) {
        return -1;
    }
    if (rt_thread_startup(tid) != RT_EOK) {
        (void)rt_thread_detach(tid);
        return -1;
    }
    if (cpu_usage_init() != 0) {
        (void)rt_thread_detach(tid);
        return -1;
    }
    rt_system_scheduler_start();
    return -1;
}

namespace {

constexpr uint32_t kThreadFlagDynamic = 1U << 0U;
constexpr uint32_t kThreadFlagStatic = 1U << 1U;

rt_int32_t timeout_ms_to_ticks(uint32_t timeout_ms)
{
    if (timeout_ms == OSAL_WAITING_FOREVER) {
        return RT_WAITING_FOREVER;
    }
    if (timeout_ms == 0U) {
        return 0;
    }
    if (timeout_ms > 0x7fffffffU) {
        timeout_ms = 0x7fffffffU;
    }

    return static_cast<rt_int32_t>(
        rt_tick_from_millisecond(static_cast<rt_int32_t>(timeout_ms)));
}

rt_tick_t period_ms_to_ticks(uint32_t period_ms)
{
    if (period_ms == 0U) {
        return 1;
    }
    const uint32_t bounded_period = period_ms > INT32_MAX
        ? static_cast<uint32_t>(INT32_MAX) : period_ms;
    rt_tick_t ticks = rt_tick_from_millisecond(
        static_cast<rt_int32_t>(bounded_period));
    return ticks == 0 ? 1 : ticks;
}

osal::Thread::State map_thread_state(rt_uint8_t state)
{
    switch (state & RT_THREAD_STAT_MASK) {
    case RT_THREAD_RUNNING:
        return osal::Thread::State::Running;
    case RT_THREAD_READY:
        return osal::Thread::State::Ready;
    case RT_THREAD_SUSPEND:
        return osal::Thread::State::Suspended;
    case RT_THREAD_CLOSE:
        return osal::Thread::State::Deleted;
    case RT_THREAD_INIT:
        return osal::Thread::State::Suspended;
    default:
        return osal::Thread::State::Invalid;
    }
}

} // namespace

namespace osal {

void Kernel::start()
{
    rt_system_scheduler_start();
}

bool Kernel::is_running()
{
    return rt_thread_self() != nullptr;
}

Kernel::SchedulerState Kernel::get_scheduler_state()
{
    return is_running() ? SchedulerState::Running : SchedulerState::NotStarted;
}

bool Kernel::in_isr()
{
    return rt_interrupt_get_nest() != 0U;
}

uint32_t Kernel::tick_count()
{
    return rt_tick_get();
}

uint32_t Kernel::uptime_ms()
{
    return rt_tick_get_millisecond();
}

MemoryStats Kernel::memory_stats()
{
    rt_size_t total = 0U;
    rt_size_t used = 0U;
    rt_size_t maximum_used = 0U;
    rt_memory_info(&total, &used, &maximum_used);
    if (used > total || maximum_used > total) {
        return {};
    }
    return {true, static_cast<size_t>(total - used),
            static_cast<size_t>(total - maximum_used)};
}

void Kernel::suspend_scheduler()
{
    rt_enter_critical();
}

bool Kernel::resume_scheduler()
{
    rt_exit_critical();
    return true;
}

Semaphore::Semaphore(uint32_t initial, uint32_t max_count)
{
    if (max_count > RT_SEM_VALUE_MAX) {
        max_count = RT_SEM_VALUE_MAX;
    }
    if (max_count == 0U || initial > max_count) {
        return;
    }

    max_count_ = max_count;
    // The native semaphore is a wake event. token_count_ is authoritative so
    // the caller-selected maximum remains exact across task/ISR races.
    auto* const storage = reinterpret_cast<struct rt_semaphore*>(
        control_storage_);
    if (rt_sem_init(storage, "sem", 0U, RT_IPC_FLAG_FIFO) != RT_EOK) {
        max_count_ = 0U;
    } else {
        handle_ = storage;
        is_static_ = true;
        token_count_.store(initial, std::memory_order_relaxed);
    }
}

Semaphore::~Semaphore()
{
    if (handle_ != nullptr) {
        if (is_static_) {
            (void)rt_sem_detach(handle_);
        } else {
            (void)rt_sem_delete(handle_);
        }
        handle_ = nullptr;
        is_static_ = false;
    }
}

int Semaphore::take(uint32_t timeout_ms)
{
    if (handle_ == nullptr) {
        return -1;
    }
    Deadline deadline(timeout_ms);
    for (;;) {
        uint32_t count = token_count_.load(std::memory_order_acquire);
        while (count > 0U) {
            if (token_count_.compare_exchange_weak(
                    count, count - 1U, std::memory_order_acq_rel)) {
                (void)rt_sem_take(handle_, 0);
                return 0;
            }
        }
        if (deadline.expired()) {
            return -1;
        }
        const rt_err_t wait_result = rt_sem_take(
            handle_, timeout_ms_to_ticks(deadline.remaining()));
        if (wait_result != RT_EOK && deadline.expired()) {
            return -1;
        }
    }
}

int Semaphore::release()
{
    if (handle_ == nullptr) {
        return -1;
    }

    uint32_t count = token_count_.load(std::memory_order_acquire);
    do {
        if (count >= max_count_) {
            return -1;
        }
    } while (!token_count_.compare_exchange_weak(
        count, count + 1U, std::memory_order_acq_rel));

    const rt_err_t release_result = rt_sem_release(handle_);
    if (release_result == RT_EOK || release_result == -RT_EFULL) {
        // A full native semaphore already contains enough wake events. The
        // logical token remains valid and must not be rolled back.
        return 0;
    }

    count = token_count_.load(std::memory_order_acquire);
    while (count > 0U) {
        if (token_count_.compare_exchange_weak(
                count, count - 1U, std::memory_order_acq_rel)) {
            return -1;
        }
    }
    // A concurrent taker consumed the logical token, so no rollback remains.
    return 0;
}

int Semaphore::release_from_isr(IsrContext& context)
{
    (void)context;
    return release();
}

uint32_t Semaphore::count() const
{
    if (handle_ == nullptr) {
        return 0U;
    }

    return token_count_.load(std::memory_order_acquire);
}

Mutex::Mutex()
{
    (void)create();
}

Mutex::~Mutex()
{
    destroy();
}

bool Mutex::create()
{
    if (handle_ != nullptr) {
        return true;
    }
    auto* const storage = reinterpret_cast<struct rt_mutex*>(
        control_storage_);
    if (rt_mutex_init(storage, "mtx", RT_IPC_FLAG_FIFO) != RT_EOK) {
        return false;
    }
    handle_ = storage;
    is_static_ = true;
    return true;
}

void Mutex::destroy()
{
    if (handle_ != nullptr) {
        if (is_static_) {
            (void)rt_mutex_detach(handle_);
        } else {
            (void)rt_mutex_delete(handle_);
        }
        handle_ = nullptr;
        is_static_ = false;
    }
}

int Mutex::lock(uint32_t timeout_ms)
{
    if (handle_ == nullptr) {
        return -1;
    }
    return rt_mutex_take(handle_, timeout_ms_to_ticks(timeout_ms)) == RT_EOK ? 0 : -1;
}

int Mutex::try_lock()
{
    if (handle_ == nullptr) {
        return -1;
    }
    return rt_mutex_take(handle_, 0) == RT_EOK ? 0 : -1;
}

int Mutex::unlock()
{
    if (handle_ == nullptr) {
        return -1;
    }
    return rt_mutex_release(handle_) == RT_EOK ? 0 : -1;
}

Thread::Thread() = default;

Thread::~Thread()
{
    if (!shutdown(0U)) {
        hal::fault::panic(hal::fault::FatalReason::ThreadShutdownTimeout,
                          0, "thread_destructor", 0U);
    }
}

void Thread::threadEntry(void* context)
{
    auto* thread = static_cast<Thread*>(context);
    while (!thread->started_.load(std::memory_order_acquire)
           && !thread->stop_requested()) {
        (void)rt_thread_suspend(rt_thread_self());
        rt_schedule();
    }
    if (!thread->stop_requested()) {
        thread->entry_(thread->context_);
    }
    thread->exited_.store(true, std::memory_order_release);
    for (;;) {
        (void)rt_thread_suspend(rt_thread_self());
        rt_schedule();
    }
}

Thread* Thread::create(Entry entry, void* context,
                       const ThreadConfig& config)
{
    auto* thread = new (std::nothrow) Thread(PrivateTag {});
    if (thread == nullptr) {
        return nullptr;
    }

    if (!thread->start(entry, context, config)) {
        delete thread;
        return nullptr;
    }

    return thread;
}

bool Thread::start(Entry entry, void* context, const ThreadConfig& config)
{
    if (entry == nullptr || handle_.handle != nullptr
        || config.priority >= RT_THREAD_PRIORITY_MAX
        || config.stack_size_bytes < RT_ALIGN_SIZE
        || (config.stack_size_bytes % RT_ALIGN_SIZE) != 0U) {
        return false;
    }

    const char* name = config.name != nullptr ? config.name : "thread";
    const rt_uint8_t prio = native_priority(config.priority);
    const rt_uint32_t tick = config.time_slice_ticks == 0U ? 20U : config.time_slice_ticks;

    entry_ = entry;
    context_ = context;
    stop_requested_.store(false, std::memory_order_relaxed);
    started_.store(false, std::memory_order_relaxed);
    exited_.store(false, std::memory_order_relaxed);

    if (config.stack_buffer != nullptr) {
        if ((reinterpret_cast<uintptr_t>(config.stack_buffer)
             % RT_ALIGN_SIZE) != 0U) {
            return false;
        }
        rt_err_t ret = rt_thread_init(&handle_.tcb, name, threadEntry, this,
                                      config.stack_buffer,
                                      static_cast<rt_uint32_t>(config.stack_size_bytes),
                                      prio, tick);
        if (ret != RT_EOK) {
            return false;
        }
        handle_.handle = &handle_.tcb;
        handle_.flags = kThreadFlagStatic;
        stack_size_bytes_ = config.stack_size_bytes;
        return true;
    }

    rt_thread_t thread = rt_thread_create(name, threadEntry, this,
                                          static_cast<rt_uint32_t>(config.stack_size_bytes),
                                          prio, tick);
    if (thread == nullptr) {
        return false;
    }
    handle_.handle = thread;

    handle_.flags = kThreadFlagDynamic;
    stack_size_bytes_ = config.stack_size_bytes;
    return true;
}

void Thread::destroy()
{
    if (!shutdown()) {
        hal::fault::panic(hal::fault::FatalReason::ThreadShutdownTimeout,
                          0, get_name(), 0U);
    }
}

void Thread::request_stop()
{
    stop_requested_.store(true, std::memory_order_release);
    if (handle_.handle != nullptr) {
        (void)rt_thread_resume(handle_.handle);
    }
}

bool Thread::join(Milliseconds timeout_ms)
{
    if (handle_.handle == nullptr || exited_.load(std::memory_order_acquire)) {
        return true;
    }
    Deadline deadline(timeout_ms);
    while (!exited_.load(std::memory_order_acquire)) {
        if (deadline.expired()) {
            return false;
        }
        (void)rt_thread_mdelay(1U);
    }
    return true;
}

bool Thread::shutdown(Milliseconds timeout_ms)
{
    if (handle_.handle == nullptr) {
        return true;
    }
    if (rt_thread_self() == handle_.handle) {
        return false;
    }
    if (started_.load(std::memory_order_acquire)) {
        request_stop();
        if (!join(timeout_ms)) {
            return false;
        }
    }
    const rt_err_t release_result =
        ((handle_.flags & kThreadFlagDynamic) != 0U)
        ? rt_thread_delete(handle_.handle)
        : rt_thread_detach(handle_.handle);
    if (release_result != RT_EOK) {
        return false;
    }
    handle_.handle = nullptr;
    handle_.flags = 0U;
    entry_ = nullptr;
    context_ = nullptr;
    stack_size_bytes_ = 0U;
    return true;
}

int Thread::startup()
{
    if (handle_.handle == nullptr
        || exited_.load(std::memory_order_acquire)) {
        return -1;
    }

    const rt_uint8_t stat = handle_.handle->stat & RT_THREAD_STAT_MASK;
    if (stat == RT_THREAD_INIT) {
        started_.store(true, std::memory_order_release);
        const bool started = rt_thread_startup(handle_.handle) == RT_EOK;
        if (!started) {
            started_.store(false, std::memory_order_release);
        }
        return started ? 0 : -1;
    }
    if (stat == RT_THREAD_SUSPEND) {
        started_.store(true, std::memory_order_release);
        const bool resumed = rt_thread_resume(handle_.handle) == RT_EOK;
        if (!resumed) {
            started_.store(false, std::memory_order_release);
        }
        return resumed ? 0 : -1;
    }
    return 0;
}

int Thread::suspend()
{
    if (handle_.handle == nullptr) {
        return -1;
    }
    return rt_thread_suspend(handle_.handle) == RT_EOK ? 0 : -1;
}

int Thread::resume()
{
    if (handle_.handle == nullptr) {
        return -1;
    }
    return rt_thread_resume(handle_.handle) == RT_EOK ? 0 : -1;
}

int Thread::yield()
{
    return rt_thread_yield() == RT_EOK ? 0 : -1;
}

bool Thread::set_priority(Priority priority)
{
    if (handle_.handle == nullptr || priority >= RT_THREAD_PRIORITY_MAX) {
        return false;
    }

    rt_uint8_t native = native_priority(priority);
    return rt_thread_control(handle_.handle, RT_THREAD_CTRL_CHANGE_PRIORITY,
                             &native) == RT_EOK;
}

Priority Thread::get_priority() const
{
    return handle_.handle != nullptr
        ? static_cast<Priority>(
            kPriorityMax - handle_.handle->current_priority)
        : kPriorityMin;
}

Thread::State Thread::get_state() const
{
    return handle_.handle != nullptr ? map_thread_state(handle_.handle->stat)
                                     : State::Invalid;
}

const char* Thread::get_name() const
{
    return handle_.handle != nullptr ? handle_.handle->name : nullptr;
}

bool Thread::abort_delay()
{
    if (handle_.handle == nullptr) {
        return false;
    }
    return rt_thread_resume(handle_.handle) == RT_EOK;
}

StackStats Thread::stack_stats() const
{
    if (handle_.handle == nullptr || handle_.handle->stack_addr == nullptr) {
        return {};
    }
    const auto* stack = static_cast<const uint8_t*>(
        handle_.handle->stack_addr);
    const size_t total = static_cast<size_t>(handle_.handle->stack_size);
    size_t free_bytes = 0U;
    while (free_bytes < total && stack[free_bytes] == '#') {
        ++free_bytes;
    }
    return {true, stack_size_bytes_,
            free_bytes < stack_size_bytes_ ? free_bytes : stack_size_bytes_};
}

// ─── IsrTrigger ──────────────────────────────────────────────────

IsrTrigger::Slot IsrTrigger::s_slots[kMaxSlots];

int IsrTrigger::register_slot(Callback cb, void *arg)
{
    if (cb == nullptr) {
        return -1;
    }
    rt_base_t level = rt_hw_interrupt_disable();
    for (int id = 0; id < kMaxSlots; ++id) {
        if (s_slots[id].cb == nullptr) {
            s_slots[id] = {cb, arg};
            rt_hw_interrupt_enable(level);
            return id;
        }
    }
    rt_hw_interrupt_enable(level);
    return -1;
}

void IsrTrigger::unregister_slot(int id)
{
    if (id < 0 || id >= kMaxSlots) {
        return;
    }
    rt_base_t level = rt_hw_interrupt_disable();
    s_slots[id] = {};
    rt_hw_interrupt_enable(level);
}

void IsrTrigger::fire(int id)
{
    if (id >= 0 && id < kMaxSlots && s_slots[id].cb != nullptr) {
        s_slots[id].cb(s_slots[id].arg);
    }
}

// ─── PeriodicThread ──────────────────────────────────────────────

uint32_t PeriodicThread::nextDelayTicks(uint32_t tick_rate, uint32_t frequency_hz,
                                        uint32_t& phase)
{
    phase += tick_rate;
    uint32_t ticks = phase / frequency_hz;
    phase %= frequency_hz;
    return ticks != 0U ? ticks : 1U;
}

void PeriodicThread::callEntry()
{
    const uint32_t sequence = sequence_ + 1U;
    sequence_ = sequence;
    PeriodicStats stats {
        sequence,
        missed_,
    };
    entry_(param_, stats);
}

void PeriodicThread::timer_isr_callback(void *arg)
{
    (void)static_cast<PeriodicThread *>(arg)->notify_from_isr();
}

static void periodic_wait_stopped()
{
    rt_thread_suspend(rt_thread_self());
    rt_schedule();
}

void PeriodicThread::threadEntry(void* parameter)
{
    auto* thread = static_cast<PeriodicThread*>(parameter);

    while (!thread->terminate_.load(std::memory_order_acquire)) {
        while (!thread->running_
               && !thread->terminate_.load(std::memory_order_acquire)) {
            periodic_wait_stopped();
        }

        if (thread->terminate_.load(std::memory_order_acquire)) {
            break;
        }

        if (thread->trigger_ == PeriodicTrigger::Tick) {
            uint32_t phase = 0U;
            rt_tick_t last_tick = rt_tick_get();

            while (thread->running_) {
                const uint32_t delay_ticks =
                    nextDelayTicks(RT_TICK_PER_SECOND, thread->frequency_hz_, phase);

                const rt_tick_t now = rt_tick_get();
                if (now - last_tick >= delay_ticks) {
                    thread->missed_ = thread->missed_ + 1U;
                }

                (void)rt_thread_delay_until(&last_tick, delay_ticks);
                if (!thread->running_) {
                    break;
                }

                thread->callEntry();
            }
        } else {
            while (thread->running_) {
                if (rt_sem_take(thread->sem_, RT_WAITING_FOREVER) != RT_EOK) {
                    continue;
                }
                if (!thread->running_) {
                    break;
                }

                rt_base_t level = rt_hw_interrupt_disable();
                thread->pending_ = 0U;
                rt_hw_interrupt_enable(level);

                thread->callEntry();
            }
        }
    }
    thread->exited_.store(true, std::memory_order_release);
    for (;;) {
        periodic_wait_stopped();
    }
}

PeriodicThread::PeriodicThread()
{
}

PeriodicThread::~PeriodicThread()
{
    if (!shutdown(0U)) {
        hal::fault::panic(hal::fault::FatalReason::ThreadShutdownTimeout,
                          0, "periodic_destructor", 0U);
    }
}

bool PeriodicThread::shutdown(Milliseconds timeout_ms)
{
    (void)stop();
    if (timer_attached_) {
        const rt_base_t level = rt_hw_interrupt_disable();
        const bool detached = timer_->enable_update_irq(nullptr, nullptr);
        if (detached) {
            timer_attached_ = false;
        }
        rt_hw_interrupt_enable(level);
        if (!detached) {
            return false;
        }
    }
    IsrTrigger::unregister_slot(trigger_id_);
    trigger_id_ = -1;
    if (thread_.handle != nullptr) {
        if (rt_thread_self() == thread_.handle) {
            hal::fault::panic(hal::fault::FatalReason::ThreadShutdownTimeout,
                              0, "periodic_self_delete", 0U);
        }
        if (started_) {
            terminate_.store(true, std::memory_order_release);
            running_.store(false, std::memory_order_release);
            if (sem_ != nullptr) {
                (void)rt_sem_release(sem_);
            }
            (void)rt_thread_resume(thread_.handle);
            Deadline deadline(timeout_ms);
            while (!exited_.load(std::memory_order_acquire)) {
                if (deadline.expired()) {
                    return false;
                }
                (void)rt_thread_mdelay(1U);
            }
        }
        const rt_err_t release_result =
            ((thread_.flags & kThreadFlagDynamic) != 0U)
            ? rt_thread_delete(thread_.handle)
            : rt_thread_detach(thread_.handle);
        if (release_result != RT_EOK) {
            return false;
        }
        thread_.handle = nullptr;
        thread_.flags = 0U;
        started_ = false;
        stack_size_bytes_ = 0U;
    }
    if (sem_ != nullptr) {
        const rt_err_t release_result = sem_is_static_
            ? rt_sem_detach(sem_)
            : rt_sem_delete(sem_);
        if (release_result != RT_EOK) {
            return false;
        }
        sem_ = nullptr;
        sem_is_static_ = false;
    }
    return true;
}

void PeriodicThread::destroy()
{
    if (!shutdown()) {
        hal::fault::panic(hal::fault::FatalReason::ThreadShutdownTimeout,
                          0, "periodic_destroy", 0U);
    }
}

PeriodicThread* PeriodicThread::create(
    const PeriodicThreadConfig& config)
{
    auto* thread = new (std::nothrow) PeriodicThread();
    if (thread == nullptr) {
        return nullptr;
    }
    if (!thread->start(config)) {
        delete thread;
        return nullptr;
    }
    return thread;
}

bool PeriodicThread::start(const PeriodicThreadConfig& config)
{
    if (config.entry == nullptr || config.frequency_hz == 0U
        || config.stack_size_bytes < RT_ALIGN_SIZE
        || (config.stack_size_bytes % RT_ALIGN_SIZE) != 0U
        || config.priority >= RT_THREAD_PRIORITY_MAX
        || thread_.handle != nullptr || sem_ != nullptr
        || (config.trigger != PeriodicTrigger::Tick
            && config.trigger != PeriodicTrigger::External)
        || (config.trigger == PeriodicTrigger::Tick
            && config.timer != nullptr)
        || (config.stack_buffer != nullptr
            && (reinterpret_cast<uintptr_t>(config.stack_buffer)
                % RT_ALIGN_SIZE) != 0U)) {
        return false;
    }
    if (config.trigger == PeriodicTrigger::Tick
        && config.frequency_hz > RT_TICK_PER_SECOND) {
        return false;
    }

    entry_ = config.entry;
    param_ = config.context;
    frequency_hz_ = config.frequency_hz;
    trigger_ = config.trigger;
    sequence_.store(0U, std::memory_order_relaxed);
    missed_.store(0U, std::memory_order_relaxed);
    pending_.store(0U, std::memory_order_relaxed);
    running_.store(false, std::memory_order_relaxed);
    terminate_.store(false, std::memory_order_relaxed);
    exited_.store(false, std::memory_order_relaxed);
    started_ = false;

    // External 模式：注册到 IsrTrigger 静态触发表
    if (config.trigger == PeriodicTrigger::External
        && config.timer == nullptr && config.register_isr_trigger) {
        trigger_id_ = IsrTrigger::register_slot(timer_isr_callback, this);
        if (trigger_id_ < 0) {
            return false;
        }
    }

    // 如果传入了硬件定时器，自动连接 ISR 回调
    if (config.timer != nullptr) {
        timer_ = config.timer;
        if (!config.timer->enable_update_irq(timer_isr_callback, this)) {
            IsrTrigger::unregister_slot(trigger_id_);
            trigger_id_ = -1;
            timer_ = nullptr;
            return false;
        }
        timer_attached_ = true;
    }

    if (config.trigger == PeriodicTrigger::External) {
        auto* const storage = reinterpret_cast<struct rt_semaphore*>(
            sem_storage_);
        if (rt_sem_init(storage, "per", 0U, RT_IPC_FLAG_FIFO) != RT_EOK) {
            (void)shutdown(0U);
            return false;
        }
        sem_ = storage;
        sem_is_static_ = true;
    }

    const char* name = config.name != nullptr ? config.name : "periodic";
    if (config.stack_buffer != nullptr) {
        const rt_err_t result = rt_thread_init(
            &thread_.tcb, name, PeriodicThread::threadEntry, this,
            config.stack_buffer,
            static_cast<rt_uint32_t>(config.stack_size_bytes),
            native_priority(config.priority), 20U);
        if (result == RT_EOK) {
            thread_.handle = &thread_.tcb;
            thread_.flags = kThreadFlagStatic;
        }
    } else {
        thread_.handle = rt_thread_create(
            name, PeriodicThread::threadEntry, this,
            static_cast<rt_uint32_t>(config.stack_size_bytes),
            native_priority(config.priority), 20U);
        if (thread_.handle != nullptr) {
            thread_.flags = kThreadFlagDynamic;
        }
    }
    if (thread_.handle == nullptr) {
        (void)shutdown(0U);
        return false;
    }
    stack_size_bytes_ = config.stack_size_bytes;
    return true;
}

int PeriodicThread::startup()
{
    if (thread_.handle == nullptr
        || terminate_.load(std::memory_order_acquire)
        || exited_.load(std::memory_order_acquire)) {
        return -1;
    }

    running_ = true;
    if (!started_) {
        const bool started = rt_thread_startup(thread_.handle) == RT_EOK;
        started_ = started;
        return started ? 0 : -1;
    }

    return rt_thread_resume(thread_.handle) == RT_EOK ? 0 : -1;
}

int PeriodicThread::stop()
{
    running_ = false;
    if (trigger_ == PeriodicTrigger::External && sem_ != nullptr) {
        (void)rt_sem_release(sem_);
    }
    return 0;
}

int PeriodicThread::notify_from_isr(uint32_t events)
{
    if (trigger_ != PeriodicTrigger::External || sem_ == nullptr || !running_ ||
        events == 0U) {
        return -1;
    }

    bool release = false;
    rt_base_t level = rt_hw_interrupt_disable();
    if (pending_ == 0U) {
        pending_ = 1U;
        missed_ = missed_ + events - 1U;
        release = true;
    } else {
        missed_ = missed_ + events;
    }
    rt_hw_interrupt_enable(level);

    if (release && rt_sem_release(sem_) != RT_EOK) {
        return -1;
    }
    return 0;
}

StackStats PeriodicThread::stack_stats() const
{
    if (thread_.handle == nullptr || thread_.handle->stack_addr == nullptr) {
        return {};
    }
    const auto* stack = static_cast<const uint8_t*>(
        thread_.handle->stack_addr);
    const size_t total = static_cast<size_t>(thread_.handle->stack_size);
    size_t free_bytes = 0U;
    while (free_bytes < total && stack[free_bytes] == '#') {
        ++free_bytes;
    }
    return {true, stack_size_bytes_,
            free_bytes < stack_size_bytes_ ? free_bytes : stack_size_bytes_};
}

EventGroup::~EventGroup()
{
    destroy();
}

bool EventGroup::create()
{
    destroy();
    native_handle_ = rt_event_create("evt", RT_IPC_FLAG_FIFO);
    return native_handle_ != nullptr;
}

void EventGroup::destroy()
{
    if (native_handle_ != nullptr) {
        rt_event_delete(static_cast<rt_event_t>(native_handle_));
        native_handle_ = nullptr;
    }
}

uint32_t EventGroup::set_bits(uint32_t bits)
{
    auto event = static_cast<rt_event_t>(native_handle_);
    if (event == nullptr) {
        return 0U;
    }
    (void)rt_event_send(event, bits);
    return get_bits();
}

uint32_t EventGroup::clear_bits(uint32_t bits)
{
    auto event = static_cast<rt_event_t>(native_handle_);
    if (event == nullptr) {
        return 0U;
    }

    rt_base_t level = rt_hw_interrupt_disable();
    event->set &= ~bits;
    const uint32_t current = event->set;
    rt_hw_interrupt_enable(level);
    return current;
}

uint32_t EventGroup::get_bits() const
{
    auto event = static_cast<rt_event_t>(native_handle_);
    if (event == nullptr) {
        return 0U;
    }

    rt_base_t level = rt_hw_interrupt_disable();
    const uint32_t bits = event->set;
    rt_hw_interrupt_enable(level);
    return bits;
}

uint32_t EventGroup::wait_bits(uint32_t bits_to_wait_for,
                               bool clear_on_exit,
                               WaitMode wait_mode,
                               Milliseconds timeout_ms)
{
    auto event = static_cast<rt_event_t>(native_handle_);
    if (event == nullptr || bits_to_wait_for == 0U) {
        return 0U;
    }

    rt_uint32_t received = 0U;
    rt_uint8_t option = wait_mode == WaitMode::All ? RT_EVENT_FLAG_AND
                                                   : RT_EVENT_FLAG_OR;
    if (clear_on_exit) {
        option |= RT_EVENT_FLAG_CLEAR;
    }

    if (rt_event_recv(event, bits_to_wait_for, option, timeout_ms_to_ticks(timeout_ms),
                      &received) != RT_EOK) {
        return 0U;
    }
    return received;
}

uint32_t EventGroup::sync(uint32_t bits_to_set,
                          uint32_t bits_to_wait_for,
                          Milliseconds timeout_ms)
{
    auto event = static_cast<rt_event_t>(native_handle_);
    if (event == nullptr || bits_to_wait_for == 0U) {
        return 0U;
    }

    (void)rt_event_send(event, bits_to_set);
    return wait_bits(bits_to_wait_for, true, WaitMode::All, timeout_ms);
}

MessageQueue::~MessageQueue()
{
    destroy();
}

bool MessageQueue::create(uint32_t length,
                          size_t item_size,
                          void* storage_buffer,
                          size_t storage_buffer_size_bytes)
{
    destroy();
    if (length == 0U || item_size == 0U) {
        return false;
    }

    item_size_ = item_size;

    if (storage_buffer != nullptr) {
        constexpr size_t alignment = RT_ALIGN_SIZE;
        if (item_size > SIZE_MAX - (alignment - 1U)) {
            return false;
        }
        const size_t aligned_item_size =
            (item_size + alignment - 1U) & ~(alignment - 1U);
        if (aligned_item_size
            > SIZE_MAX - kMessageQueueItemOverhead) {
            return false;
        }
        const size_t slot_size =
            aligned_item_size + kMessageQueueItemOverhead;
        if (slot_size > SIZE_MAX / static_cast<size_t>(length)) {
            return false;
        }
        const size_t required = static_cast<size_t>(length)
            * slot_size;
        if (storage_buffer_size_bytes < required
            || static_cast<size_t>(
                static_cast<rt_size_t>(required)) != required) {
            return false;
        }
        auto* mq = reinterpret_cast<rt_mq_t>(control_storage_);
        if (rt_mq_init(mq, "mq", storage_buffer, static_cast<rt_size_t>(item_size),
                       static_cast<rt_size_t>(required),
                       RT_IPC_FLAG_FIFO) != RT_EOK) {
            return false;
        }
        native_handle_ = mq;
        control_block_buffer_ = mq;
        owns_control_block_buffer_ = false;
        length_ = length;
        reset_stats();
        return true;
    }

    rt_mq_t mq = rt_mq_create("mq", static_cast<rt_size_t>(item_size),
                              length, RT_IPC_FLAG_FIFO);
    if (mq == nullptr) {
        return false;
    }
    native_handle_ = mq;
    length_ = length;
    reset_stats();
    return true;
}

void MessageQueue::destroy()
{
    auto mq = static_cast<rt_mq_t>(native_handle_);
    if (mq != nullptr) {
        if (control_block_buffer_ != nullptr) {
            (void)rt_mq_detach(mq);
        } else {
            (void)rt_mq_delete(mq);
        }
    }

    if (owns_control_block_buffer_ && control_block_buffer_ != nullptr) {
        rtos_free(control_block_buffer_);
    }
    native_handle_ = nullptr;
    control_block_buffer_ = nullptr;
    item_size_ = 0U;
    length_ = 0U;
    reset_stats();
    owns_control_block_buffer_ = false;
}

bool MessageQueue::send(const void* item, Milliseconds timeout_ms)
{
    auto mq = static_cast<rt_mq_t>(native_handle_);
    const bool succeeded = mq != nullptr && item != nullptr
        && rt_mq_send_wait(mq, item, static_cast<rt_size_t>(item_size_),
                           timeout_ms_to_ticks(timeout_ms)) == RT_EOK;
    record_send(succeeded);
    return succeeded;
}

bool MessageQueue::receive(void* item, Milliseconds timeout_ms)
{
    auto mq = static_cast<rt_mq_t>(native_handle_);
    return mq != nullptr && item != nullptr &&
           rt_mq_recv(mq, item, static_cast<rt_size_t>(item_size_),
                      timeout_ms_to_ticks(timeout_ms)) == RT_EOK;
}

bool MessageQueue::reset()
{
    auto mq = static_cast<rt_mq_t>(native_handle_);
    return mq != nullptr && rt_mq_control(mq, RT_IPC_CMD_RESET, nullptr) == RT_EOK;
}

uint32_t MessageQueue::count() const
{
    auto mq = static_cast<rt_mq_t>(native_handle_);
    return mq != nullptr ? mq->entry : 0U;
}

uint32_t MessageQueue::free_slots() const
{
    auto mq = static_cast<rt_mq_t>(native_handle_);
    return mq != nullptr ? static_cast<uint32_t>(mq->max_msgs - mq->entry) : 0U;
}

void MessageQueue::record_send(bool succeeded)
{
    if (!succeeded) {
        send_failures_.fetch_add(1U, std::memory_order_relaxed);
        return;
    }
    const uint32_t depth = count();
    uint32_t observed = high_water_mark_.load(std::memory_order_relaxed);
    while (observed < depth
           && !high_water_mark_.compare_exchange_weak(
               observed, depth, std::memory_order_relaxed)) {
    }
}

MessageQueueStats MessageQueue::stats() const
{
    return {length_, count(),
            high_water_mark_.load(std::memory_order_relaxed),
            send_failures_.load(std::memory_order_relaxed)};
}

void MessageQueue::reset_stats()
{
    high_water_mark_.store(count(), std::memory_order_relaxed);
    send_failures_.store(0U, std::memory_order_relaxed);
}

// ---- StreamBuffer (ring buffer + semaphore 实现) ----

struct StreamBufferInternal {
    uint8_t *buf;
    size_t cap;
    size_t trigger;
    size_t head;
    size_t tail;
    size_t count;
    bool event_pending;
    rt_sem_t sem;
    struct rt_semaphore sem_storage;
    bool owns_buf;
    bool static_control;
};

static_assert(sizeof(StreamBufferInternal)
              <= osal::kStreamBufferControlBytes);
static_assert(alignof(StreamBufferInternal) <= alignof(std::max_align_t));

static inline size_t sb_used(const StreamBufferInternal *sb) {
    return __atomic_load_n(&sb->count, __ATOMIC_ACQUIRE);
}

static inline size_t sb_free(const StreamBufferInternal *sb) {
    return sb->cap - sb_used(sb);
}

static size_t sb_write(StreamBufferInternal *sb,
                       const uint8_t *data, size_t len,
                       size_t& previous_count) {
    const size_t avail = sb_free(sb);
    if (len > avail) len = avail;
    for (size_t i = 0; i < len; i++) {
        sb->buf[(sb->head + i) % sb->cap] = data[i];
    }
    sb->head = (sb->head + len) % sb->cap;
    previous_count = __atomic_fetch_add(&sb->count, len,
                                         __ATOMIC_RELEASE);
    return len;
}

static size_t sb_read(StreamBufferInternal *sb,
                      uint8_t *data, size_t len) {
    const size_t avail = sb_used(sb);
    if (len > avail) len = avail;
    for (size_t i = 0; i < len; i++) {
        data[i] = sb->buf[(sb->tail + i) % sb->cap];
    }
    sb->tail = (sb->tail + len) % sb->cap;
    __atomic_fetch_sub(&sb->count, len, __ATOMIC_RELEASE);
    return len;
}

static void sb_drain_event(StreamBufferInternal *sb) {
    while (rt_sem_take(sb->sem, 0) == RT_EOK) {
    }
    __atomic_store_n(&sb->event_pending, false, __ATOMIC_RELEASE);
}

static bool sb_notify(StreamBufferInternal *sb) {
    bool expected = false;
    if (!__atomic_compare_exchange_n(&sb->event_pending, &expected, true,
                                     false, __ATOMIC_ACQ_REL,
                                     __ATOMIC_ACQUIRE)) {
        return false;
    }
    if (rt_sem_release(sb->sem) == RT_EOK) {
        return true;
    }
    __atomic_store_n(&sb->event_pending, false, __ATOMIC_RELEASE);
    return false;
}

static bool sb_wait_event(StreamBufferInternal *sb, rt_int32_t ticks) {
    if (rt_sem_take(sb->sem, ticks) != RT_EOK) {
        return false;
    }
    __atomic_store_n(&sb->event_pending, false, __ATOMIC_RELEASE);
    return true;
}

static bool sb_signal_if_ready(StreamBufferInternal *sb,
                               size_t previous_count,
                               size_t written) {
    if (written == 0U || previous_count >= sb->trigger
        || previous_count + written < sb->trigger) {
        return false;
    }
    return sb_notify(sb);
}

StreamBuffer::~StreamBuffer()
{
    destroy();
}

bool StreamBuffer::create(size_t buf_size, size_t trigger_level)
{
    destroy();
    if (buf_size == 0U || trigger_level == 0U
        || trigger_level > buf_size) {
        return false;
    }
    auto *sb = static_cast<StreamBufferInternal*>(
        rtos_malloc(sizeof(StreamBufferInternal)));
    if (sb == nullptr) return false;

    sb->buf = static_cast<uint8_t*>(rtos_malloc(buf_size));
    if (sb->buf == nullptr) { rtos_free(sb); return false; }

    sb->sem = rt_sem_create("sb", 0, RT_IPC_FLAG_FIFO);
    if (sb->sem == nullptr) {
        rtos_free(sb->buf); rtos_free(sb); return false;
    }

    sb->cap = buf_size;
    sb->trigger = trigger_level;
    sb->head = 0;
    sb->tail = 0;
    sb->count = 0;
    sb->event_pending = false;
    sb->owns_buf = true;
    sb->static_control = false;
    handle_ = sb;
    return true;
}

bool StreamBuffer::create(uint8_t *storage, size_t storage_size,
                          size_t trigger_level)
{
    destroy();
    if (storage == nullptr || storage_size <= 1U || trigger_level == 0U
        || trigger_level >= storage_size) {
        return false;
    }
    auto *sb = reinterpret_cast<StreamBufferInternal*>(control_storage_);
    sb->sem = &sb->sem_storage;
    if (rt_sem_init(sb->sem, "sb", 0U, RT_IPC_FLAG_FIFO) != RT_EOK) {
        return false;
    }

    sb->buf = storage;
    sb->cap = storage_size - 1U;
    sb->trigger = trigger_level;
    sb->head = 0;
    sb->tail = 0;
    sb->count = 0;
    sb->event_pending = false;
    sb->owns_buf = false;
    sb->static_control = true;
    handle_ = sb;
    return true;
}

void StreamBuffer::destroy()
{
    auto *sb = static_cast<StreamBufferInternal*>(handle_);
    if (sb == nullptr) return;
    if (sb->sem != nullptr) {
        if (sb->static_control) {
            (void)rt_sem_detach(sb->sem);
        } else {
            (void)rt_sem_delete(sb->sem);
        }
    }
    if (sb->owns_buf && sb->buf != nullptr) rtos_free(sb->buf);
    if (!sb->static_control) rtos_free(sb);
    handle_ = nullptr;
}

size_t StreamBuffer::send(const uint8_t *data, size_t len,
                          Milliseconds timeout_ms)
{
    auto *sb = static_cast<StreamBufferInternal*>(handle_);
    if (sb == nullptr || data == nullptr || len == 0U) return 0U;

    const rt_int32_t total_ticks = timeout_ms_to_ticks(timeout_ms);
    rt_int32_t remaining_ticks = total_ticks;
    const rt_tick_t start_tick = rt_tick_get();
    while (sb_free(sb) == 0U) {
        if (!sb_wait_event(sb, remaining_ticks)) {
            return 0U;
        }
        if (sb_free(sb) != 0U) {
            break;
        }
        if (total_ticks == RT_WAITING_FOREVER) {
            continue;
        }

        const rt_tick_t elapsed = rt_tick_get() - start_tick;
        if (elapsed >= static_cast<rt_tick_t>(total_ticks)) {
            return 0U;
        }
        remaining_ticks = total_ticks - static_cast<rt_int32_t>(elapsed);
    }

    size_t previous_count = 0U;
    const size_t written = sb_write(sb, data, len, previous_count);
    (void)sb_signal_if_ready(sb, previous_count, written);
    return written;
}

size_t StreamBuffer::send_from_isr(const uint8_t *data, size_t len,
                                   IsrContext& context)
{
    auto *sb = static_cast<StreamBufferInternal*>(handle_);
    if (sb == nullptr || data == nullptr || len == 0U) return 0U;

    size_t previous_count = 0U;
    const size_t written = sb_write(sb, data, len, previous_count);
    const bool released = sb_signal_if_ready(sb, previous_count, written);
    context.request_reschedule(released);
    return written;
}

size_t StreamBuffer::receive(uint8_t *data, size_t len,
                             Milliseconds timeout_ms)
{
    auto *sb = static_cast<StreamBufferInternal*>(handle_);
    if (sb == nullptr || data == nullptr || len == 0U) return 0U;

    const rt_int32_t total_ticks = timeout_ms_to_ticks(timeout_ms);
    rt_int32_t remaining_ticks = total_ticks;
    const rt_tick_t start_tick = rt_tick_get();
    while (sb_used(sb) == 0U) {
        if (!sb_wait_event(sb, remaining_ticks)) {
            if (sb_used(sb) == 0U) {
                return 0U;
            }
            break;
        }
        if (sb_used(sb) != 0U) {
            break;
        }
        if (total_ticks == RT_WAITING_FOREVER) {
            continue;
        }

        const rt_tick_t elapsed = rt_tick_get() - start_tick;
        if (elapsed >= static_cast<rt_tick_t>(total_ticks)) {
            return 0U;
        }
        remaining_ticks = total_ticks - static_cast<rt_int32_t>(elapsed);
    }

    const size_t read = sb_read(sb, data, len);
    (void)sb_notify(sb);

    return read;
}

size_t StreamBuffer::receive_from_isr(uint8_t *data, size_t len,
                                      IsrContext& context)
{
    auto *sb = static_cast<StreamBufferInternal*>(handle_);
    if (sb == nullptr || data == nullptr || len == 0U) return 0U;

    if (sb_used(sb) == 0U) {
        return 0U;
    }

    const size_t read = sb_read(sb, data, len);
    context.request_reschedule(sb_notify(sb));
    return read;
}

size_t StreamBuffer::bytes_available() const
{
    auto *sb = static_cast<StreamBufferInternal*>(handle_);
    return sb != nullptr ? sb_used(sb) : 0U;
}

size_t StreamBuffer::space_available() const
{
    auto *sb = static_cast<StreamBufferInternal*>(handle_);
    return sb != nullptr ? sb_free(sb) : 0U;
}

void StreamBuffer::reset()
{
    auto *sb = static_cast<StreamBufferInternal*>(handle_);
    if (sb == nullptr) return;
    rt_enter_critical();
    sb->head = 0;
    sb->tail = 0;
    __atomic_store_n(&sb->count, 0U, __ATOMIC_RELEASE);
    sb_drain_event(sb);
    rt_exit_critical();
}

void SoftTimer::dispatch(void* timer)
{
    auto* self = static_cast<SoftTimer*>(timer);
    if (self != nullptr && self->callback_ != nullptr) {
        self->callback_(self->context_);
    }
}

SoftTimer::~SoftTimer()
{
    destroy();
}

bool SoftTimer::create(const char* name,
                       Milliseconds period_ms,
                       bool auto_reload,
                       Callback callback,
                       void* context)
{
    destroy();
    if (callback == nullptr) {
        return false;
    }

    callback_ = callback;
    context_ = context;
    const rt_uint8_t flags =
        static_cast<rt_uint8_t>((auto_reload ? RT_TIMER_FLAG_PERIODIC
                                             : RT_TIMER_FLAG_ONE_SHOT) |
                                RT_TIMER_FLAG_SOFT_TIMER);
    native_handle_ = rt_timer_create(name != nullptr ? name : "tmr",
                                     SoftTimer::dispatch, this,
                                     period_ms_to_ticks(period_ms), flags);
    if (native_handle_ == nullptr) {
        callback_ = nullptr;
        context_ = nullptr;
    }
    return native_handle_ != nullptr;
}

void SoftTimer::destroy()
{
    if (native_handle_ != nullptr) {
        rt_timer_delete(static_cast<rt_timer_t>(native_handle_));
        native_handle_ = nullptr;
    }
    callback_ = nullptr;
    context_ = nullptr;
}

bool SoftTimer::start()
{
    auto timer = static_cast<rt_timer_t>(native_handle_);
    return timer != nullptr && rt_timer_start(timer) == RT_EOK;
}

bool SoftTimer::stop()
{
    auto timer = static_cast<rt_timer_t>(native_handle_);
    return timer != nullptr && rt_timer_stop(timer) == RT_EOK;
}

bool SoftTimer::reset()
{
    auto timer = static_cast<rt_timer_t>(native_handle_);
    if (timer == nullptr) {
        return false;
    }
    (void)rt_timer_stop(timer);
    return rt_timer_start(timer) == RT_EOK;
}

bool SoftTimer::change_period(Milliseconds period_ms)
{
    auto timer = static_cast<rt_timer_t>(native_handle_);
    if (timer == nullptr) {
        return false;
    }
    rt_tick_t ticks = period_ms_to_ticks(period_ms);
    return rt_timer_control(timer, RT_TIMER_CTRL_SET_TIME, &ticks) == RT_EOK;
}

bool SoftTimer::is_active() const
{
    auto timer = static_cast<rt_timer_t>(native_handle_);
    if (timer == nullptr) {
        return false;
    }
    rt_uint32_t state = 0U;
    return rt_timer_control(timer, RT_TIMER_CTRL_GET_STATE, &state) == RT_EOK &&
           state == RT_TIMER_FLAG_ACTIVATED;
}

CriticalSectionGuard::CriticalSectionGuard()
{
    rt_enter_critical();
}

CriticalSectionGuard::~CriticalSectionGuard()
{
    rt_exit_critical();
}

IsrCriticalSectionGuard::IsrCriticalSectionGuard()
    : saved_state_(static_cast<uintptr_t>(rt_hw_interrupt_disable()))
{
}

IsrCriticalSectionGuard::~IsrCriticalSectionGuard()
{
    rt_hw_interrupt_enable(static_cast<rt_base_t>(saved_state_));
}

namespace this_thread {

void sleep_for(Milliseconds timeout_ms)
{
    (void)rt_thread_mdelay(static_cast<rt_int32_t>(timeout_ms));
}

bool sleep_until(uint32_t* previous_wake_tick, Milliseconds period_ms)
{
    if (previous_wake_tick == nullptr) {
        return false;
    }

    rt_tick_t tick = static_cast<rt_tick_t>(*previous_wake_tick);
    const rt_err_t ret = rt_thread_delay_until(&tick, period_ms_to_ticks(period_ms));
    *previous_wake_tick = tick;
    return ret == RT_EOK;
}

void yield()
{
    (void)rt_thread_yield();
}

void suspend()
{
    (void)rt_thread_suspend(rt_thread_self());
    rt_schedule();
}

} // namespace this_thread

} // namespace osal
