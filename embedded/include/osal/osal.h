#pragma once

#include <osal_types.h>
#include <cstddef>

// ─── 系统启动 ─────────────────────────────────────────────────────

int osal_init(void);
int osal_start(void (*entry)(void *parameter), void *parameter);

// ─── Semaphore ────────────────────────────────────────────────────

namespace osal {

class Semaphore {
public:
    Semaphore(const Semaphore&) = delete;
    Semaphore& operator=(const Semaphore&) = delete;

    explicit Semaphore(uint32_t initial = 0);
    ~Semaphore();

    [[nodiscard]] int take(uint32_t timeout = OSAL_WAITING_FOREVER);
    [[nodiscard]] int release();
    [[nodiscard]] bool isValid() const { return handle_ != nullptr; }
    [[nodiscard]] osal_sem_t handle() const { return handle_; }

private:
    osal_sem_t handle_;
};

// ─── Mutex ────────────────────────────────────────────────────────

class Mutex {
public:
    Mutex(const Mutex&) = delete;
    Mutex& operator=(const Mutex&) = delete;

    explicit Mutex();
    ~Mutex();

    [[nodiscard]] int lock(uint32_t timeout = OSAL_WAITING_FOREVER);
    [[nodiscard]] int tryLock();
    [[nodiscard]] int unlock();
    [[nodiscard]] bool isValid() const { return handle_ != nullptr; }
    [[nodiscard]] osal_mutex_t handle() const { return handle_; }

private:
    osal_mutex_t handle_;
};

class ScopedLock {
public:
    explicit ScopedLock(Mutex& m) : m_(m) { (void)m_.lock(); }
    ~ScopedLock() { (void)m_.unlock(); }
    ScopedLock(const ScopedLock&) = delete;
    ScopedLock& operator=(const ScopedLock&) = delete;
private:
    Mutex& m_;
};

// ─── Thread ───────────────────────────────────────────────────────

class Thread {
public:
    Thread(const Thread&) = delete;
    Thread& operator=(const Thread&) = delete;

    Thread(const char* name, void (*entry)(void*), void* param,
           void* stack, uint32_t stack_size, uint8_t prio, uint32_t tick = 0);
    ~Thread();

    [[nodiscard]] static Thread* create(const char* name, void (*entry)(void*),
                                         void* param, size_t stack_size,
                                         int32_t prio, int32_t tick = 0);

    [[nodiscard]] int startup();
    [[nodiscard]] int suspend();
    [[nodiscard]] int resume();
    [[nodiscard]] int yield();
    [[nodiscard]] bool isValid() const { return handle_.handle != nullptr; }

    [[nodiscard]] osal_thread_t* handle() { return &handle_; }
    [[nodiscard]] const osal_thread_t* handle() const { return &handle_; }

private:
    struct PrivateTag {};
    Thread(PrivateTag) : handle_{}, owned_(true) {}
    osal_thread_t handle_;
    bool owned_ {false};
};

namespace this_thread {
inline void sleep_for(uint32_t ms) { osal_sleep(ms); }
inline void yield() { osal_thread_yield(); }
} // namespace this_thread

} // namespace osal
