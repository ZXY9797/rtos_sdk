#pragma once

#include <cstdint>

namespace hal {

// Converts an arbitrary CAN FD payload length to the smallest legal DLC.
// padded_length is the number of bytes the controller will read/write.
[[nodiscard]] constexpr bool can_fd_length_to_dlc(
    uint8_t length, uint8_t& dlc, uint8_t& padded_length)
{
    if (length <= 8U) {
        dlc = length;
        padded_length = length;
        return true;
    }
    constexpr uint8_t lengths[] = {12U, 16U, 20U, 24U, 32U, 48U, 64U};
    for (uint8_t index = 0U; index < sizeof(lengths); ++index) {
        if (length <= lengths[index]) {
            dlc = static_cast<uint8_t>(9U + index);
            padded_length = lengths[index];
            return true;
        }
    }
    return false;
}

[[nodiscard]] constexpr bool can_fd_dlc_to_length(
    uint8_t dlc, uint8_t& length)
{
    if (dlc <= 8U) {
        length = dlc;
        return true;
    }
    constexpr uint8_t lengths[] = {12U, 16U, 20U, 24U, 32U, 48U, 64U};
    if (dlc > 15U) {
        return false;
    }
    length = lengths[dlc - 9U];
    return true;
}

} // namespace hal
