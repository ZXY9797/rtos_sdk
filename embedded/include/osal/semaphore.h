#pragma once

#include <osal.h>
#include <cstdint>

namespace osal {

class ISemaphore {
public:
    virtual ~ISemaphore() = default;

    [[nodiscard]] virtual int take(uint32_t timeout = OSAL_WAITING_FOREVER) = 0;
    [[nodiscard]] virtual int release() = 0;
};

class Semaphore {
public:
    Semaphore(const Semaphore&) = delete;
    Semaphore& operator=(const Semaphore&) = delete;

    explicit Semaphore(uint32_t initial_count = 0, uint8_t flag = 0)
        : count_(initial_count), flag_(flag) {}

    [[nodiscard]] int take(uint32_t timeout = OSAL_WAITING_FOREVER) {
        (void)timeout;
        if (count_ > 0) {
            --count_;
            return 0;
        }
        return -1;
    }

    [[nodiscard]] int release() {
        ++count_;
        return 0;
    }

    [[nodiscard]] uint32_t count() const { return count_; }

private:
    uint32_t count_;
    uint8_t flag_;
};

} // namespace osal
