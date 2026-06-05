#pragma once

#include "rtdef.h"
#include <rtthread.h>
#include <cstddef>
#include <cstdint>

#define OSAL_WAITING_FOREVER    RT_WAITING_FOREVER

struct osal_thread { struct rt_thread tcb; rt_thread_t handle; uint32_t flags; };
typedef struct osal_thread osal_thread_t;
typedef struct rt_mutex *osal_mutex_t;
typedef struct rt_semaphore *osal_sem_t;

static inline int osal_sleep(int ms) {
    return rt_thread_mdelay(ms);
}

static inline int osal_thread_yield(void) {
    return (int)rt_thread_yield();
}

static inline void osal_interrupt_enter(void) { rt_interrupt_enter(); }
static inline void osal_interrupt_leave(void) { rt_interrupt_leave(); }
static inline void sys_clock_announce(uint32_t ticks) { rt_tick_increase(); }

// ─── 堆封装 ───────────────────────────────────────────────────────

static inline void *rtos_malloc(size_t size) { return rt_malloc(size); }
static inline void  rtos_free(void *ptr) { if (ptr) rt_free(ptr); }
