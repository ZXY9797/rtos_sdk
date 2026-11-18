#pragma once
#include <cstdint>
namespace algo::motor {
// Numeric values define the diagnostic result contract.
enum class MotorStatus : uint8_t {
    Ok = 0, InvalidArgument = 1, Busy = 3, NotSupported = 4,
    HardwareError = 5, Timeout = 6, Cancelled = 0x0E,
};
}
