#pragma once

#include <osal.h>
#include <cstdint>
#include <cstddef>

namespace osal {

class IThread {
public:
    virtual ~IThread() = default;

    [[nodiscard]] virtual int startup() = 0;
    [[nodiscard]] virtual int suspend() = 0;
    [[nodiscard]] virtual int resume() = 0;
    [[nodiscard]] virtual int yield() = 0;
    [[nodiscard]] virtual bool isValid() const = 0;
};

class Thread final : public IThread {
public:
    Thread(const Thread&) = delete;
    Thread& operator=(const Thread&) = delete;

    Thread(const char* name, void (*entry)(void*), void* param,
           void* stack, uint32_t stack_size, uint8_t prio, uint32_t tick = 0)
        : external_(nullptr) {
        osal_thread_init(&handle_, name, entry, param,
                         stack, stack_size, prio, tick);
    }

    [[nodiscard]] static Thread* create(const char* name, void (*entry)(void*),
                                         void* param, size_t stack_size,
                                         int32_t prio, int32_t tick = 0) {
        osal_thread_t* raw = osal_thread_create(name, entry, param,
                                                 stack_size, prio, tick);
        if (!raw) return nullptr;

        auto* thread = new Thread(PrivateTag{});
        thread->external_ = raw;
        return thread;
    }

    ~Thread() override {}

    [[nodiscard]] int startup() override { return osal_thread_startup(handle()); }
    [[nodiscard]] int suspend() override { return osal_thread_suspend(handle()); }
    [[nodiscard]] int resume() override { return osal_thread_resume(handle()); }
    [[nodiscard]] int yield() override { return osal_thread_yield(); }
    [[nodiscard]] bool isValid() const { return handle() != nullptr; }

    [[nodiscard]] osal_thread_t* handle() {
        return external_ ? external_ : &handle_;
    }

    [[nodiscard]] const osal_thread_t* handle() const {
        return external_ ? external_ : &handle_;
    }

private:
    struct PrivateTag {};
    explicit Thread(PrivateTag) : external_(nullptr) {}
    osal_thread_t handle_{};
    osal_thread_t* external_{nullptr};
};

[[nodiscard]] inline int sleep(int ms) { return osal_sleep(ms); }

namespace this_thread {

inline void sleep_for(uint32_t ms) { osal_sleep(ms); }
inline void yield() { osal_thread_yield(); }

} // namespace this_thread

} // namespace osal
