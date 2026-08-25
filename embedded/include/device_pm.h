#pragma once

#include <cstddef>
#include <cstdint>

namespace hal {

enum class DevicePowerState : uint8_t {
    Active,
    Suspending,
    Suspended,
    Resuming,
    Error,
};

struct DevicePowerStatus {
    DevicePowerState state {DevicePowerState::Active};
    size_t managed_devices {0U};
    size_t transitioned_devices {0U};
    int failed_ordinal {-1};
    int error {0};
};

// Task/idle context only. Drivers participating in PM must implement paired,
// non-blocking suspend()/resume() methods. A failed suspend() must leave that
// device active. Suspend is reverse init order; resume is init order. A
// suspend failure rolls prior successful transitions back.
[[nodiscard]] bool suspend_devices();
[[nodiscard]] bool resume_devices();
[[nodiscard]] DevicePowerStatus device_power_status();

} // namespace hal
