#pragma once

#include <cstdint>

namespace hal {

enum class Status : uint8_t {
    Ok = 0,
    InvalidArgument,
    Busy,
    NotSupported,
    HardwareError,
    Timeout,
};

} // namespace hal
