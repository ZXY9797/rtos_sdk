#include <algo/motor/execution_limits.h>
#include <algorithm>
#include <cmath>

namespace algo::motor
{
namespace
{
float clip(float value, float limit) { return std::clamp(value, -limit, limit); }
} // namespace

bool ExecutionLimits::configure(const ExecutionConfig &c, float limit, float fast_hz, float joint_hz,
                                bool preserve)
{
    const float values[]{c.phase_current_a,        c.continuous_current_a,  c.excess_i2t_a2_s,
                         c.maximum_velocity_rad_s, c.current_slew_a_s,      c.command_timeout_s,
                         c.temperature_timeout_s,  c.hard_limit_margin_rad, c.following_error_rad,
                         c.following_time_s,       c.motoring_power_w,      c.regenerative_power_w};
    for (float value : values)
        if (!std::isfinite(value) || value < 0.0F)
            return false;
    if (!std::isfinite(limit) || limit <= 0.0F || !std::isfinite(fast_hz) || fast_hz <= 0.0F ||
        !std::isfinite(joint_hz) || joint_hz <= 0.0F || c.phase_current_a <= 0.0F ||
        !std::isfinite(c.minimum_position_rad) || !std::isfinite(c.maximum_position_rad) ||
        (c.position_limits && c.minimum_position_rad >= c.maximum_position_rad) ||
        !std::isfinite(c.derate_start_c) || !std::isfinite(c.derate_stop_c) ||
        c.derate_start_c >= c.derate_stop_c)
        return false;
    const bool restore = preserve && configured_;
    const auto previous = state_;
    config_ = c;
    current_limit_ = limit;
    fast_dt_ = 1.0F / fast_hz;
    joint_dt_ = 1.0F / joint_hz;
    configured_ = true;
    if (!restore)
        reset();
    else
    {
        state_ = previous;
        state_.applied_current_a = 0;
        state_.current_limit_a = limit;
        state_.limited = false;
        if (c.excess_i2t_a2_s > 0 && state_.excess_i2t_a2_s >= c.excess_i2t_a2_s)
            trip(ExecutionFault::I2t);
    }
    return true;
}

void ExecutionLimits::reset()
{
    state_ = {};
    state_.current_limit_a = current_limit_;
    following_s_ = maximum_phase_a_ = measured_iq_a_ = 0.0F;
    last_position_ = last_velocity_ = 0.0F;
    derating_ = 1.0F;
}

void ExecutionLimits::current_sample(float u, float v, float w, float iq, float power, bool active)
{
    if (!configured_)
        return;
    if (!std::isfinite(u) || !std::isfinite(v) || !std::isfinite(w) || !std::isfinite(iq) ||
        !std::isfinite(power))
    {
        if (active)
            trip(ExecutionFault::PhaseCurrent);
        return;
    }
    maximum_phase_a_ = std::max({std::fabs(u), std::fabs(v), std::fabs(w)});
    measured_iq_a_ = iq;
    state_.electrical_power_w += 0.05F * (power - state_.electrical_power_w);
    if (active && maximum_phase_a_ > config_.phase_current_a)
        trip(ExecutionFault::PhaseCurrent);
    const float equivalent_squared = (u * u + v * v + w * w) * (2.0F / 3.0F);
    const float excess =
        active ? equivalent_squared - config_.continuous_current_a * config_.continuous_current_a
               : -config_.continuous_current_a * config_.continuous_current_a;
    state_.excess_i2t_a2_s = std::max(0.0F, state_.excess_i2t_a2_s + fast_dt_ * excess);
    if (active && config_.excess_i2t_a2_s > 0.0F && state_.excess_i2t_a2_s >= config_.excess_i2t_a2_s)
        trip(ExecutionFault::I2t);
}

void ExecutionLimits::supervise(float position, float velocity, float error, bool follow, bool active,
                                bool normal, float temperature, bool valid_temperature)
{
    if (!configured_)
        return;
    last_position_ = position;
    last_velocity_ = velocity;
    if (!active)
    {
        following_s_ = state_.invalid_temperature_s = 0.0F;
        return;
    }
    if (!std::isfinite(position) || !std::isfinite(velocity))
    {
        // Open-loop calibration can run before valid angle calibration exists.
        if (normal)
            trip(ExecutionFault::Position);
    }
    else
    {
        if (config_.maximum_velocity_rad_s > 0.0F && std::fabs(velocity) > config_.maximum_velocity_rad_s)
            trip(ExecutionFault::Overspeed);
        if (config_.position_limits &&
            (position < config_.minimum_position_rad - config_.hard_limit_margin_rad ||
             position > config_.maximum_position_rad + config_.hard_limit_margin_rad))
            trip(ExecutionFault::Position);
    }
    if (normal)
    {
        state_.command_age_s += joint_dt_;
        if (config_.command_timeout_s > 0.0F && state_.command_age_s > config_.command_timeout_s)
            trip(ExecutionFault::CommandStale);
    }
    if (follow && config_.following_error_rad > 0.0F &&
        (!std::isfinite(error) || std::fabs(error) > config_.following_error_rad))
        following_s_ += joint_dt_;
    else
        following_s_ = 0.0F;
    if (following_s_ > config_.following_time_s)
        trip(ExecutionFault::Following);
    if (!valid_temperature || !std::isfinite(temperature))
    {
        state_.invalid_temperature_s += joint_dt_;
        if (config_.temperature_timeout_s > 0.0F &&
            state_.invalid_temperature_s > config_.temperature_timeout_s)
            trip(ExecutionFault::TemperatureStale);
    }
    else
    {
        state_.invalid_temperature_s = 0.0F;
        derating_ = std::clamp((config_.derate_stop_c - temperature) /
                                   (config_.derate_stop_c - config_.derate_start_c),
                               0.0F, 1.0F);
    }
}

float ExecutionLimits::apply(float requested, float position, float velocity, float kt, bool active,
                             float bias)
{
    if (!configured_ || !active || state_.faults || !std::isfinite(requested))
    {
        state_.applied_current_a = 0.0F;
        return 0.0F;
    }
    float limit = current_limit_ * derating_;
    if (!std::isfinite(position) || !std::isfinite(velocity) || !std::isfinite(kt) || !std::isfinite(bias))
    {
        trip(ExecutionFault::ExternalSignal);
        state_.applied_current_a = 0;
        return 0;
    }
    const float power =
        config_.mechanical_power ? (kt * requested + bias) * velocity : state_.electrical_power_w;
    const float budget = power >= 0.0F ? config_.motoring_power_w : config_.regenerative_power_w;
    if (budget > 0.0F)
    {
        if (!config_.mechanical_power && std::fabs(power) > budget && requested * measured_iq_a_ > 0.0F)
            limit = std::min(limit, std::fabs(measured_iq_a_) * budget / std::fabs(power));
    }
    float applied = clip(requested, limit);
    if (config_.current_slew_a_s > 0.0F)
        applied = state_.applied_current_a +
                  clip(applied - state_.applied_current_a, config_.current_slew_a_s * joint_dt_);
    // A newly tighter safety limit must override a slower slew trajectory.
    applied = clip(applied, limit);
    if (config_.position_limits && std::isfinite(position) && kt != 0.0F &&
        ((position <= config_.minimum_position_rad && applied * kt + bias < 0.0F) ||
         (position >= config_.maximum_position_rad && applied * kt + bias > 0.0F)))
        applied = clip(-bias / kt, limit);
    if (config_.mechanical_power && std::fabs(kt * velocity) > 1.0e-6F)
    {
        const float actual_power = (kt * applied + bias) * velocity;
        const float actual_budget =
            actual_power >= 0 ? config_.motoring_power_w : config_.regenerative_power_w;
        if (actual_budget > 0 && std::fabs(actual_power) > actual_budget)
            applied = clip((std::copysign(actual_budget, actual_power) / velocity - bias) / kt, limit);
        if (actual_budget > 0 && std::fabs((kt * applied + bias) * velocity) > actual_budget + 1e-4F)
        {
            trip(ExecutionFault::PowerBudget);
            applied = 0;
        }
    }
    state_.current_limit_a = limit;
    state_.limited = applied != requested;
    return state_.applied_current_a = applied;
}

bool ExecutionLimits::clear()
{
    if (!configured_ || maximum_phase_a_ > config_.phase_current_a || !std::isfinite(last_velocity_) ||
        (config_.maximum_velocity_rad_s > 0.0F &&
         std::fabs(last_velocity_) > config_.maximum_velocity_rad_s) ||
        (config_.excess_i2t_a2_s > 0 && state_.excess_i2t_a2_s > 0.1F * config_.excess_i2t_a2_s) ||
        (config_.position_limits &&
         (last_position_ < config_.minimum_position_rad || last_position_ > config_.maximum_position_rad)))
        return false;
    reset();
    return true;
}

} // namespace algo::motor
