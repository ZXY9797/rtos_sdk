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
void osal_yield_from_isr(int reschedule);
void sys_clock_announce(uint32_t ticks);

void *rtos_malloc(size_t size);
void rtos_free(void *ptr);

namespace osal {
inline constexpr uint32_t kSemaphoreMaxCount = 0xFFFFU;
inline constexpr uint32_t kMinRtosCallableIrqPriority = 0U;
inline constexpr uint32_t kLowestIrqPriority =
    (1UL << CONFIG_NUM_IRQ_PRIO_BITS) - 1UL;
inline constexpr uint8_t kPriorityMax = 31U;
inline constexpr size_t kDefaultThreadStackBytes = 1024U;
inline constexpr size_t kMessageQueueControlBytes = 1U;
inline constexpr size_t kSemaphoreControlBytes = 1U;
inline constexpr size_t kMutexControlBytes = 1U;
inline constexpr size_t kMessageQueueItemAlignment = 1U;
inline constexpr size_t kMessageQueueItemOverhead = 0U;
inline constexpr size_t kStreamBufferControlBytes = 1U;
} // namespace osal
