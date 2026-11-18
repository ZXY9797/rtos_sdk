#include <foc/svpwm.h>

#include <algorithm>
#include <cmath>

namespace foc {
namespace {

constexpr float kMinimumBusVoltage = 0.1F;
constexpr float kInverseSqrtThree = 0.5773502691896258F;
constexpr float kSqrtThreeOverTwo = 0.8660254037844386F;

[[nodiscard]] bool finite_vector(const Vec2 &value)
{
    return std::isfinite(value.a) && std::isfinite(value.b);
}

} // namespace

bool Svpwm::set_modulation_limit(float limit)
{
    if (!std::isfinite(limit) || limit <= 0.0F || limit >= 1.0F) {
        return false;
    }
    modulation_max_ = limit;
    return true;
}

bool Svpwm::generate_duty(const Vec2 &v_ab, float vbus,
                          PhaseDuty &duty) const
{
    duty = {};
    if (!finite_vector(v_ab) || !std::isfinite(vbus)
        || vbus < kMinimumBusVoltage) {
        return false;
    }

    float alpha = v_ab.a;
    float beta = v_ab.b;
    const float magnitude = std::hypot(alpha, beta);
    const float maximum_voltage =
        vbus * modulation_max_ * kInverseSqrtThree;
    if (magnitude > maximum_voltage && magnitude > 0.0F) {
        const float scale = maximum_voltage / magnitude;
        alpha *= scale;
        beta *= scale;
    }

    float phase_u = alpha;
    float phase_v = -0.5F * alpha + kSqrtThreeOverTwo * beta;
    float phase_w = -0.5F * alpha - kSqrtThreeOverTwo * beta;
    const float minimum_phase = std::min({phase_u, phase_v, phase_w});
    const float maximum_phase = std::max({phase_u, phase_v, phase_w});
    const float common_mode = -0.5F * (minimum_phase + maximum_phase);
    const float voltage_to_duty = 1.0F / vbus;
    duty.phase_u = std::clamp(
        0.5F + (phase_u + common_mode) * voltage_to_duty,
        0.0F, 1.0F);
    duty.phase_v = std::clamp(
        0.5F + (phase_v + common_mode) * voltage_to_duty,
        0.0F, 1.0F);
    duty.phase_w = std::clamp(
        0.5F + (phase_w + common_mode) * voltage_to_duty,
        0.0F, 1.0F);
    return true;
}

void Svpwm::generate(const Vec2 &v_ab, float vbus, uint32_t period,
                     uint32_t &duty_u, uint32_t &duty_v,
                     uint32_t &duty_w)
{
    PhaseDuty duty {};
    if (period == 0U || !generate_duty(v_ab, vbus, duty)) {
        duty_u = 0U;
        duty_v = 0U;
        duty_w = 0U;
        return;
    }
    const float period_counts = static_cast<float>(period);
    duty_u = static_cast<uint32_t>(duty.phase_u * period_counts);
    duty_v = static_cast<uint32_t>(duty.phase_v * period_counts);
    duty_w = static_cast<uint32_t>(duty.phase_w * period_counts);
}

} // namespace foc
