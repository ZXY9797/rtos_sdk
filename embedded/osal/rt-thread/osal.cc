#include "rtdef.h"
#include "sys/util.h"
#include <rtthread.h>
#include <rthw.h>
#include <osal.h>
#include <cstdint>
#include <cstddef>
#include <mem.h>

// ─── 系统启动 ──────────────────────────────────────────────────────

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
    rt_system_heap_init(rt_heap_pool, rt_heap_pool + sizeof(rt_heap_pool));
#endif

    rt_system_timer_init();
    rt_system_scheduler_init();

#ifdef RT_USING_SIGNALS
    rt_system_signal_init();
#endif

    rt_system_timer_thread_init();
    rt_thread_idle_init();

#ifdef RT_USING_SMP
    rt_hw_spin_lock(&_cpus_lock);
#endif

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

    (void)result;

    rt_thread_startup(tid);

    cpu_usage_init();

    rt_system_scheduler_start();

    /* never reach here */
    return 0;
}

// ─── Semaphore ─────────────────────────────────────────────────────

namespace osal {

Semaphore::Semaphore(uint32_t initial)
    : handle_(rt_sem_create("sem", initial, RT_IPC_FLAG_FIFO)) {}

Semaphore::~Semaphore() {
    if (handle_) rt_sem_delete(handle_);
}

int Semaphore::take(uint32_t timeout) {
    return (rt_sem_take(handle_, timeout) == RT_EOK) ? 0 : -1;
}

int Semaphore::release() {
    return (rt_sem_release(handle_) == RT_EOK) ? 0 : -1;
}

// ─── Mutex ─────────────────────────────────────────────────────────

Mutex::Mutex()
    : handle_(rt_mutex_create("mtx", RT_IPC_FLAG_FIFO)) {}

Mutex::~Mutex() {
    if (handle_) rt_mutex_delete(handle_);
}

int Mutex::lock(uint32_t timeout) {
    return (rt_mutex_take(handle_, timeout) == RT_EOK) ? 0 : -1;
}

int Mutex::tryLock() {
    return (rt_mutex_take(handle_, 0) == RT_EOK) ? 0 : -1;
}

int Mutex::unlock() {
    return (rt_mutex_release(handle_) == RT_EOK) ? 0 : -1;
}

// ─── Thread ────────────────────────────────────────────────────────

Thread::Thread(const char* name, void (*entry)(void*), void* param,
               void* stack, uint32_t stack_size, uint8_t prio, uint32_t tick)
    : handle_{}, owned_(false) {
    rt_thread_init(&handle_.tcb, name, entry, param,
                   stack, stack_size, prio, tick);
    handle_.handle = &handle_.tcb;
}

Thread::~Thread() {
    if (owned_ && handle_.handle) {
        rt_thread_delete(handle_.handle);
    }
}

Thread* Thread::create(const char* name, void (*entry)(void*),
                        void* param, size_t stack_size,
                        int32_t prio, int32_t tick) {
    auto *thread = new Thread(PrivateTag{});

    rt_thread_t h = rt_thread_create(name, entry, param,
                                      stack_size, prio, tick);
    if (!h) {
        delete thread;
        return nullptr;
    }
    thread->handle_.handle = h;
    return thread;
}

int Thread::startup() {
    return (int)rt_thread_startup(handle_.handle);
}

int Thread::suspend() {
    return (int)rt_thread_suspend(handle_.handle);
}

int Thread::resume() {
    return (int)rt_thread_resume(handle_.handle);
}

int Thread::yield() {
    return (int)rt_thread_yield();
}

} // namespace osal
