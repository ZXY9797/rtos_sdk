#include <gimbal/voltage_foc.h>

#include <gimbal/math.h>

#include <algorithm>
#include <cmath>

namespace gimbal {
namespace {

constexpr float kSqrtThreeOverTwo = 0.8660254037844386F;
constexpr float kInverseSqrtThree = 0.5773502691896258F;
constexpr float kMinimumBusVoltage = 0.1F;

} // namespace

bool valid_voltage_foc_config(const VoltageFocConfig &config)
{
    return std::isfinite(config.nominal_bus_voltage_v)
        && config.nominal_bus_voltage_v > kMinimumBusVoltage
        && std::isfinite(config.maximum_modulation)
        && config.maximum_modulation > 0.0F
        && config.maximum_modulation < 1.0F;
}

bool VoltageFoc::configure(const VoltageFocConfig &config)
{
    if (!valid_voltage_foc_config(config)) {
        is_configured_ = false;
        return false;
    }
    config_ = config;
    is_configured_ = true;
    return true;
}

PhaseDuty VoltageFoc::calculate(float electrical_angle_rad,
                                float d_axis_voltage_v,
                                float q_axis_voltage_v,
                                float bus_voltage_v) const
{
    if (!is_configured_ || !std::isfinite(electrical_angle_rad)
        || !std::isfinite(d_axis_voltage_v)
        || !std::isfinite(q_axis_voltage_v)) {
        return {};
    }
    float effective_bus_voltage = bus_voltage_v;
    if (!std::isfinite(effective_bus_voltage)
        || effective_bus_voltage < kMinimumBusVoltage) {
        effective_bus_voltage = config_.nominal_bus_voltage_v;
    }
    const float sine = std::sin(electrical_angle_rad);
    const float cosine = std::cos(electrical_angle_rad);
    float alpha = d_axis_voltage_v * cosine - q_axis_voltage_v * sine;
    float beta = d_axis_voltage_v * sine + q_axis_voltage_v * cosine;
    if (!std::isfinite(alpha) || !std::isfinite(beta)) {
        return {};
    }
    const float magnitude = std::hypot(alpha, beta);
    const float maximum_voltage =
        effective_bus_voltage * config_.maximum_modulation
        * kInverseSqrtThree;
    if (magnitude > maximum_voltage && magnitude > 0.0F) {
        const float gain = maximum_voltage / magnitude;
        alpha *= gain;
        beta *= gain;
    }
    float phase_u = alpha;
    float phase_v = -0.5F * alpha + kSqrtThreeOverTwo * beta;
    float phase_w = -0.5F * alpha - kSqrtThreeOverTwo * beta;
    const float minimum_phase = std::min({phase_u, phase_v, phase_w});
    const float maximum_phase = std::max({phase_u, phase_v, phase_w});
    const float common_mode = -0.5F * (minimum_phase + maximum_phase);
    const float voltage_to_duty = 1.0F / effective_bus_voltage;
    phase_u = 0.5F + (phase_u + common_mode) * voltage_to_duty;
    phase_v = 0.5F + (phase_v + common_mode) * voltage_to_duty;
    phase_w = 0.5F + (phase_w + common_mode) * voltage_to_duty;
    return {
        math::clamp(phase_u, 0.0F, 1.0F),
        math::clamp(phase_v, 0.0F, 1.0F),
        math::clamp(phase_w, 0.0F, 1.0F),
    };
}

} // namespace gimbal
