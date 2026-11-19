#pragma once

#include <cstdint>

namespace hal {

struct CanFrame {
    uint32_t id {};
    uint8_t size {};
    bool extended {};
    bool fd {};
    bool brs {};
    uint8_t data[64] {};
};

} // namespace hal
