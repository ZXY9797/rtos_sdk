#ifndef MSDK_OSAL_H_
#define MSDK_OSAL_H_

#include <FreeRTOS.h>
#include <task.h>
#include <semphr.h>
#include <osal_type.h>
#include <toolchain.h>
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define OSAL_IPC_FLAG_FIFO     0
#define OSAL_IPC_FLAG_PRIO     1

#define OSAL_WAITING_FOREVER    portMAX_DELAY

/** Thread */
int osal_thread_init(osal_thread_t *thread, const char *name,
                     void (*entry)(void *parameter), void *parameter,
                     void *stack_start, uint32_t stack_size, uint8_t priority,
                     uint32_t tick);

osal_thread_t *osal_thread_create(const char *name,
                        void (*entry)(void *parameter),
                        void *parameter,
                        size_t stack_size,
                        int32_t priority,
                        int32_t tick);

static ALWAYS_INLINE int osal_sleep(int ms) {
    vTaskDelay(pdMS_TO_TICKS(ms));
    return 0;
}

static ALWAYS_INLINE int osal_thread_startup(osal_thread_t *thread) {
    vTaskResume(thread->handle);
    return 0;
}

static ALWAYS_INLINE int osal_thread_suspend(osal_thread_t *thread) {
    vTaskSuspend(thread->handle);
    return 0;
}

static ALWAYS_INLINE int osal_thread_resume(osal_thread_t *thread) {
    vTaskResume(thread->handle);
    return 0;
}

static ALWAYS_INLINE int osal_thread_yield(void) {
    taskYIELD();
    return 0;
}

/** Mutex */
int osal_mutex_init(osal_mutex_t *mutex, const char *name, uint8_t flag);

osal_mutex_t *osal_mutex_create(const char *name, uint8_t flag);

static ALWAYS_INLINE int osal_mutex_take(osal_mutex_t *mutex, uint32_t timeout) {
    return (xSemaphoreTake(*mutex, timeout) == pdTRUE) ? 0 : -1;
}

static ALWAYS_INLINE int osal_mutex_trytake(osal_mutex_t *mutex) {
    return (xSemaphoreTake(*mutex, 0) == pdTRUE) ? 0 : -1;
}

static ALWAYS_INLINE int osal_mutex_release(osal_mutex_t *mutex) {
    return (xSemaphoreGive(*mutex) == pdTRUE) ? 0 : -1;
}

/** ISR notification */
static ALWAYS_INLINE void osal_interrupt_enter(void) {
}

static ALWAYS_INLINE void osal_interrupt_leave(void) {
}

static ALWAYS_INLINE void sys_clock_announce(uint32_t ticks) {
    (void)ticks;
}

int osal_init(void);
int osal_start(void (*entry)(void *parameter), void *parameter);

#ifdef __cplusplus
}
#endif

#endif
