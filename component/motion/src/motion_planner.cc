#include <gimbal/motion_planner.h>

#include <algo/motion_profile.h>
#include <gimbal/math.h>

#include <cmath>

namespace gimbal {
namespace {

constexpr float kMaximumSamplePeriodS = 0.1F;

[[nodiscard]] float component(const Vector3 &value, size_t axis)
{
    const float values[kAxisCount] {value.x, value.y, value.z};
    return axis < kAxisCount ? values[axis] : 0.0F;
}

void set_component(Vector3 &value, size_t axis, float component_value)
{
    float *values[kAxisCount] {&value.x, &value.y, &value.z};
    if (axis < kAxisCount) {
        *values[axis] = component_value;
    }
}

[[nodiscard]] bool positive_vector(const Vector3 &value)
{
    return std::isfinite(value.x) && std::isfinite(value.y)
        && std::isfinite(value.z) && value.x > 0.0F
        && value.y > 0.0F && value.z > 0.0F;
}

[[nodiscard]] bool finite_quaternion(const Quaternion &value)
{
    const float norm_squared = value.w * value.w + value.x * value.x
        + value.y * value.y + value.z * value.z;
    return std::isfinite(norm_squared) && norm_squared > 1.0e-12F;
}

} // namespace

bool valid_motion_limits(const MotionLimits &limits)
{
    return positive_vector(limits.maximum_speed_rad_s)
        && positive_vector(limits.maximum_acceleration_rad_s2)
        && positive_vector(limits.maximum_deceleration_rad_s2)
        && positive_vector(limits.maximum_jerk_rad_s3)
        && std::isfinite(limits.capture_angle_rad)
        && std::isfinite(limits.capture_rate_rad_s)
        && limits.capture_angle_rad > 0.0F
        && limits.capture_rate_rad_s > 0.0F;
}

bool MotionPlanner::configure(const MotionLimits &limits)
{
    if (!valid_motion_limits(limits)) {
        is_configured_ = false;
        return false;
    }
    limits_ = limits;
    is_configured_ = true;
    return true;
}

void MotionPlanner::reset(const Quaternion &attitude)
{
    reference_ = math::normalized(attitude);
    target_ = reference_;
    rate_ = {};
    acceleration_ = {};
    sequence_ = 0U;
    is_settled_ = true;
}

bool MotionPlanner::set_target(const Quaternion &target)
{
    if (!is_configured_ || !finite_quaternion(target)) {
        return false;
    }
    target_ = math::normalized(target);
    is_settled_ = false;
    return true;
}

MotionReference MotionPlanner::update(float sample_period_s,
                                      uint32_t publish_time_us)
{
    MotionReference output {};
    output.header.sequence = ++sequence_;
    output.header.capture_time_us = publish_time_us;
    output.header.publish_time_us = publish_time_us;
    if (!is_configured_ || !std::isfinite(sample_period_s)
        || sample_period_s <= 0.0F
        || sample_period_s > kMaximumSamplePeriodS) {
        output.header.health = Health::Fault;
        return output;
    }
    const Quaternion error_rotation = math::multiply(
        math::conjugate(reference_), target_);
    const Vector3 error = math::rotation_vector(error_rotation);
    std::array<algo::MotionProfileState, kAxisCount> next_states {};
    for (size_t axis = 0U; axis < kAxisCount; ++axis) {
        const algo::MotionProfileLimits axis_limits {
            component(limits_.maximum_speed_rad_s, axis),
            component(limits_.maximum_acceleration_rad_s2, axis),
            component(limits_.maximum_deceleration_rad_s2, axis),
            component(limits_.maximum_jerk_rad_s3, axis),
        };
        const algo::MotionProfileState state {
            component(rate_, axis), component(acceleration_, axis),
        };
        if (!algo::JerkLimitedMotionProfile::step(
                component(error, axis), sample_period_s,
                axis_limits, state, next_states[axis])) {
            output.header.health = Health::Fault;
            return output;
        }
    }
    for (size_t axis = 0U; axis < kAxisCount; ++axis) {
        set_component(rate_, axis, next_states[axis].rate);
        set_component(acceleration_, axis,
                      next_states[axis].acceleration);
    }
    const Vector3 increment = math::scale(rate_, sample_period_s);
    reference_ = math::normalized(math::multiply(
        reference_, math::from_rotation_vector(increment)));
    is_settled_ = math::norm(error) <= limits_.capture_angle_rad
        && math::norm(rate_) <= limits_.capture_rate_rad_s;
    if (is_settled_) {
        reference_ = target_;
        rate_ = {};
        acceleration_ = {};
    }
    output.attitude = reference_;
    output.angular_rate_rad_s = rate_;
    output.angular_acceleration_rad_s2 = acceleration_;
    output.header.health = Health::Valid;
    output.header.valid_flags = kAllAxesMask;
    return output;
}

} // namespace gimbal
