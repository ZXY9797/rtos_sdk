#pragma once

#include <drivers/status.h>
#include <atomic>
#include <cstdint>

namespace hal {

/**
 * @brief Device lifecycle state.
 */
enum class DeviceState : uint8_t {
    Created,
    Initialized,
    Open,
    Error,
};

/**
 * @brief Base class for drivers that need a shared lifecycle contract.
 *
 * Drivers may inherit this class to expose a uniform readiness predicate to
 * the generated device registry. Error is an explicit terminal state, so a
 * failed driver is never reported as ready.
 */
class DeviceBase {
public:
    DeviceBase() = default;

    const char *name() const { return name_.load(std::memory_order_acquire); }

    DeviceState state() const { return state_.load(std::memory_order_acquire); }

    Status last_error() const {
        return last_error_.load(std::memory_order_acquire);
    }

    bool is_initialized() const {
        const DeviceState current = state();
        return current == DeviceState::Initialized ||
               current == DeviceState::Open;
    }

    bool is_open() const { return state() == DeviceState::Open; }

    bool is_ready() const {
        return is_initialized() && last_error() == Status::Ok;
    }

protected:
    void set_name(const char *name) {
        name_.store(name, std::memory_order_release);
    }

    void set_state(DeviceState state) {
        state_.store(state, std::memory_order_release);
        if (state != DeviceState::Error) {
            last_error_.store(Status::Ok, std::memory_order_release);
        }
    }

    void set_error(Status error) {
        last_error_.store(
            (error == Status::Ok) ? Status::HardwareError : error,
            std::memory_order_release);
        state_.store(DeviceState::Error, std::memory_order_release);
    }

private:
    std::atomic<const char *> name_ {nullptr};
    std::atomic<DeviceState> state_ {DeviceState::Created};
    std::atomic<Status> last_error_ {Status::Ok};
};

} // namespace hal
