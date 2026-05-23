/*
 * Copyright (c) 2016, Wind River Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_INCLUDE_KERNEL_H_
#define ZEPHYR_INCLUDE_KERNEL_H_

#include <zephyr/kernel_includes.h>
#include <errno.h>
#include <limits.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Priority definitions */
#define K_PRIO_COOP(x) (-(CONFIG_NUM_COOP_PRIORITIES - (x)))
#define K_PRIO_PREEMPT(x) (x)

#define K_HIGHEST_THREAD_PRIO (-CONFIG_NUM_COOP_PRIORITIES)
#define K_LOWEST_THREAD_PRIO CONFIG_NUM_PREEMPT_PRIORITIES
#define K_IDLE_PRIO K_LOWEST_THREAD_PRIO

/* Timeout macros - must be before functions that use them */
#define K_NO_WAIT   Z_TIMEOUT_NO_WAIT
#define K_FOREVER   Z_TIMEOUT_TICKS((k_ticks_t) -1)
#define K_MSEC(ms)  Z_TIMEOUT_TICKS((k_ticks_t)(ms) * CONFIG_SYS_CLOCK_TICKS_PER_SEC / 1000)
#define K_SEC(s)    Z_TIMEOUT_TICKS((k_ticks_t)(s) * CONFIG_SYS_CLOCK_TICKS_PER_SEC)
#define K_USEC(us)  Z_TIMEOUT_TICKS((k_ticks_t)(us) * CONFIG_SYS_CLOCK_TICKS_PER_SEC / 1000000)

/* Forward declarations */
struct k_thread;
struct k_mutex;
struct k_sem;

/**
 * @brief Thread APIs
 */

k_tid_t k_thread_create(struct k_thread *new_thread,
                        k_thread_stack_t *stack,
                        size_t stack_size,
                        k_thread_entry_t entry,
                        void *p1, void *p2, void *p3,
                        int prio, uint32_t options,
                        k_timeout_t delay);

int k_thread_start(k_tid_t thread);
void k_thread_abort(k_tid_t thread);
int32_t k_sleep(k_timeout_t duration);
void k_yield(void);
k_tid_t k_current_get(void);
int k_thread_priority_get(k_tid_t thread);
void k_thread_priority_set(k_tid_t thread, int prio);

static inline int32_t k_msleep(int32_t ms)
{
    return k_sleep(K_MSEC(ms));
}

/**
 * @brief Semaphore APIs
 */

struct k_sem {
    struct _wait_q wait_q;
    unsigned int count;
    unsigned int limit;
};

int k_sem_init(struct k_sem *sem, unsigned int initial_count, unsigned int limit);
int k_sem_take(struct k_sem *sem, k_timeout_t timeout);
void k_sem_give(struct k_sem *sem);
unsigned int k_sem_count_get(struct k_sem *sem);

/**
 * @brief Mutex APIs
 */

struct k_mutex {
    struct _wait_q wait_q;
    struct k_thread *owner;
    unsigned int lock_count;
    int owner_orig_prio;
};

int k_mutex_init(struct k_mutex *mutex);
int k_mutex_lock(struct k_mutex *mutex, k_timeout_t timeout);
int k_mutex_unlock(struct k_mutex *mutex);

/**
 * @brief Memory Allocation APIs
 */

struct k_heap {
    void *mem;
    size_t size;
};

void *k_heap_alloc(struct k_heap *heap, size_t bytes, k_timeout_t timeout);
void k_heap_free(struct k_heap *heap, void *mem);
void *k_malloc(size_t bytes);
void k_free(void *mem);
void *k_calloc(size_t nmemb, size_t size);

/* Macro to define a static k_heap with a static buffer */
#define K_HEAP_DEFINE(name, bytes) \
    static char _kheap_mem_##name[bytes]; \
    static struct k_heap name = { \
        .mem = _kheap_mem_##name, \
        .size = bytes, \
    }

/**
 * @brief Interrupt APIs
 */

unsigned int irq_lock(void);
void irq_unlock(unsigned int key);
void irq_disable(unsigned int irq);
void irq_enable(unsigned int irq);
bool k_is_in_isr(void);
bool k_is_preempt_thread(void);

/**
 * @brief Spinlock APIs
 */

struct k_spinlock {
    unsigned int key;
};

typedef struct {
    unsigned int key;
} k_spinlock_key_t;

k_spinlock_key_t k_spin_lock(struct k_spinlock *l);
void k_spin_unlock(struct k_spinlock *l, k_spinlock_key_t key);

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_INCLUDE_KERNEL_H_ */
