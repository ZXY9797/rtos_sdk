#include <algo/foc/svpwm.h>

#include <algo/math/basic_math.h>

namespace algo::motor::foc {

SvpwmSector svpwm_get_sector(const AlphaBeta& input)
{
    const float u1 = input.beta;
    const float u2 =
        (basic_math::kSqrt3Over2 * input.alpha) - (0.5F * input.beta);
    const float u3 =
        (-basic_math::kSqrt3Over2 * input.alpha) - (0.5F * input.beta);

    const uint8_t s1 = (u1 > 0.0F) ? 1U : 0U;
    const uint8_t s2 = (u2 > 0.0F) ? 1U : 0U;
    const uint8_t s3 = (u3 > 0.0F) ? 1U : 0U;
    const uint8_t sector = static_cast<uint8_t>((s3 << 2U) | (s2 << 1U) | s1);

    switch (sector) {
    case 3:
        return SvpwmSector::Sector1;
    case 1:
        return SvpwmSector::Sector2;
    case 5:
        return SvpwmSector::Sector3;
    case 4:
        return SvpwmSector::Sector4;
    case 6:
        return SvpwmSector::Sector5;
    case 2:
        return SvpwmSector::Sector6;
    default:
        return SvpwmSector::None;
    }
}

SvpwmTimes svpwm_calculate_times(const AlphaBeta& input,
                                 SvpwmSector sector,
                                 float vbus,
                                 float pwm_period)
{
    SvpwmTimes output {};
    if ((vbus <= 0.0F) ||
        (pwm_period <= 0.0F) ||
        (sector == SvpwmSector::None)) {
        return output;
    }

    const float u1 = input.beta;
    const float u2 =
        (basic_math::kSqrt3Over2 * input.alpha) - (0.5F * input.beta);
    const float u3 =
        (-basic_math::kSqrt3Over2 * input.alpha) - (0.5F * input.beta);
    const float k = basic_math::kSqrt3 * pwm_period / vbus;

    float t1 = 0.0F;
    float t2 = 0.0F;
    switch (sector) {
    case SvpwmSector::Sector1:
        t1 = k * u2;
        t2 = k * u1;
        break;
    case SvpwmSector::Sector2:
        t1 = -k * u2;
        t2 = -k * u3;
        break;
    case SvpwmSector::Sector3:
        t1 = k * u1;
        t2 = k * u3;
        break;
    case SvpwmSector::Sector4:
        t1 = -k * u1;
        t2 = -k * u2;
        break;
    case SvpwmSector::Sector5:
        t1 = k * u3;
        t2 = k * u2;
        break;
    case SvpwmSector::Sector6:
        t1 = -k * u3;
        t2 = -k * u1;
        break;
    case SvpwmSector::None:
    default:
        return output;
    }

    const float sum = t1 + t2;
    if (sum > pwm_period) {
        t1 = (t1 * pwm_period) / sum;
        t2 = (t2 * pwm_period) / sum;
    }
    const float t0 = pwm_period - t1 - t2;

    switch (sector) {
    case SvpwmSector::Sector1:
        output.tu = t0 * 0.5F;
        output.tv = output.tu + t1;
        output.tw = output.tv + t2;
        break;
    case SvpwmSector::Sector2:
        output.tu = (t0 * 0.5F) + t1;
        output.tv = t0 * 0.5F;
        output.tw = pwm_period - (t0 * 0.5F);
        break;
    case SvpwmSector::Sector3:
        output.tu = pwm_period - (t0 * 0.5F);
        output.tv = t0 * 0.5F;
        output.tw = (t0 * 0.5F) + t1;
        break;
    case SvpwmSector::Sector4:
        output.tu = pwm_period - (t0 * 0.5F);
        output.tv = (t0 * 0.5F) + t1;
        output.tw = t0 * 0.5F;
        break;
    case SvpwmSector::Sector5:
        output.tu = (t0 * 0.5F) + t1;
        output.tv = pwm_period - (t0 * 0.5F);
        output.tw = t0 * 0.5F;
        break;
    case SvpwmSector::Sector6:
        output.tu = t0 * 0.5F;
        output.tv = pwm_period - (t0 * 0.5F);
        output.tw = (t0 * 0.5F) + t1;
        break;
    case SvpwmSector::None:
    default:
        return SvpwmTimes {};
    }

    output.t0 = t0;
    output.t1 = t1;
    output.t2 = t2;
    output.duty_u =
        basic_math::clamp_float((output.tu / pwm_period) * 100.0F,
                                0.0F,
                                100.0F);
    output.duty_v =
        basic_math::clamp_float((output.tv / pwm_period) * 100.0F,
                                0.0F,
                                100.0F);
    output.duty_w =
        basic_math::clamp_float((output.tw / pwm_period) * 100.0F,
                                0.0F,
                                100.0F);
    return output;
}

}  // namespace algo::motor::foc
