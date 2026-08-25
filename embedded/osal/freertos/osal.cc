#include <mem.h>
#include <osal.h>
#include <arch/arm/cortex_m/fault.h>

#include <event_groups.h>
#include <queue.h>
#include <stream_buffer.h>
#include <timers.h>

#include <cstddef>
#include <cstdint>
#include <limits>

#define MAIN_THREAD_STACK_SIZE  (CONFIG_MAIN_STACK_SIZE / sizeof(StackType_t))

static_assert(CONFIG_NUM_IRQ_PRIO_BITS == __NVIC_PRIO_BITS,
              "Kconfig/CMSIS NVIC priority-bit mismatch");
static_assert((CONFIG_MAIN_STACK_SIZE % sizeof(StackType_t)) == 0U,
              "CONFIG_MAIN_STACK_SIZE must align to StackType_t");

static TaskHandle_t main_task_handle;
static void (*g_user_entry)(void);
static StaticTask_t main_task_tcb;
static StackType_t main_task_stack[MAIN_THREAD_STACK_SIZE];

static void main_task_entry(void* parameter)
{
    (void)parameter;
    g_user_entry();
    vTaskDelete(nullptr);
    for (;;) {
    }
}

int osal_init(void)
{
    return 0;
}

int osal_start(void (*entry)(void))
{
    if (entry == nullptr) {
        return -1;
    }
    g_user_entry = entry;
    main_task_handle = xTaskCreateStatic(
        main_task_entry, "main", MAIN_THREAD_STACK_SIZE, nullptr,
        configMAX_PRIORITIES / 3, main_task_stack, &main_task_tcb);
    if (main_task_handle == nullptr) {
        return -1;
    }

    vTaskStartScheduler();
    return -1;
}

namespace {

constexpr uint32_t kThreadFlagDynamic = 1U << 0U;
constexpr uint32_t kThreadFlagStatic = 1U << 1U;

TickType_t timeout_ms_to_ticks(uint32_t timeout_ms)
{
    if (timeout_ms == OSAL_WAITING_FOREVER) {
        return portMAX_DELAY;
    }
    if (timeout_ms == 0U) {
        return 0U;
    }

    uint64_t ticks = (static_cast<uint64_t>(timeout_ms) * configTICK_RATE_HZ + 999U) / 1000U;
    if (ticks == 0U) {
        ticks = 1U;
    }
    if (ticks >= portMAX_DELAY) {
        ticks = portMAX_DELAY - 1U;
    }
    return static_cast<TickType_t>(ticks);
}

TickType_t period_ms_to_ticks(uint32_t period_ms)
{
    TickType_t ticks = timeout_ms_to_ticks(period_ms);
    return ticks == 0U ? 1U : ticks;
}

configSTACK_DEPTH_TYPE stack_depth_from_bytes(size_t stack_size_bytes)
{
    if (stack_size_bytes < sizeof(StackType_t)
        || (stack_size_bytes % sizeof(StackType_t)) != 0U) {
        return 0U;
    }
    const size_t depth = stack_size_bytes / sizeof(StackType_t);
    if (depth > std::numeric_limits<configSTACK_DEPTH_TYPE>::max()) {
        return 0U;
    }
    return static_cast<configSTACK_DEPTH_TYPE>(depth);
}

osal::Thread::State map_task_state(eTaskState state)
{
    switch (state) {
    case eRunning:
        return osal::Thread::State::Running;
    case eReady:
        return osal::Thread::State::Ready;
    case eBlocked:
        return osal::Thread::State::Blocked;
    case eSuspended:
        return osal::Thread::State::Suspended;
    case eDeleted:
        return osal::Thread::State::Deleted;
    default:
        return osal::Thread::State::Invalid;
    }
}

} // namespace

namespace osal {

void Kernel::start()
{
    vTaskStartScheduler();
}

bool Kernel::is_running()
{
    return xTaskGetSchedulerState() == taskSCHEDULER_RUNNING;
}

Kernel::SchedulerState Kernel::get_scheduler_state()
{
    switch (xTaskGetSchedulerState()) {
    case taskSCHEDULER_RUNNING:
        return SchedulerState::Running;
    case taskSCHEDULER_SUSPENDED:
        return SchedulerState::Suspended;
    default:
        return SchedulerState::NotStarted;
    }
}

bool Kernel::in_isr()
{
    return xPortIsInsideInterrupt() != pdFALSE;
}

uint32_t Kernel::tick_count()
{
    return in_isr() ? xTaskGetTickCountFromISR() : xTaskGetTickCount();
}

uint32_t Kernel::uptime_ms()
{
    const uint64_t ticks = tick_count();
    return static_cast<uint32_t>((ticks * 1000U) / configTICK_RATE_HZ);
}

void Kernel::suspend_scheduler()
{
    vTaskSuspendAll();
}

bool Kernel::resume_scheduler()
{
    return xTaskResumeAll() == pdTRUE;
}

Semaphore::Semaphore(uint32_t initial, uint32_t max_count)
{
    const auto native_initial = static_cast<UBaseType_t>(initial);
    const auto native_max = static_cast<UBaseType_t>(max_count);
    if (max_count == 0U || initial > max_count ||
        static_cast<uint32_t>(native_initial) != initial ||
        static_cast<uint32_t>(native_max) != max_count) {
        return;
    }

    max_count_ = max_count;
    auto* const storage = reinterpret_cast<StaticSemaphore_t*>(
        control_storage_);
    handle_ = xSemaphoreCreateCountingStatic(
        native_max, native_initial, storage);
    if (handle_ == nullptr) {
        max_count_ = 0U;
    } else {
        is_static_ = true;
    }
}

Semaphore::~Semaphore()
{
    if (handle_ != nullptr) {
        vSemaphoreDelete(handle_);
        handle_ = nullptr;
        is_static_ = false;
    }
}

int Semaphore::take(uint32_t timeout_ms)
{
    if (handle_ == nullptr) {
        return -1;
    }
    return xSemaphoreTake(handle_, timeout_ms_to_ticks(timeout_ms)) == pdTRUE ? 0 : -1;
}

int Semaphore::release()
{
    if (handle_ == nullptr) {
        return -1;
    }
    return xSemaphoreGive(handle_) == pdTRUE ? 0 : -1;
}

MemoryStats Kernel::memory_stats()
{
    return {true,
            static_cast<size_t>(xPortGetFreeHeapSize()),
            static_cast<size_t>(xPortGetMinimumEverFreeHeapSize())};
}

int Semaphore::release_from_isr(IsrContext& context)
{
    if (handle_ == nullptr) {
        return -1;
    }

    BaseType_t higher_priority_woken = pdFALSE;
    const BaseType_t result =
        xSemaphoreGiveFromISR(handle_, &higher_priority_woken);
    context.request_reschedule(higher_priority_woken == pdTRUE);
    return result == pdTRUE ? 0 : -1;
}

uint32_t Semaphore::count() const
{
    return handle_ != nullptr ? static_cast<uint32_t>(uxSemaphoreGetCount(handle_)) : 0U;
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
    auto* const storage = reinterpret_cast<StaticSemaphore_t*>(
        control_storage_);
    handle_ = xSemaphoreCreateMutexStatic(storage);
    is_static_ = handle_ != nullptr;
    return is_static_;
}

void Mutex::destroy()
{
    if (handle_ != nullptr) {
        vSemaphoreDelete(handle_);
        handle_ = nullptr;
        is_static_ = false;
    }
}

int Mutex::lock(uint32_t timeout_ms)
{
    if (handle_ == nullptr) {
        return -1;
    }
    return xSemaphoreTake(handle_, timeout_ms_to_ticks(timeout_ms)) == pdTRUE ? 0 : -1;
}

int Mutex::try_lock()
{
    if (handle_ == nullptr) {
        return -1;
    }
    return xSemaphoreTake(handle_, 0U) == pdTRUE ? 0 : -1;
}

int Mutex::unlock()
{
    if (handle_ == nullptr) {
        return -1;
    }
    return xSemaphoreGive(handle_) == pdTRUE ? 0 : -1;
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
        vTaskSuspend(nullptr);
    }
    if (!thread->stop_requested()) {
        thread->entry_(thread->context_);
    }
    thread->exited_.store(true, std::memory_order_release);
    for (;;) {
        vTaskSuspend(nullptr);
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
        || config.priority > kPriorityMax) {
        return false;
    }

    const char* name = config.name != nullptr ? config.name : "thread";
    const configSTACK_DEPTH_TYPE stack_depth =
        stack_depth_from_bytes(config.stack_size_bytes);
    if (stack_depth == 0U) {
        return false;
    }

    entry_ = entry;
    context_ = context;
    stop_requested_.store(false, std::memory_order_relaxed);
    started_.store(false, std::memory_order_relaxed);
    exited_.store(false, std::memory_order_relaxed);

    if (config.stack_buffer != nullptr) {
        if ((reinterpret_cast<uintptr_t>(config.stack_buffer)
             % alignof(StackType_t)) != 0U) {
            return false;
        }
        handle_.handle = xTaskCreateStatic(threadEntry, name, stack_depth, this,
                                           config.priority,
                                           static_cast<StackType_t*>(config.stack_buffer),
                                           &handle_.tcb);
        if (handle_.handle == nullptr) {
            return false;
        }
        handle_.flags = kThreadFlagStatic;
    } else {
        TaskHandle_t h = nullptr;
        const BaseType_t ret = xTaskCreate(threadEntry, name, stack_depth, this,
                                           config.priority, &h);
        if (ret != pdPASS || h == nullptr) {
            return false;
        }
        handle_.handle = h;
        handle_.flags = kThreadFlagDynamic;
    }

    vTaskSuspend(handle_.handle);
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
        (void)xTaskAbortDelay(handle_.handle);
        vTaskResume(handle_.handle);
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
        osal_sleep(1);
    }
    return true;
}

bool Thread::shutdown(Milliseconds timeout_ms)
{
    if (handle_.handle == nullptr) {
        return true;
    }
    if (xTaskGetCurrentTaskHandle() == handle_.handle) {
        return false;
    }
    if (!Kernel::is_running()) {
        vTaskDelete(handle_.handle);
    } else {
        request_stop();
        if (!join(timeout_ms)) {
            return false;
        }
        vTaskDelete(handle_.handle);
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
    started_.store(true, std::memory_order_release);
    vTaskResume(handle_.handle);
    return 0;
}

int Thread::suspend()
{
    if (handle_.handle == nullptr) {
        return -1;
    }
    vTaskSuspend(handle_.handle);
    return 0;
}

int Thread::resume()
{
    if (handle_.handle == nullptr) {
        return -1;
    }
    vTaskResume(handle_.handle);
    return 0;
}

int Thread::yield()
{
    taskYIELD();
    return 0;
}

bool Thread::set_priority(Priority priority)
{
    if (handle_.handle == nullptr || priority >= configMAX_PRIORITIES) {
        return false;
    }
    vTaskPrioritySet(handle_.handle, priority);
    return true;
}

Priority Thread::get_priority() const
{
    return handle_.handle != nullptr ? static_cast<Priority>(uxTaskPriorityGet(handle_.handle))
                                     : kPriorityMin;
}

Thread::State Thread::get_state() const
{
    return handle_.handle != nullptr ? map_task_state(eTaskGetState(handle_.handle))
                                     : State::Invalid;
}

const char* Thread::get_name() const
{
    return handle_.handle != nullptr ? pcTaskGetName(handle_.handle) : nullptr;
}

bool Thread::abort_delay()
{
    return handle_.handle != nullptr && xTaskAbortDelay(handle_.handle) == pdPASS;
}

// ─── IsrTrigger ──────────────────────────────────────────────────

IsrTrigger::Slot IsrTrigger::s_slots[kMaxSlots];

int IsrTrigger::register_slot(Callback cb, void *arg)
{
    if (cb == nullptr) {
        return -1;
    }
    taskENTER_CRITICAL();
    for (int id = 0; id < kMaxSlots; ++id) {
        if (s_slots[id].cb == nullptr) {
            s_slots[id] = {cb, arg};
            taskEXIT_CRITICAL();
            return id;
        }
    }
    taskEXIT_CRITICAL();
    return -1;
}

StackStats Thread::stack_stats() const
{
    if (handle_.handle == nullptr) {
        return {};
    }
    const size_t free_bytes = static_cast<size_t>(
        uxTaskGetStackHighWaterMark(handle_.handle)) * sizeof(StackType_t);
    return {true, stack_size_bytes_,
            free_bytes < stack_size_bytes_ ? free_bytes : stack_size_bytes_};
}

void IsrTrigger::unregister_slot(int id)
{
    if (id < 0 || id >= kMaxSlots) {
        return;
    }
    taskENTER_CRITICAL();
    s_slots[id] = {};
    taskEXIT_CRITICAL();
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

void PeriodicThread::threadEntry(void* parameter)
{
    auto* thread = static_cast<PeriodicThread*>(parameter);

    while (!thread->terminate_.load(std::memory_order_acquire)) {
        while (!thread->running_
               && !thread->terminate_.load(std::memory_order_acquire)) {
            vTaskSuspend(nullptr);
        }

        if (thread->terminate_.load(std::memory_order_acquire)) {
            break;
        }

        if (thread->trigger_ == PeriodicTrigger::Tick) {
            uint32_t phase = 0U;
            TickType_t last_tick = xTaskGetTickCount();

            while (thread->running_) {
                const TickType_t delay_ticks = static_cast<TickType_t>(
                    nextDelayTicks(configTICK_RATE_HZ, thread->frequency_hz_, phase));
                if (xTaskDelayUntil(&last_tick, delay_ticks) == pdFALSE) {
                    thread->missed_ = thread->missed_ + 1U;
                    last_tick = xTaskGetTickCount();
                }
                if (!thread->running_) {
                    break;
                }

                thread->callEntry();
            }
        } else {
            while (thread->running_) {
                if (xSemaphoreTake(thread->sem_, portMAX_DELAY) != pdTRUE) {
                    continue;
                }
                if (!thread->running_) {
                    break;
                }

                taskENTER_CRITICAL();
                thread->pending_ = 0U;
                taskEXIT_CRITICAL();

                thread->callEntry();
            }
        }
    }
    thread->exited_.store(true, std::memory_order_release);
    for (;;) {
        vTaskSuspend(nullptr);
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
        taskENTER_CRITICAL();
        const bool detached = timer_->enable_update_irq(nullptr, nullptr);
        if (detached) {
            timer_attached_ = false;
        }
        taskEXIT_CRITICAL();
        if (!detached) {
            return false;
        }
    }
    IsrTrigger::unregister_slot(trigger_id_);
    trigger_id_ = -1;
    if (thread_.handle != nullptr) {
        if (xTaskGetCurrentTaskHandle() == thread_.handle) {
            hal::fault::panic(hal::fault::FatalReason::ThreadShutdownTimeout,
                              0, "periodic_self_delete", 0U);
        }
        if (started_ && Kernel::is_running()) {
            terminate_.store(true, std::memory_order_release);
            running_.store(false, std::memory_order_release);
            if (sem_ != nullptr) {
                (void)xSemaphoreGive(sem_);
            }
            (void)xTaskAbortDelay(thread_.handle);
            vTaskResume(thread_.handle);
            Deadline deadline(timeout_ms);
            while (!exited_.load(std::memory_order_acquire)) {
                if (deadline.expired()) {
                    return false;
                }
                osal_sleep(1U);
            }
        }
        vTaskDelete(thread_.handle);
        thread_.handle = nullptr;
        thread_.flags = 0U;
        started_ = false;
        stack_size_bytes_ = 0U;
    }
    if (sem_ != nullptr) {
        vSemaphoreDelete(sem_);
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
    const configSTACK_DEPTH_TYPE stack_depth =
        stack_depth_from_bytes(config.stack_size_bytes);
    if (config.entry == nullptr || config.frequency_hz == 0U
        || stack_depth == 0U
        || config.priority > kPriorityMax
        || thread_.handle != nullptr || sem_ != nullptr
        || (config.trigger != PeriodicTrigger::Tick
            && config.trigger != PeriodicTrigger::External)
        || (config.trigger == PeriodicTrigger::Tick
            && config.timer != nullptr)
        || (config.stack_buffer != nullptr
            && (reinterpret_cast<uintptr_t>(config.stack_buffer)
                % alignof(StackType_t)) != 0U)) {
        return false;
    }
    if (config.trigger == PeriodicTrigger::Tick
        && config.frequency_hz > configTICK_RATE_HZ) {
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
        auto* const storage = reinterpret_cast<StaticSemaphore_t*>(
            sem_storage_);
        sem_ = xSemaphoreCreateCountingStatic(1U, 0U, storage);
        sem_is_static_ = sem_ != nullptr;
        if (sem_ == nullptr) {
            (void)shutdown(0U);
            return false;
        }
    }

    const char* name = config.name != nullptr ? config.name : "periodic";
    if (config.stack_buffer != nullptr) {
        thread_.handle = xTaskCreateStatic(
            PeriodicThread::threadEntry, name, stack_depth, this,
            config.priority, static_cast<StackType_t*>(config.stack_buffer),
            &thread_.tcb);
        thread_.flags = kThreadFlagStatic;
    } else {
        TaskHandle_t handle = nullptr;
        const BaseType_t result = xTaskCreate(
            PeriodicThread::threadEntry, name, stack_depth, this,
            config.priority, &handle);
        if (result == pdPASS) {
            thread_.handle = handle;
            thread_.flags = kThreadFlagDynamic;
        }
    }
    if (thread_.handle == nullptr) {
        (void)shutdown(0U);
        return false;
    }
    stack_size_bytes_ = config.stack_size_bytes;
    vTaskSuspend(thread_.handle);
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
    started_ = true;
    vTaskResume(thread_.handle);
    return 0;
}

int PeriodicThread::stop()
{
    running_ = false;
    if (trigger_ == PeriodicTrigger::External && sem_ != nullptr) {
        (void)xSemaphoreGive(sem_);
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
    UBaseType_t irq_state = taskENTER_CRITICAL_FROM_ISR();
    if (pending_ == 0U) {
        pending_ = 1U;
        missed_ = missed_ + events - 1U;
        release = true;
    } else {
        missed_ = missed_ + events;
    }
    taskEXIT_CRITICAL_FROM_ISR(irq_state);

    if (release) {
        BaseType_t higher_priority_woken = pdFALSE;
        if (xSemaphoreGiveFromISR(sem_, &higher_priority_woken) != pdTRUE) {
            return -1;
        }
        portYIELD_FROM_ISR(higher_priority_woken);
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
    native_handle_ = xEventGroupCreate();
    return native_handle_ != nullptr;
}

void EventGroup::destroy()
{
    if (native_handle_ != nullptr) {
        vEventGroupDelete(static_cast<EventGroupHandle_t>(native_handle_));
        native_handle_ = nullptr;
    }
}

uint32_t EventGroup::set_bits(uint32_t bits)
{
    auto event = static_cast<EventGroupHandle_t>(native_handle_);
    if (event == nullptr) {
        return 0U;
    }
    return static_cast<uint32_t>(xEventGroupSetBits(event, bits));
}

uint32_t EventGroup::clear_bits(uint32_t bits)
{
    auto event = static_cast<EventGroupHandle_t>(native_handle_);
    if (event == nullptr) {
        return 0U;
    }
    return static_cast<uint32_t>(xEventGroupClearBits(event, bits));
}

uint32_t EventGroup::get_bits() const
{
    auto event = static_cast<EventGroupHandle_t>(native_handle_);
    return event != nullptr ? static_cast<uint32_t>(xEventGroupGetBits(event)) : 0U;
}

uint32_t EventGroup::wait_bits(uint32_t bits_to_wait_for,
                               bool clear_on_exit,
                               WaitMode wait_mode,
                               Milliseconds timeout_ms)
{
    auto event = static_cast<EventGroupHandle_t>(native_handle_);
    if (event == nullptr || bits_to_wait_for == 0U) {
        return 0U;
    }
    return static_cast<uint32_t>(
        xEventGroupWaitBits(event, bits_to_wait_for,
                            clear_on_exit ? pdTRUE : pdFALSE,
                            wait_mode == WaitMode::All ? pdTRUE : pdFALSE,
                            timeout_ms_to_ticks(timeout_ms)));
}

uint32_t EventGroup::sync(uint32_t bits_to_set,
                          uint32_t bits_to_wait_for,
                          Milliseconds timeout_ms)
{
    auto event = static_cast<EventGroupHandle_t>(native_handle_);
    if (event == nullptr || bits_to_wait_for == 0U) {
        return 0U;
    }
    return static_cast<uint32_t>(
        xEventGroupSync(event, bits_to_set, bits_to_wait_for,
                        timeout_ms_to_ticks(timeout_ms)));
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
        if (item_size > SIZE_MAX / static_cast<size_t>(length)) {
            return false;
        }
        const size_t required = static_cast<size_t>(length) * item_size;
        if (storage_buffer_size_bytes < required) {
            return false;
        }

        auto* control = reinterpret_cast<StaticQueue_t*>(control_storage_);

        QueueHandle_t queue = xQueueCreateStatic(length, item_size,
                                                 static_cast<uint8_t*>(storage_buffer),
                                                 control);
        if (queue == nullptr) {
            return false;
        }

        native_handle_ = queue;
        control_block_buffer_ = control;
        owns_control_block_buffer_ = false;
        length_ = length;
        reset_stats();
        return true;
    }

    QueueHandle_t queue = xQueueCreate(length, item_size);
    if (queue == nullptr) {
        return false;
    }

    native_handle_ = queue;
    length_ = length;
    reset_stats();
    return true;
}

void MessageQueue::destroy()
{
    auto queue = static_cast<QueueHandle_t>(native_handle_);
    if (queue != nullptr) {
        vQueueDelete(queue);
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

StackStats PeriodicThread::stack_stats() const
{
    if (thread_.handle == nullptr) {
        return {};
    }
    const size_t free_bytes = static_cast<size_t>(
        uxTaskGetStackHighWaterMark(thread_.handle)) * sizeof(StackType_t);
    return {true, stack_size_bytes_,
            free_bytes < stack_size_bytes_ ? free_bytes : stack_size_bytes_};
}

bool MessageQueue::send(const void* item, Milliseconds timeout_ms)
{
    auto queue = static_cast<QueueHandle_t>(native_handle_);
    const bool succeeded = queue != nullptr && item != nullptr
        && xQueueSend(queue, item, timeout_ms_to_ticks(timeout_ms)) == pdTRUE;
    record_send(succeeded);
    return succeeded;
}

bool MessageQueue::receive(void* item, Milliseconds timeout_ms)
{
    auto queue = static_cast<QueueHandle_t>(native_handle_);
    return queue != nullptr && item != nullptr &&
           xQueueReceive(queue, item, timeout_ms_to_ticks(timeout_ms)) == pdTRUE;
}

bool MessageQueue::reset()
{
    auto queue = static_cast<QueueHandle_t>(native_handle_);
    return queue != nullptr && xQueueReset(queue) == pdPASS;
}

uint32_t MessageQueue::count() const
{
    auto queue = static_cast<QueueHandle_t>(native_handle_);
    return queue != nullptr ? static_cast<uint32_t>(uxQueueMessagesWaiting(queue)) : 0U;
}

uint32_t MessageQueue::free_slots() const
{
    auto queue = static_cast<QueueHandle_t>(native_handle_);
    return queue != nullptr ? static_cast<uint32_t>(uxQueueSpacesAvailable(queue)) : 0U;
}

// ---- StreamBuffer ----

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
    auto h = xStreamBufferCreate(buf_size, trigger_level);
    if (h == nullptr) {
        return false;
    }
    handle_ = h;
    control_block_ = nullptr;
    owns_storage_ = true;
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
    auto *ctrl = reinterpret_cast<StaticStreamBuffer_t*>(control_storage_);
    auto h = xStreamBufferCreateStatic(storage_size, trigger_level,
                                       storage, ctrl);
    if (h == nullptr) {
        return false;
    }
    handle_ = h;
    control_block_ = ctrl;
    owns_storage_ = false;
    return true;
}

void StreamBuffer::destroy()
{
    auto h = static_cast<StreamBufferHandle_t>(handle_);
    if (h != nullptr) {
        vStreamBufferDelete(h);
    }
    handle_ = nullptr;
    control_block_ = nullptr;
    owns_storage_ = false;
}

size_t StreamBuffer::send(const uint8_t *data, size_t len,
                          Milliseconds timeout_ms)
{
    auto h = static_cast<StreamBufferHandle_t>(handle_);
    if (h == nullptr || data == nullptr || len == 0U) {
        return 0U;
    }
    return xStreamBufferSend(h, data, len,
                             timeout_ms_to_ticks(timeout_ms));
}

size_t StreamBuffer::send_from_isr(const uint8_t *data, size_t len,
                                   IsrContext& context)
{
    auto h = static_cast<StreamBufferHandle_t>(handle_);
    if (h == nullptr || data == nullptr || len == 0U) {
        return 0U;
    }
    BaseType_t woken = pdFALSE;
    size_t written = xStreamBufferSendFromISR(h, data, len, &woken);
    context.request_reschedule(woken == pdTRUE);
    return written;
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

size_t StreamBuffer::receive(uint8_t *data, size_t len,
                             Milliseconds timeout_ms)
{
    auto h = static_cast<StreamBufferHandle_t>(handle_);
    if (h == nullptr || data == nullptr || len == 0U) {
        return 0U;
    }
    return xStreamBufferReceive(h, data, len,
                                timeout_ms_to_ticks(timeout_ms));
}

size_t StreamBuffer::receive_from_isr(uint8_t *data, size_t len,
                                      IsrContext& context)
{
    auto h = static_cast<StreamBufferHandle_t>(handle_);
    if (h == nullptr || data == nullptr || len == 0U) {
        return 0U;
    }
    BaseType_t woken = pdFALSE;
    size_t read = xStreamBufferReceiveFromISR(h, data, len, &woken);
    context.request_reschedule(woken == pdTRUE);
    return read;
}

size_t StreamBuffer::bytes_available() const
{
    auto h = static_cast<StreamBufferHandle_t>(handle_);
    return h != nullptr ? xStreamBufferBytesAvailable(h) : 0U;
}

size_t StreamBuffer::space_available() const
{
    auto h = static_cast<StreamBufferHandle_t>(handle_);
    return h != nullptr ? xStreamBufferSpacesAvailable(h) : 0U;
}

void StreamBuffer::reset()
{
    auto h = static_cast<StreamBufferHandle_t>(handle_);
    if (h != nullptr) {
        xStreamBufferReset(h);
    }
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
    const auto timer_callback = [](TimerHandle_t timer) {
        SoftTimer::dispatch(pvTimerGetTimerID(timer));
    };

    native_handle_ = xTimerCreate(name != nullptr ? name : "tmr",
                                  period_ms_to_ticks(period_ms),
                                  auto_reload ? pdTRUE : pdFALSE,
                                  this,
                                  timer_callback);
    if (native_handle_ == nullptr) {
        callback_ = nullptr;
        context_ = nullptr;
    }
    return native_handle_ != nullptr;
}

void SoftTimer::destroy()
{
    if (native_handle_ != nullptr) {
        (void)xTimerDelete(static_cast<TimerHandle_t>(native_handle_), portMAX_DELAY);
        native_handle_ = nullptr;
    }
    callback_ = nullptr;
    context_ = nullptr;
}

bool SoftTimer::start()
{
    auto timer = static_cast<TimerHandle_t>(native_handle_);
    return timer != nullptr && xTimerStart(timer, portMAX_DELAY) == pdPASS;
}

bool SoftTimer::stop()
{
    auto timer = static_cast<TimerHandle_t>(native_handle_);
    return timer != nullptr && xTimerStop(timer, portMAX_DELAY) == pdPASS;
}

bool SoftTimer::reset()
{
    auto timer = static_cast<TimerHandle_t>(native_handle_);
    return timer != nullptr && xTimerReset(timer, portMAX_DELAY) == pdPASS;
}

bool SoftTimer::change_period(Milliseconds period_ms)
{
    auto timer = static_cast<TimerHandle_t>(native_handle_);
    return timer != nullptr &&
           xTimerChangePeriod(timer, period_ms_to_ticks(period_ms), portMAX_DELAY) == pdPASS;
}

bool SoftTimer::is_active() const
{
    auto timer = static_cast<TimerHandle_t>(native_handle_);
    return timer != nullptr && xTimerIsTimerActive(timer) != pdFALSE;
}

CriticalSectionGuard::CriticalSectionGuard()
{
    taskENTER_CRITICAL();
}

CriticalSectionGuard::~CriticalSectionGuard()
{
    taskEXIT_CRITICAL();
}

IsrCriticalSectionGuard::IsrCriticalSectionGuard()
    : saved_state_(static_cast<uintptr_t>(taskENTER_CRITICAL_FROM_ISR()))
{
}

IsrCriticalSectionGuard::~IsrCriticalSectionGuard()
{
    taskEXIT_CRITICAL_FROM_ISR(static_cast<UBaseType_t>(saved_state_));
}

namespace this_thread {

void sleep_for(Milliseconds timeout_ms)
{
    vTaskDelay(timeout_ms_to_ticks(timeout_ms));
}

bool sleep_until(uint32_t* previous_wake_tick, Milliseconds period_ms)
{
    if (previous_wake_tick == nullptr) {
        return false;
    }

    TickType_t tick = static_cast<TickType_t>(*previous_wake_tick);
    const BaseType_t ret = xTaskDelayUntil(&tick, period_ms_to_ticks(period_ms));
    *previous_wake_tick = tick;
    return ret == pdTRUE;
}

void yield()
{
    taskYIELD();
}

void suspend()
{
    vTaskSuspend(nullptr);
}

} // namespace this_thread

} // namespace osal

static StaticTask_t idle_task_tcb;
static StackType_t idle_task_stack[configMINIMAL_STACK_SIZE];

extern "C" void vApplicationGetIdleTaskMemory(StaticTask_t** ppxIdleTaskTCBBuffer,
                                              StackType_t** ppxIdleTaskStackBuffer,
                                              uint32_t* pulIdleTaskStackSize)
{
    *ppxIdleTaskTCBBuffer = &idle_task_tcb;
    *ppxIdleTaskStackBuffer = idle_task_stack;
    *pulIdleTaskStackSize = configMINIMAL_STACK_SIZE;
}

static StaticTask_t timer_task_tcb;
static StackType_t timer_task_stack[configTIMER_TASK_STACK_DEPTH];

extern "C" void vApplicationGetTimerTaskMemory(StaticTask_t** ppxTimerTaskTCBBuffer,
                                               StackType_t** ppxTimerTaskStackBuffer,
                                               uint32_t* pulTimerTaskStackSize)
{
    *ppxTimerTaskTCBBuffer = &timer_task_tcb;
    *ppxTimerTaskStackBuffer = timer_task_stack;
    *pulTimerTaskStackSize = configTIMER_TASK_STACK_DEPTH;
}

#include <arch/arm/cortex_m/fault.h>

extern "C" void vApplicationStackOverflowHook(TaskHandle_t, char *pcTaskName)
{
    hal::fault::panic(hal::fault::FatalReason::StackOverflow, 0,
                      pcTaskName != nullptr ? pcTaskName : "?", 0U);
}

extern "C" void vApplicationMallocFailedHook()
{
    hal::fault::panic(hal::fault::FatalReason::AllocationFailure, 0,
                      "freertos_heap", 0U);
}

extern "C" void osal_freertos_assert_failed(const char *file,
                                               unsigned int line)
{
    hal::fault::panic(hal::fault::FatalReason::KernelAssert,
                      static_cast<int32_t>(line), file, line);
}
