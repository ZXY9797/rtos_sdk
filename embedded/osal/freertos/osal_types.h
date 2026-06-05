#pragma once

#include <FreeRTOS.h>
#include <task.h>
#include <semphr.h>
#include <cstdint>

#define OSAL_WAITING_FOREVER    portMAX_DELAY

struct osal_thread { TaskHandle_t handle; uint32_t flags; };
typedef struct osal_thread osal_thread_t;
typedef SemaphoreHandle_t osal_mutex_t;
typedef SemaphoreHandle_t osal_sem_t;

static inline int osal_sleep(int ms) {
    vTaskDelay(pdMS_TO_TICKS(ms));
    return 0;
}

static inline int osal_thread_yield(void) {
    taskYIELD();
    return 0;
}

static inline void osal_interrupt_enter(void) {}
static inline void osal_interrupt_leave(void) {}
static inline void sys_clock_announce(uint32_t ticks) { (void)ticks; }

// ─── 堆封装 ───────────────────────────────────────────────────────

static inline void *rtos_malloc(size_t size) { return pvPortMalloc(size); }
static inline void  rtos_free(void *ptr) { if (ptr) vPortFree(ptr); }
