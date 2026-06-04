#pragma once

#include <osal.h>
#include <cstdint>

namespace osal {

class IMutex {
public:
    virtual ~IMutex() = default;

    [[nodiscard]] virtual int lock(uint32_t timeout = OSAL_WAITING_FOREVER) = 0;
    [[nodiscard]] virtual int tryLock() = 0;
    [[nodiscard]] virtual int unlock() = 0;
    [[nodiscard]] virtual bool isValid() const = 0;
};

class Mutex final : public IMutex {
public:
    Mutex(const Mutex&) = delete;
    Mutex& operator=(const Mutex&) = delete;

    explicit Mutex(const char* name = nullptr, uint8_t flag = 0)
        : external_(nullptr) {
        osal_mutex_init(&handle_, name, flag);
    }

    [[nodiscard]] static Mutex* create(const char* name = nullptr, uint8_t flag = 0) {
        osal_mutex_t* raw = osal_mutex_create(name, flag);
        if (!raw) return nullptr;

        auto* mutex = new Mutex(PrivateTag{});
        mutex->external_ = raw;
        return mutex;
    }

    ~Mutex() override {}

    [[nodiscard]] int lock(uint32_t timeout = OSAL_WAITING_FOREVER) override {
        return osal_mutex_take(handle(), timeout);
    }

    [[nodiscard]] int tryLock() override {
        return osal_mutex_trytake(handle());
    }

    [[nodiscard]] int unlock() override {
        return osal_mutex_release(handle());
    }

    [[nodiscard]] bool isValid() const { return handle() != nullptr; }

    [[nodiscard]] osal_mutex_t* handle() {
        return external_ ? external_ : &handle_;
    }

    [[nodiscard]] const osal_mutex_t* handle() const {
        return external_ ? external_ : &handle_;
    }

private:
    struct PrivateTag {};
    explicit Mutex(PrivateTag) : external_(nullptr) {}
    osal_mutex_t handle_{};
    osal_mutex_t* external_{nullptr};
};

class ScopedLock {
public:
    explicit ScopedLock(Mutex& mutex) : mutex_(mutex) {
        (void)mutex_.lock();
    }

    ~ScopedLock() {
        (void)mutex_.unlock();
    }

    ScopedLock(const ScopedLock&) = delete;
    ScopedLock& operator=(const ScopedLock&) = delete;

private:
    Mutex& mutex_;
};

} // namespace osal
