#ifndef MSDK_OSAL_H_
#define MSDK_OSAL_H_

#include "rtdef.h"
#include <rtthread.h>
#include <toolchain.h>
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define OSAL_IPC_FLAG_FIFO     RT_IPC_FLAG_FIFO
#define OSAL_IPC_FLAG_PRIO     RT_IPC_FLAG_PRIO

#define OSAL_WAITING_FOREVER    RT_WAITING_FOREVER

/** 线程相关 */
struct osal_thread {
    struct rt_thread tcb;
    uint32_t flags;
};

typedef struct osal_thread osal_thread_t;

int osal_thread_init(osal_thread_t *thread, const char *name,
                     void (*entry)(void *parameter), void *parameter,
                     void *stack_start, uint32_t stack_size, uint8_t priority,
                     uint32_t tick);

osal_thread_t *osal_thread_create( const char *name,
                        void (*entry)(void *parameter),
                        void *parameter,
                        size_t stack_size,
                        int32_t priority,
                        int32_t tick);

static ALWAYS_INLINE int osal_sleep(int ms) {
    return rt_thread_mdelay(ms);
}

static ALWAYS_INLINE int osal_thread_startup(osal_thread_t *thread) {
    return (int)rt_thread_startup(&thread->tcb);
}

static ALWAYS_INLINE int osal_thread_suspend(osal_thread_t *thread) {
    return (int)rt_thread_resume(&thread->tcb);
}

static ALWAYS_INLINE int osal_thread_resume(osal_thread_t *thread) {
    return (int)rt_thread_resume(&thread->tcb);
}

static ALWAYS_INLINE int osal_thread_yield(void) {
    return (int)rt_thread_yield();
}

typedef struct rt_mutex osal_mutex_t;

int osal_mutex_init(osal_mutex_t *mutex, const char *name, uint8_t flag);

osal_mutex_t *osal_mutex_create(const char *name, uint8_t flag);

static ALWAYS_INLINE int osal_mutex_take(osal_mutex_t *mutex, uint32_t timeout) {
    return (int)rt_mutex_take(mutex, timeout);
}

static ALWAYS_INLINE int osal_mutex_trytake(osal_mutex_t *mutex) {
    return (int)rt_mutex_trytake(mutex);
}

static ALWAYS_INLINE int osal_mutex_release(osal_mutex_t *mutex) {
    return (int)rt_mutex_release(mutex);
}

typedef struct rt_semaphore osal_sem_t;

static ALWAYS_INLINE void osal_interrupt_enter(void) {
    rt_interrupt_enter();
}

static ALWAYS_INLINE void osal_interrupt_leave(void) {
    rt_interrupt_leave();
}

static ALWAYS_INLINE void sys_clock_announce(uint32_t ticks) {
    rt_tick_increase();
}

int osal_init(void);
int osal_start(void (*entry)(void *parameter), void *parameter);



#ifdef __cplusplus
}
#endif

#endif
