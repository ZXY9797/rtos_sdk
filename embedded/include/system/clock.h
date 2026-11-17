#pragma once
#include <cstdint>

namespace system_clock {
// Modulo uint32 microseconds. SysTick interpolation assumes the configured
// fixed CPU clock and a serviced system tick. Maximum 3 read attempts.
uint32_t now_us();
}
