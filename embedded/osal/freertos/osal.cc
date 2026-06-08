#include <mem.h>
#include <osal.h>

#include <event_groups.h>
#include <queue.h>
#include <timers.h>

#include <cstddef>
#include <cstdint>

#define MAIN_THREAD_STACK_SIZE  (CONFIG_MAIN_STACK_SIZE / sizeof(StackType_t))

static TaskHandle_t main_task_handle;
static void (*g_user_entry)(void);

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
    g_user_entry = entry;
    BaseType_t ret = xTaskCreate(main_task_entry, "main",
                                 MAIN_THREAD_STACK_SIZE,
                                 nullptr,
                                 configMAX_PRIORITIES / 3,
                                 &main_task_handle);
    if (ret != pdPASS) {
        return -1;
    }

    vTaskStartScheduler();

    for (;;) {
    }
    return 0;
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

UBaseType_t stack_depth_from_bytes(size_t stack_size_bytes)
{
    if (stack_size_bytes < sizeof(StackType_t)) {
        return 0U;
    }
    return static_cast<UBaseType_t>(
        (stack_size_bytes + sizeof(StackType_t) - 1U) / sizeof(StackType_t));
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
    handle_ = xSemaphoreCreateCounting(native_max, native_initial);
    if (handle_ == nullptr) {
        max_count_ = 0U;
    }
}

Semaphore::~Semaphore()
{
    if (handle_ != nullptr) {
        vSemaphoreDelete(handle_);
        handle_ = nullptr;
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
    handle_ = xSemaphoreCreateMutex();
    return handle_ != nullptr;
}

void Mutex::destroy()
{
    if (handle_ != nullptr) {
        vSemaphoreDelete(handle_);
        handle_ = nullptr;
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
    const UBaseType_t stack_depth = stack_depth_from_bytes(config.stack_size_bytes);
    if (stack_depth == 0U) {
        return false;
    }

    if (config.stack_buffer != nullptr) {
        handle_.handle = xTaskCreateStatic(entry, name, stack_depth, context,
                                           config.priority,
                                           static_cast<StackType_t*>(config.stack_buffer),
                                           &handle_.tcb);
        if (handle_.handle == nullptr) {
            return false;
        }
        handle_.flags = kThreadFlagStatic;
    } else {
        TaskHandle_t h = nullptr;
        const BaseType_t ret = xTaskCreate(entry, name, stack_depth, context,
                                           config.priority, &h);
        if (ret != pdPASS || h == nullptr) {
            return false;
        }
        handle_.handle = h;
        handle_.flags = kThreadFlagDynamic;
    }

    vTaskSuspend(handle_.handle);
    return true;
}

void Thread::destroy()
{
    if (handle_.handle != nullptr) {
        vTaskDelete(handle_.handle);
        handle_.handle = nullptr;
        handle_.flags = 0U;
    }
}

int Thread::startup()
{
    if (handle_.handle == nullptr) {
        return -1;
    }
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

void PeriodicThread::threadEntry(void* parameter)
{
    auto* thread = static_cast<PeriodicThread*>(parameter);

    for (;;) {
        while (!thread->running_) {
            vTaskSuspend(nullptr);
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
}

PeriodicThread::PeriodicThread(PrivateTag)
{
}

PeriodicThread::~PeriodicThread()
{
    (void)stop();
    if (thread_.handle != nullptr) {
        vTaskDelete(thread_.handle);
        thread_.handle = nullptr;
    }
    if (sem_ != nullptr) {
        vSemaphoreDelete(sem_);
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
    if (entry == nullptr || frequency_hz == 0U || stack_size < sizeof(StackType_t)) {
        return nullptr;
    }
    if (trigger == PeriodicTrigger::Tick && frequency_hz > configTICK_RATE_HZ) {
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
        thread->sem_ = xSemaphoreCreateCounting(1U, 0U);
        if (thread->sem_ == nullptr) {
            delete thread;
            return nullptr;
        }
    }

    TaskHandle_t h = nullptr;
    const BaseType_t ret = xTaskCreate(PeriodicThread::threadEntry, name,
                                       stack_depth_from_bytes(stack_size),
                                       thread, prio, &h);
    if (ret != pdPASS || h == nullptr) {
        delete thread;
        return nullptr;
    }

    thread->thread_.handle = h;
    thread->thread_.flags = kThreadFlagDynamic;
    vTaskSuspend(h);
    return thread;
}

int PeriodicThread::startup()
{
    if (thread_.handle == nullptr) {
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
        const size_t required = static_cast<size_t>(length) * item_size;
        if (storage_buffer_size_bytes < required) {
            return false;
        }

        auto* control = static_cast<StaticQueue_t*>(rtos_malloc(sizeof(StaticQueue_t)));
        if (control == nullptr) {
            return false;
        }

        QueueHandle_t queue = xQueueCreateStatic(length, item_size,
                                                 static_cast<uint8_t*>(storage_buffer),
                                                 control);
        if (queue == nullptr) {
            rtos_free(control);
            return false;
        }

        native_handle_ = queue;
        control_block_buffer_ = control;
        owns_control_block_buffer_ = true;
        return true;
    }

    QueueHandle_t queue = xQueueCreate(length, item_size);
    if (queue == nullptr) {
        return false;
    }

    native_handle_ = queue;
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
    owns_control_block_buffer_ = false;
}

bool MessageQueue::send(const void* item, Milliseconds timeout_ms)
{
    auto queue = static_cast<QueueHandle_t>(native_handle_);
    return queue != nullptr && item != nullptr &&
           xQueueSend(queue, item, timeout_ms_to_ticks(timeout_ms)) == pdTRUE;
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
    hal::fault::print("\nSTACK OVERFLOW: ");
    hal::fault::print(pcTaskName ? pcTaskName : "?");
    hal::fault::putc('\n');
    for (;;) { __asm__("cpsid i"); __asm__("wfi"); }
}

extern "C" void vApplicationMallocFailedHook()
{
    hal::fault::print("\nMALLOC FAILED\n");
    for (;;) { __asm__("cpsid i"); __asm__("wfi"); }
}
