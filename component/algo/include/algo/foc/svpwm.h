#pragma once

#include <algo/foc/transform.h>

#include <stdint.h>

namespace algo::motor::foc {

enum class SvpwmSector : uint8_t {
    None = 0,
    Sector1 = 1,
    Sector2 = 2,
    Sector3 = 3,
    Sector4 = 4,
    Sector5 = 5,
    Sector6 = 6,
};

struct SvpwmTimes {
    float t0 = 0.0F;
    float t1 = 0.0F;
    float t2 = 0.0F;
    float tu = 0.0F;
    float tv = 0.0F;
    float tw = 0.0F;
    float duty_u = 0.0F;
    float duty_v = 0.0F;
    float duty_w = 0.0F;
};

SvpwmSector svpwm_get_sector(const AlphaBeta& input);
SvpwmTimes svpwm_calculate_times(const AlphaBeta& input,
                                 SvpwmSector sector,
                                 float vbus,
                                 float pwm_period);

}  // namespace algo::motor::foc
