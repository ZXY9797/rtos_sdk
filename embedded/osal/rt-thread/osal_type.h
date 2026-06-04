#ifndef RTOS_SDK_OSAL_TYPE_H_
#define RTOS_SDK_OSAL_TYPE_H_
#include <rtthread.h>
#include <stdint.h>

struct osal_thread {
    struct rt_thread tcb;
    uint32_t flags;
};

typedef struct osal_thread osal_thread_t;

typedef struct rt_mutex osal_mutex_t;

typedef struct rt_semaphore osal_sem_t;

#endif
