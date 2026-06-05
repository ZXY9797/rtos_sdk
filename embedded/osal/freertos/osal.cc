#include <osal.h>
#include <cstdint>
#include <cstddef>

#define MAIN_THREAD_STACK_SIZE  (CONFIG_MAIN_STACK_SIZE / sizeof(StackType_t))

// ─── 系统启动 ──────────────────────────────────────────────────────

static TaskHandle_t main_task_handle;

static void main_task_entry(void *parameter)
{
    auto entry = reinterpret_cast<void (*)(void *)>(parameter);
    entry(nullptr);
    vTaskDelete(nullptr);
    for (;;) {}
}

int osal_init(void)
{
    return 0;
}

int osal_start(void (*entry)(void *parameter), void *parameter)
{
    (void)parameter;
    BaseType_t ret = xTaskCreate(main_task_entry, "main",
                                  MAIN_THREAD_STACK_SIZE,
                                  reinterpret_cast<void *>(entry),
                                  configMAX_PRIORITIES / 3,
                                  &main_task_handle);
    if (ret != pdPASS) {
        return -1;
    }

    vTaskStartScheduler();

    /* never reach here */
    for (;;) {}
    return 0;
}

// ─── Semaphore ─────────────────────────────────────────────────────

namespace osal {

Semaphore::Semaphore(uint32_t initial)
    : handle_(xSemaphoreCreateCounting(0xFFFF, initial)) {}

Semaphore::~Semaphore() {
    if (handle_) vSemaphoreDelete(handle_);
}

int Semaphore::take(uint32_t timeout) {
    return (xSemaphoreTake(handle_, timeout) == pdTRUE) ? 0 : -1;
}

int Semaphore::release() {
    return (xSemaphoreGive(handle_) == pdTRUE) ? 0 : -1;
}

// ─── Mutex ─────────────────────────────────────────────────────────

Mutex::Mutex()
    : handle_(xSemaphoreCreateMutex()) {}

Mutex::~Mutex() {
    if (handle_) vSemaphoreDelete(handle_);
}

int Mutex::lock(uint32_t timeout) {
    return (xSemaphoreTake(handle_, timeout) == pdTRUE) ? 0 : -1;
}

int Mutex::tryLock() {
    return (xSemaphoreTake(handle_, 0) == pdTRUE) ? 0 : -1;
}

int Mutex::unlock() {
    return (xSemaphoreGive(handle_) == pdTRUE) ? 0 : -1;
}

// ─── Thread ────────────────────────────────────────────────────────

Thread::Thread(const char* name, void (*entry)(void*), void* param,
               void* stack, uint32_t stack_size, uint8_t prio, uint32_t tick)
    : handle_{}, owned_(false) {
    (void)tick;
    handle_.handle = xTaskCreateStatic(entry, name,
                                        stack_size / sizeof(StackType_t),
                                        param,
                                        prio,
                                        static_cast<StackType_t *>(stack),
                                        nullptr);
}

Thread::~Thread() {
    if (owned_ && handle_.handle) {
        vTaskDelete(handle_.handle);
    }
}

Thread* Thread::create(const char* name, void (*entry)(void*),
                        void* param, size_t stack_size,
                        int32_t prio, int32_t tick) {
    (void)tick;
    auto *thread = new Thread(PrivateTag{});
    thread->owned_ = true;

    TaskHandle_t h = nullptr;
    BaseType_t ret = xTaskCreate(entry, name,
                                  stack_size / sizeof(StackType_t),
                                  param,
                                  prio,
                                  &h);
    if (ret != pdPASS) {
        delete thread;
        return nullptr;
    }
    thread->handle_.handle = h;
    return thread;
}

int Thread::startup() {
    vTaskResume(handle_.handle);
    return 0;
}

int Thread::suspend() {
    vTaskSuspend(handle_.handle);
    return 0;
}

int Thread::resume() {
    vTaskResume(handle_.handle);
    return 0;
}

int Thread::yield() {
    taskYIELD();
    return 0;
}

} // namespace osal

// ─── FreeRTOS 静态分配回调 ────────────────────────────────────────

static StaticTask_t idle_task_tcb;
static StackType_t idle_task_stack[configMINIMAL_STACK_SIZE];

extern "C" void vApplicationGetIdleTaskMemory(StaticTask_t **ppxIdleTaskTCBBuffer,
                                   StackType_t **ppxIdleTaskStackBuffer,
                                   uint32_t *pulIdleTaskStackSize)
{
    *ppxIdleTaskTCBBuffer = &idle_task_tcb;
    *ppxIdleTaskStackBuffer = idle_task_stack;
    *pulIdleTaskStackSize = configMINIMAL_STACK_SIZE;
}

static StaticTask_t timer_task_tcb;
static StackType_t timer_task_stack[configTIMER_TASK_STACK_DEPTH];

extern "C" void vApplicationGetTimerTaskMemory(StaticTask_t **ppxTimerTaskTCBBuffer,
                                    StackType_t **ppxTimerTaskStackBuffer,
                                    uint32_t *pulTimerTaskStackSize)
{
    *ppxTimerTaskTCBBuffer = &timer_task_tcb;
    *ppxTimerTaskStackBuffer = timer_task_stack;
    *pulTimerTaskStackSize = configTIMER_TASK_STACK_DEPTH;
}
