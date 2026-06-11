#pragma once

#include <cstddef>
#include <cstdint>

struct osal_bare_sem;
struct osal_bare_mutex;

struct osal_thread {
    void *handle;
    uint32_t flags;
};

using osal_thread_t = osal_thread;
using osal_mutex_t = osal_bare_mutex *;
using osal_sem_t = osal_bare_sem *;

int osal_sleep(int ms);
int osal_thread_yield(void);
void osal_interrupt_enter(void);
void osal_interrupt_leave(void);
void sys_clock_announce(uint32_t ticks);

void *rtos_malloc(size_t size);
void rtos_free(void *ptr);

namespace osal {
inline constexpr uint32_t kSemaphoreMaxCount = 0xFFFFU;
inline constexpr uint8_t kPriorityMax = 31U;
inline constexpr size_t kDefaultThreadStackBytes = 1024U;
} // namespace osal

