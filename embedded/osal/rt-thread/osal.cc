#include "rtdef.h"
#include "sys/util.h"
#include <rtthread.h>
#include <rthw.h>
#include <osal.h>
#include <cstdint>
#include <cstddef>
#include <mmem.h>

/* if there is not enable heap, we should use static thread and stack. */
ALIGN(8)
static rt_uint8_t main_stack[RT_MAIN_THREAD_STACK_SIZE];
struct rt_thread main_thread;

#ifdef RT_USING_HEAP
static rt_uint8_t rt_heap_pool[32 * 1024];
#endif

int osal_init()
{
    rt_hw_interrupt_disable();

#ifdef RT_USING_HEAP
    /* heap initialization */
    rt_system_heap_init(rt_heap_pool, rt_heap_pool + sizeof(rt_heap_pool));
#endif

    /* timer system initialization */
    rt_system_timer_init();

    /* scheduler system initialization */
    rt_system_scheduler_init();

#ifdef RT_USING_SIGNALS
    /* signal system initialization */
    rt_system_signal_init();
#endif /* RT_USING_SIGNALS */

    /* timer thread initialization */
    rt_system_timer_thread_init();

    /* idle thread initialization */
    rt_thread_idle_init();

#ifdef RT_USING_SMP
    rt_hw_spin_lock(&_cpus_lock);
#endif /* RT_USING_SMP */

    return 0;
}

extern int cpu_usage_init(void);

int osal_start(void (*entry)(void *parameter), void *parameter)
{
    rt_err_t result;

    rt_thread_t tid = &main_thread;
    result = rt_thread_init(tid, "main", entry, parameter,
                            main_stack, sizeof(main_stack), RT_MAIN_THREAD_PRIORITY, 20);
    RT_ASSERT(result == RT_EOK);

    /* if not define RT_USING_HEAP, using to eliminate the warning */
    (void)result;

    rt_thread_startup(tid);

    cpu_usage_init();

    /* start scheduler */
    rt_system_scheduler_start();

    /* never reach here */
    return 0;
}

int osal_thread_init(struct osal_thread *thread, const char *name,
                     void (*entry)(void *parameter), void *parameter,
                     void *stack_start, uint32_t stack_size, uint8_t priority,
                     uint32_t tick) {
    return static_cast<int>(rt_thread_init(&(thread->tcb), name, entry, parameter,
                             stack_start, stack_size, priority, tick));
}

osal_thread_t *osal_thread_create(const char *name,
                        void (*entry)(void *parameter),
                        void *parameter,
                        size_t stack_size,
                        int32_t priority,
                        int32_t tick)
{
    rt_err_t result;
    osal_thread_t *thread = nullptr;
    uint8_t *stack = nullptr;

    thread = static_cast<osal_thread_t *>(mmem_caps_aligned_alloc(4, sizeof(osal_thread_t), MEM_CAP_HIGHSPEED));
    if (thread == nullptr) {
        goto err_exit;
    }
    stack = static_cast<uint8_t *>(mmem_caps_aligned_alloc(8, stack_size, MEM_CAP_HIGHSPEED));
    if (stack == nullptr) {
        goto err_exit;
    }
    result = rt_thread_init(&(thread->tcb), name, entry, parameter,
                                stack, stack_size, priority, tick);
    if (result != RT_EOK) {
        goto err_exit;
    }
    return thread;
err_exit:
    if (thread) {
        mmem_aligned_free(thread);
    }
    if (stack) {
        mmem_aligned_free(stack);
    }
    return nullptr;
}

int osal_mutex_init(osal_mutex_t *mutex, const char *name, uint8_t flag)
{
    return static_cast<int>(rt_mutex_init(mutex, name, flag));
}

osal_mutex_t *osal_mutex_create(const char *name, uint8_t flag)
{
    rt_err_t result;
    osal_mutex_t *mutex = nullptr;

    mutex = static_cast<osal_mutex_t *>(mmem_caps_aligned_alloc(4, sizeof(osal_mutex_t), MEM_CAP_HIGHSPEED));
    if (mutex == nullptr) {
        goto err_exit;
    }

    result = rt_mutex_init(mutex, name, flag);
    if (result != RT_EOK) {
        goto err_exit;
    }

    return mutex;
err_exit:
    if (mutex) {
        mmem_aligned_free(mutex);
    }
    return nullptr;
}
