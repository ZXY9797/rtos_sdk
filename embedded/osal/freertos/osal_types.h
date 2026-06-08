#pragma once

#include <FreeRTOS.h>
#include <task.h>
#include <semphr.h>
#include <cstdint>

struct osal_thread {
    TaskHandle_t handle;
    StaticTask_t tcb;
    uint32_t flags;
};
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

// ─── OSAL 常量 ────────────────────────────────────────────────────

namespace osal {
inline constexpr uint32_t kSemaphoreMaxCount = 0xFFFFU;
inline constexpr uint8_t  kPriorityMax =
    static_cast<uint8_t>((configMAX_PRIORITIES > 0) ? (configMAX_PRIORITIES - 1) : 0);
inline constexpr size_t   kDefaultThreadStackBytes = 1024U;
}  // namespace osal
