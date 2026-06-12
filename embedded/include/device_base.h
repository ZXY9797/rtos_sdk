#pragma once

#include <drivers/status.h>
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
    constexpr DeviceBase() = default;

    const char *name() const { return name_; }

    DeviceState state() const { return state_; }

    Status last_error() const { return last_error_; }

    bool is_initialized() const {
        return state_ == DeviceState::Initialized ||
               state_ == DeviceState::Open;
    }

    bool is_open() const { return state_ == DeviceState::Open; }

    bool is_ready() const {
        return is_initialized() && last_error_ == Status::Ok;
    }

protected:
    void set_name(const char *name) { name_ = name; }

    void set_state(DeviceState state) {
        state_ = state;
        if (state != DeviceState::Error) {
            last_error_ = Status::Ok;
        }
    }

    void set_error(Status error) {
        last_error_ = (error == Status::Ok) ? Status::HardwareError : error;
        state_ = DeviceState::Error;
    }

private:
    const char *name_ = nullptr;
    DeviceState state_ = DeviceState::Created;
    Status last_error_ = Status::Ok;
};

} // namespace hal
