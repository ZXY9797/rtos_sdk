#include <algo/motion_profile.h>

#include <algorithm>
#include <cmath>

namespace algo {
namespace {

[[nodiscard]] bool positive_finite(float value)
{
    return std::isfinite(value) && value > 0.0F;
}

} // namespace

bool valid_motion_profile_limits(const MotionProfileLimits &limits)
{
    return positive_finite(limits.maximum_speed)
        && positive_finite(limits.maximum_acceleration)
        && positive_finite(limits.maximum_deceleration)
        && positive_finite(limits.maximum_jerk);
}

bool JerkLimitedMotionProfile::step(
    float position_error, float sample_period_s,
    const MotionProfileLimits &limits,
    const MotionProfileState &state,
    MotionProfileState &next_state)
{
    next_state = {};
    if (!std::isfinite(position_error)
        || !positive_finite(sample_period_s)
        || !valid_motion_profile_limits(limits)
        || !std::isfinite(state.rate)
        || !std::isfinite(state.acceleration)) {
        return false;
    }
    const float braking_energy = 2.0F * limits.maximum_deceleration
        * std::abs(position_error);
    if (!std::isfinite(braking_energy)) {
        return false;
    }
    const float braking_speed = std::sqrt(braking_energy);
    const float target_speed = std::copysign(
        std::min(limits.maximum_speed, braking_speed), position_error);
    const float speed_error = target_speed - state.rate;
    const bool is_braking = target_speed * state.rate < 0.0F
        || std::abs(target_speed) < std::abs(state.rate);
    const float acceleration_limit = is_braking
        ? limits.maximum_deceleration : limits.maximum_acceleration;
    const float requested_acceleration = std::clamp(
        speed_error / sample_period_s,
        -acceleration_limit, acceleration_limit);
    const float acceleration_change = limits.maximum_jerk
        * sample_period_s;
    if (!std::isfinite(requested_acceleration)
        || !std::isfinite(acceleration_change)) {
        return false;
    }
    next_state.acceleration = std::clamp(
        requested_acceleration,
        state.acceleration - acceleration_change,
        state.acceleration + acceleration_change);
    next_state.rate = std::clamp(
        state.rate + next_state.acceleration * sample_period_s,
        -limits.maximum_speed, limits.maximum_speed);
    return std::isfinite(next_state.acceleration)
        && std::isfinite(next_state.rate);
}

} // namespace algo
