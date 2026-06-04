#ifndef RTOS_SDK_OSAL_TYPE_H_
#define RTOS_SDK_OSAL_TYPE_H_

#include <FreeRTOS.h>
#include <task.h>
#include <semphr.h>
#include <stdint.h>

struct osal_thread {
    TaskHandle_t handle;
    uint32_t flags;
};

typedef struct osal_thread osal_thread_t;

typedef SemaphoreHandle_t osal_mutex_t;

typedef SemaphoreHandle_t osal_sem_t;

#endif
