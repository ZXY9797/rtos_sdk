#include <foc/voltage_foc.h>

#include <cmath>

namespace foc {
namespace {

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
    if (!svpwm_.set_modulation_limit(config.maximum_modulation)) {
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
    const float alpha =
        d_axis_voltage_v * cosine - q_axis_voltage_v * sine;
    const float beta =
        d_axis_voltage_v * sine + q_axis_voltage_v * cosine;
    if (!std::isfinite(alpha) || !std::isfinite(beta)) {
        return {};
    }
    PhaseDuty duty {};
    if (!svpwm_.generate_duty({alpha, beta}, effective_bus_voltage, duty)) {
        return {};
    }
    return duty;
}

} // namespace foc
