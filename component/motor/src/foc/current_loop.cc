#include <algo/foc/current_loop.h>
#include <algo/math/basic_math.h>
#include <cmath>

namespace algo::motor {

bool CurrentLoop::configure(const CurrentLoopConfig& config)
{
    configured_ = false;
    reset();
    auto d_axis = config.d_axis, q_axis = config.q_axis;
    d_axis.anti_windup = q_axis.anti_windup = true;
    if (!std::isfinite(config.minimum_bus_voltage_v) ||
        config.minimum_bus_voltage_v <= 0.0F ||
        !std::isfinite(config.open_loop_phase_step_rad) ||
        config.open_loop_phase_step_rad <= 0.0F ||
        !std::isfinite(config.voltage_utilization) || config.voltage_utilization <= 0.0F ||
        config.voltage_utilization > 1.0F ||
        !std::isfinite(config.resistance_ohm) || config.resistance_ohm < 0.0F ||
        !std::isfinite(config.ld_h) || config.ld_h < 0.0F ||
        !std::isfinite(config.lq_h) || config.lq_h < 0.0F ||
        !std::isfinite(config.flux_wb) || config.flux_wb < 0.0F ||
        (config.decoupling && (config.ld_h <= 0.0F || config.lq_h <= 0.0F)) ||
        !d_pid_.configure(d_axis) || !q_pid_.configure(q_axis)) {
        return false;
    }
    config_ = config;
    configured_ = true;
    return true;
}

void CurrentLoop::reset()
{
    d_pid_.reset();
    q_pid_.reset();
    open_loop_angle_rad_ = 0.0F;
    held_q_voltage_v_ = 0.0F;
}

CurrentLoopOutput CurrentLoop::update(const CurrentLoopInput& input)
{
    return update(input, measure(input.current_a, input.measured_electrical_angle_rad));
}

foc::Dq CurrentLoop::measure(const foc::Phase3& current, float angle)
{
    return foc::park_transform(foc::clarke_transform(current), angle);
}

CurrentLoopOutput CurrentLoop::update(const CurrentLoopInput& input, const foc::Dq& measured)
{
    CurrentLoopOutput output {};
    if (!configured_ || !std::isfinite(input.current_a.u) ||
        !std::isfinite(input.current_a.v) || !std::isfinite(input.current_a.w) ||
        !std::isfinite(input.measured_electrical_angle_rad)) {
        reset();
        return output;
    }
    output.current_a = measured;
    if (!std::isfinite(output.current_a.d) || !std::isfinite(output.current_a.q)) {
        reset();
        return CurrentLoopOutput {};
    }
    if (input.mode == CurrentLoopMode::Disabled) {
        reset();
        output.valid = true;
        return output;
    }
    if (!std::isfinite(input.bus_voltage_v) ||
        input.bus_voltage_v <= config_.minimum_bus_voltage_v) {
        reset();
        return output;
    }
    float angle = input.measured_electrical_angle_rad;
    foc::Dq feedforward {};
    switch (input.mode) {
    case CurrentLoopMode::Current:
        if (!std::isfinite(input.target_current_a.d) ||
            !std::isfinite(input.target_current_a.q)) {
            reset();
            return output;
        }
        output.voltage_v.d = d_pid_.update(input.target_current_a.d - output.current_a.d);
        output.voltage_v.q = q_pid_.update(input.target_current_a.q - output.current_a.q);
        if (config_.decoupling) {
            if (!std::isfinite(input.electrical_velocity_rad_s)) { reset(); return {}; }
            const float omega = input.electrical_velocity_rad_s;
            feedforward.d = config_.resistance_ohm * output.current_a.d -
                            omega * config_.lq_h * output.current_a.q;
            feedforward.q = config_.resistance_ohm * output.current_a.q +
                            omega * (config_.ld_h * output.current_a.d + config_.flux_wb);
            output.voltage_v.d += feedforward.d;
            output.voltage_v.q += feedforward.q;
        }
        break;
    case CurrentLoopMode::HeldQVoltage:
        // IqOpenLoop holds the last q voltage; it does not
        // reinterpret its current target as volts.
        d_pid_.reset();
        q_pid_.reset();
        output.voltage_v.q = held_q_voltage_v_;
        break;
    case CurrentLoopMode::OpenLoop: {
        if (!std::isfinite(input.target_open_loop_angle_rad)) {
            reset();
            return output;
        }
        const float error = input.target_open_loop_angle_rad - open_loop_angle_rad_;
        open_loop_angle_rad_ += basic_math::clamp_float(
            error, -config_.open_loop_phase_step_rad, config_.open_loop_phase_step_rad);
        angle = open_loop_angle_rad_;
        output.voltage_v = input.voltage_v;
        break;
    }
    case CurrentLoopMode::Voltage:
        output.voltage_v = input.voltage_v;
        break;
    case CurrentLoopMode::DirectAlphaBeta:
        output.voltage_alpha_beta_v = input.direct_voltage_v;
        break;
    default:
        reset();
        return output;
    }
    if (!std::isfinite(output.voltage_v.d) || !std::isfinite(output.voltage_v.q)) {
        reset();
        return CurrentLoopOutput {};
    }
    if (input.mode != CurrentLoopMode::DirectAlphaBeta)
        output.voltage_alpha_beta_v = foc::inverse_park_transform(output.voltage_v, angle);
    if (!std::isfinite(output.voltage_alpha_beta_v.alpha) ||
        !std::isfinite(output.voltage_alpha_beta_v.beta)) {
        reset();
        return CurrentLoopOutput {};
    }
    const float magnitude = std::hypot(output.voltage_alpha_beta_v.alpha,
                                        output.voltage_alpha_beta_v.beta);
    const float available = input.bus_voltage_v * 0.57735026919F * config_.voltage_utilization;
    if (magnitude > available) {
        output.voltage_scale = available / magnitude;
        output.voltage_v.d *= output.voltage_scale;
        output.voltage_v.q *= output.voltage_scale;
        output.voltage_alpha_beta_v.alpha *= output.voltage_scale;
        output.voltage_alpha_beta_v.beta *= output.voltage_scale;
    }
    if (input.mode == CurrentLoopMode::Current) {
        (void)d_pid_.track_output(output.voltage_v.d - feedforward.d);
        (void)q_pid_.track_output(output.voltage_v.q - feedforward.q);
        held_q_voltage_v_ = output.voltage_v.q;
    }
    output.sector = foc::svpwm_get_sector(output.voltage_alpha_beta_v);
    if (output.sector != foc::SvpwmSector::None) {
        output.times = foc::svpwm_calculate_times(
            output.voltage_alpha_beta_v, output.sector, input.bus_voltage_v, 1.0F);
        if (!std::isfinite(output.times.t0) || !std::isfinite(output.times.t1) ||
            !std::isfinite(output.times.t2) || !std::isfinite(output.times.tu) ||
            !std::isfinite(output.times.tv) || !std::isfinite(output.times.tw)) {
            reset();
            return CurrentLoopOutput {};
        }
        output.duty_percent = {output.times.duty_u, output.times.duty_v, output.times.duty_w};
    }
    if (!std::isfinite(output.duty_percent.u) || !std::isfinite(output.duty_percent.v) ||
        !std::isfinite(output.duty_percent.w)) {
        reset();
        return CurrentLoopOutput {};
    }
    if (config_.duty_inverted) {
        output.duty_percent.u = 100.0F - output.duty_percent.u;
        output.duty_percent.v = 100.0F - output.duty_percent.v;
        output.duty_percent.w = 100.0F - output.duty_percent.w;
    }
    output.applied_electrical_angle_rad = angle;
    output.valid = true;
    output.energize = true;
    return output;
}

uint32_t CurrentLoop::duty_count(float percent, uint32_t auto_reload)
{
    if (auto_reload == 0U || !std::isfinite(percent)) return 0U;
    return static_cast<uint32_t>(basic_math::clamp_float(percent, 0.0F, 100.0F) *
                                 static_cast<float>(auto_reload - 1U) / 100.0F);
}

uint32_t CurrentLoop::adc_trigger_count(uint32_t auto_reload, float pwm_frequency_hz,
                                       uint32_t before_peak_ns)
{
    if (!auto_reload || !std::isfinite(pwm_frequency_hz) || pwm_frequency_hz <= 0.0F)
        return 0U;
    const float cycles = float(before_peak_ns) * pwm_frequency_hz * 2.0F *
                         float(auto_reload) / 1000000000.0F;
    if (!std::isfinite(cycles) || cycles >= float(UINT32_MAX)) return 1U;
    const uint32_t margin = static_cast<uint32_t>(cycles + 0.5F);
    const uint32_t trigger = auto_reload > margin ? auto_reload - margin : 1U;
    return trigger ? trigger : 1U;
}

}  // namespace algo::motor
