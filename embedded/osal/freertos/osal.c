#include <osal.h>
#include <soc.h>
#include <stdint.h>
#include <stddef.h>

#define MAIN_THREAD_STACK_SIZE  (CONFIG_MAIN_STACK_SIZE / sizeof(StackType_t))

static TaskHandle_t main_task_handle;

static void main_task_entry(void *parameter)
{
    void (*entry)(void *) = (void (*)(void *))parameter;
    entry(NULL);
    vTaskDelete(NULL);
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
                                  (void *)entry,
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

int osal_thread_init(osal_thread_t *thread, const char *name,
                     void (*entry)(void *parameter), void *parameter,
                     void *stack_start, uint32_t stack_size, uint8_t priority,
                     uint32_t tick)
{
    (void)tick;
    (void)name;
    /* Static task creation - stack_start must be a StackType_t array */
    thread->handle = xTaskCreateStatic(entry, name,
                                        stack_size / sizeof(StackType_t),
                                        parameter,
                                        priority,
                                        (StackType_t *)stack_start,
                                        NULL);
    return (thread->handle != NULL) ? 0 : -1;
}

osal_thread_t *osal_thread_create(const char *name,
                        void (*entry)(void *parameter),
                        void *parameter,
                        size_t stack_size,
                        int32_t priority,
                        int32_t tick)
{
    (void)tick;
    osal_thread_t *thread = pvPortMalloc(sizeof(osal_thread_t));
    if (thread == NULL) {
        return NULL;
    }

    thread->flags = 0;
    BaseType_t ret = xTaskCreate(entry, name,
                                  stack_size / sizeof(StackType_t),
                                  parameter,
                                  priority,
                                  &thread->handle);
    if (ret != pdPASS) {
        vPortFree(thread);
        return NULL;
    }

    return thread;
}

int osal_mutex_init(osal_mutex_t *mutex, const char *name, uint8_t flag)
{
    (void)name;
    (void)flag;
    *mutex = xSemaphoreCreateMutex();
    return (*mutex != NULL) ? 0 : -1;
}

osal_mutex_t *osal_mutex_create(const char *name, uint8_t flag)
{
    (void)name;
    (void)flag;
    osal_mutex_t *mutex = pvPortMalloc(sizeof(osal_mutex_t));
    if (mutex == NULL) {
        return NULL;
    }
    *mutex = xSemaphoreCreateMutex();
    if (*mutex == NULL) {
        vPortFree(mutex);
        return NULL;
    }
    return mutex;
}

/* FreeRTOS static allocation callbacks */
static StaticTask_t idle_task_tcb;
static StackType_t idle_task_stack[configMINIMAL_STACK_SIZE];

void vApplicationGetIdleTaskMemory(StaticTask_t **ppxIdleTaskTCBBuffer,
                                   StackType_t **ppxIdleTaskStackBuffer,
                                   uint32_t *pulIdleTaskStackSize)
{
    *ppxIdleTaskTCBBuffer = &idle_task_tcb;
    *ppxIdleTaskStackBuffer = idle_task_stack;
    *pulIdleTaskStackSize = configMINIMAL_STACK_SIZE;
}

static StaticTask_t timer_task_tcb;
static StackType_t timer_task_stack[configTIMER_TASK_STACK_DEPTH];

void vApplicationGetTimerTaskMemory(StaticTask_t **ppxTimerTaskTCBBuffer,
                                    StackType_t **ppxTimerTaskStackBuffer,
                                    uint32_t *pulTimerTaskStackSize)
{
    *ppxTimerTaskTCBBuffer = &timer_task_tcb;
    *ppxTimerTaskStackBuffer = timer_task_stack;
    *pulTimerTaskStackSize = configTIMER_TASK_STACK_DEPTH;
}
