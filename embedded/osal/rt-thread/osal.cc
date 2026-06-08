#include "rtdef.h"
#include "sys/util.h"

#include <mem.h>
#include <osal.h>
#include <rthw.h>
#include <rtthread.h>

#include <cstddef>
#include <cstdint>
#include <cstring>

ALIGN(8)
static rt_uint8_t main_stack[RT_MAIN_THREAD_STACK_SIZE];
static struct rt_thread main_thread;
static void (*g_user_entry)(void);

static void main_thread_wrapper(void* parameter)
{
    (void)parameter;
    g_user_entry();
}

#ifdef RT_USING_HEAP
static rt_uint8_t rt_heap_pool[32 * 1024];
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
    g_user_entry = entry;

    rt_thread_t tid = &main_thread;
    rt_err_t result = rt_thread_init(tid, "main", main_thread_wrapper, nullptr,
                                     main_stack, sizeof(main_stack),
                                     RT_MAIN_THREAD_PRIORITY, 20);
    RT_ASSERT(result == RT_EOK);
    (void)result;

    rt_thread_startup(tid);
    cpu_usage_init();
    rt_system_scheduler_start();

    return 0;
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

    rt_tick_t ticks = rt_tick_from_millisecond(static_cast<rt_int32_t>(period_ms));
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
    handle_ = rt_sem_create("sem", initial, RT_IPC_FLAG_FIFO);
    if (handle_ == nullptr) {
        max_count_ = 0U;
    }
}

Semaphore::~Semaphore()
{
    if (handle_ != nullptr) {
        rt_sem_delete(handle_);
        handle_ = nullptr;
    }
}

int Semaphore::take(uint32_t timeout_ms)
{
    if (handle_ == nullptr) {
        return -1;
    }
    return rt_sem_take(handle_, timeout_ms_to_ticks(timeout_ms)) == RT_EOK ? 0 : -1;
}

int Semaphore::release()
{
    if (handle_ == nullptr) {
        return -1;
    }

    rt_base_t level = rt_hw_interrupt_disable();
    const bool full = rt_list_isempty(&handle_->parent.suspend_thread) &&
                      handle_->value >= max_count_;
    rt_hw_interrupt_enable(level);
    if (full) {
        return -1;
    }

    return rt_sem_release(handle_) == RT_EOK ? 0 : -1;
}

uint32_t Semaphore::count() const
{
    if (handle_ == nullptr) {
        return 0U;
    }

    rt_base_t level = rt_hw_interrupt_disable();
    const uint32_t value = handle_->value;
    rt_hw_interrupt_enable(level);
    return value;
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
    handle_ = rt_mutex_create("mtx", RT_IPC_FLAG_FIFO);
    return handle_ != nullptr;
}

void Mutex::destroy()
{
    if (handle_ != nullptr) {
        rt_mutex_delete(handle_);
        handle_ = nullptr;
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
    destroy();
}

Thread* Thread::create(const char* name, Entry entry, void* param,
                       size_t stack_size, int32_t prio, int32_t tick)
{
    auto* thread = new Thread(PrivateTag {});
    if (thread == nullptr) {
        return nullptr;
    }

    ThreadConfig config {};
    config.name = name;
    config.priority = static_cast<Priority>(prio);
    config.stack_size_bytes = stack_size;
    config.time_slice_ticks = static_cast<uint32_t>(tick);
    if (!thread->start(entry, param, config)) {
        delete thread;
        return nullptr;
    }

    return thread;
}

bool Thread::start(Entry entry, void* context, const ThreadConfig& config)
{
    if (entry == nullptr || handle_.handle != nullptr) {
        return false;
    }

    const char* name = config.name != nullptr ? config.name : "thread";
    const rt_uint8_t prio = static_cast<rt_uint8_t>(config.priority);
    const rt_uint32_t tick = config.time_slice_ticks == 0U ? 20U : config.time_slice_ticks;

    if (config.stack_buffer != nullptr) {
        rt_err_t ret = rt_thread_init(&handle_.tcb, name, entry, context,
                                      config.stack_buffer,
                                      static_cast<rt_uint32_t>(config.stack_size_bytes),
                                      prio, tick);
        if (ret != RT_EOK) {
            return false;
        }
        handle_.handle = &handle_.tcb;
        handle_.flags = kThreadFlagStatic;
        return true;
    }

    rt_thread_t thread = rt_thread_create(name, entry, context,
                                          static_cast<rt_uint32_t>(config.stack_size_bytes),
                                          prio, tick);
    if (thread == nullptr) {
        return false;
    }
    handle_.handle = thread;

    handle_.flags = kThreadFlagDynamic;
    return true;
}

void Thread::destroy()
{
    if (handle_.handle == nullptr) {
        return;
    }

    if ((handle_.flags & kThreadFlagDynamic) != 0U) {
        (void)rt_thread_delete(handle_.handle);
    } else {
        (void)rt_thread_detach(handle_.handle);
    }

    handle_.handle = nullptr;
    handle_.flags = 0U;
}

int Thread::startup()
{
    if (handle_.handle == nullptr) {
        return -1;
    }

    const rt_uint8_t stat = handle_.handle->stat & RT_THREAD_STAT_MASK;
    if (stat == RT_THREAD_INIT) {
        return rt_thread_startup(handle_.handle) == RT_EOK ? 0 : -1;
    }
    if (stat == RT_THREAD_SUSPEND) {
        return rt_thread_resume(handle_.handle) == RT_EOK ? 0 : -1;
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

    rt_uint8_t native_priority = priority;
    return rt_thread_control(handle_.handle, RT_THREAD_CTRL_CHANGE_PRIORITY,
                             &native_priority) == RT_EOK;
}

Priority Thread::get_priority() const
{
    return handle_.handle != nullptr ? handle_.handle->current_priority : kPriorityMin;
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

static void periodic_wait_stopped()
{
    rt_thread_suspend(rt_thread_self());
    rt_schedule();
}

void PeriodicThread::threadEntry(void* parameter)
{
    auto* thread = static_cast<PeriodicThread*>(parameter);

    for (;;) {
        while (!thread->running_) {
            periodic_wait_stopped();
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
}

PeriodicThread::PeriodicThread(PrivateTag)
{
}

PeriodicThread::~PeriodicThread()
{
    (void)stop();
    if (thread_.handle != nullptr) {
        rt_thread_delete(thread_.handle);
        thread_.handle = nullptr;
    }
    if (sem_ != nullptr) {
        rt_sem_delete(sem_);
        sem_ = nullptr;
    }
}

PeriodicThread* PeriodicThread::create(const char* name,
                                       PeriodicEntry entry,
                                       void* param,
                                       size_t stack_size,
                                       int32_t prio,
                                       uint32_t frequency_hz,
                                       PeriodicTrigger trigger)
{
    if (entry == nullptr || frequency_hz == 0U) {
        return nullptr;
    }
    if (trigger == PeriodicTrigger::Tick && frequency_hz > RT_TICK_PER_SECOND) {
        return nullptr;
    }

    auto* thread = new PeriodicThread(PrivateTag {});
    if (thread == nullptr) {
        return nullptr;
    }

    thread->entry_ = entry;
    thread->param_ = param;
    thread->frequency_hz_ = frequency_hz;
    thread->trigger_ = trigger;

    if (trigger == PeriodicTrigger::External) {
        thread->sem_ = rt_sem_create("per", 0U, RT_IPC_FLAG_FIFO);
        if (thread->sem_ == nullptr) {
            delete thread;
            return nullptr;
        }
    }

    rt_thread_t h = rt_thread_create(name, PeriodicThread::threadEntry, thread,
                                     static_cast<rt_uint32_t>(stack_size),
                                     static_cast<rt_uint8_t>(prio), 20U);
    if (h == nullptr) {
        delete thread;
        return nullptr;
    }
    thread->thread_.handle = h;
    thread->thread_.flags = kThreadFlagDynamic;
    return thread;
}

int PeriodicThread::startup()
{
    if (thread_.handle == nullptr) {
        return -1;
    }

    running_ = true;
    if (!started_) {
        started_ = true;
        return rt_thread_startup(thread_.handle) == RT_EOK ? 0 : -1;
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
        auto* mq = static_cast<rt_mq_t>(rtos_malloc(sizeof(struct rt_messagequeue)));
        if (mq == nullptr) {
            return false;
        }
        if (rt_mq_init(mq, "mq", storage_buffer, static_cast<rt_size_t>(item_size),
                       static_cast<rt_size_t>(storage_buffer_size_bytes),
                       RT_IPC_FLAG_FIFO) != RT_EOK) {
            rtos_free(mq);
            return false;
        }
        native_handle_ = mq;
        control_block_buffer_ = mq;
        owns_control_block_buffer_ = true;
        return true;
    }

    rt_mq_t mq = rt_mq_create("mq", static_cast<rt_size_t>(item_size),
                              length, RT_IPC_FLAG_FIFO);
    if (mq == nullptr) {
        return false;
    }
    native_handle_ = mq;
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
    owns_control_block_buffer_ = false;
}

bool MessageQueue::send(const void* item, Milliseconds timeout_ms)
{
    auto mq = static_cast<rt_mq_t>(native_handle_);
    return mq != nullptr && item != nullptr &&
           rt_mq_send_wait(mq, item, static_cast<rt_size_t>(item_size_),
                           timeout_ms_to_ticks(timeout_ms)) == RT_EOK;
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
