#include <gimbal/dynamics.h>

#include <gimbal/math.h>

#include <algorithm>
#include <cmath>

namespace gimbal {
namespace {

constexpr float kMinimumMotorParameter = 1.0e-6F;
constexpr float kMinimumMappingDeterminant = 1.0e-6F;

[[nodiscard]] float component(const Vector3 &value, size_t axis)
{
    switch (axis) {
    case 0U:
        return value.x;
    case 1U:
        return value.y;
    case 2U:
        return value.z;
    default:
        return 0.0F;
    }
}

[[nodiscard]] bool finite_vector(const Vector3 &value)
{
    return std::isfinite(value.x) && std::isfinite(value.y)
        && std::isfinite(value.z);
}

[[nodiscard]] bool finite_matrix(const Matrix3 &value)
{
    for (const auto &row : value.value) {
        for (float element : row) {
            if (!std::isfinite(element)) return false;
        }
    }
    return true;
}

[[nodiscard]] bool finite_array(
    const std::array<float, kAxisCount> &value)
{
    return std::all_of(value.begin(), value.end(),
                       [](float element) {
                           return std::isfinite(element);
                       });
}

} // namespace

bool valid_dynamics_config(const DynamicsConfig &config)
{
    if (!std::isfinite(config.nominal_bus_voltage_v)
        || config.nominal_bus_voltage_v <= 0.0F) {
        return false;
    }
    for (size_t axis = 0U; axis < kAxisCount; ++axis) {
        if (config.inertia_matrix.value[axis][axis]
                   <= kMinimumMotorParameter
            || !std::isfinite(config.gravity_torque_nm[axis])
            || !std::isfinite(config.gravity_zero_rad[axis])
            || !std::isfinite(config.viscous_friction_nm_s[axis])
            || config.viscous_friction_nm_s[axis] < 0.0F
            || !std::isfinite(config.coulomb_friction_nm[axis])
            || config.coulomb_friction_nm[axis] < 0.0F
            || !std::isfinite(config.cogging_torque_nm[axis])
            || !std::isfinite(config.torque_constant_nm_a[axis])
            || config.torque_constant_nm_a[axis]
                   <= kMinimumMotorParameter
            || !std::isfinite(config.phase_resistance_ohm[axis])
            || config.phase_resistance_ohm[axis]
                   <= kMinimumMotorParameter
            || !std::isfinite(config.back_emf_v_s_rad[axis])
            || config.back_emf_v_s_rad[axis] < 0.0F
            || config.pole_pairs[axis] == 0U) {
            return false;
        }
        for (size_t coupled = 0U; coupled < kAxisCount; ++coupled) {
            const float value = config.inertia_matrix.value[axis][coupled];
            const float mirror = config.inertia_matrix.value[coupled][axis];
            if (!std::isfinite(value)
                || std::abs(value - mirror) > 1.0e-4F) {
                return false;
            }
        }
    }
    const float leading_minor =
        config.inertia_matrix.value[0][0]
            * config.inertia_matrix.value[1][1]
        - config.inertia_matrix.value[0][1]
            * config.inertia_matrix.value[1][0];
    return leading_minor > kMinimumMotorParameter
        && math::determinant(config.inertia_matrix)
            > kMinimumMotorParameter;
}

bool DynamicsFeedforward::configure(const DynamicsConfig &config)
{
    if (!valid_dynamics_config(config)) {
        is_configured_ = false;
        return false;
    }
    config_ = config;
    is_configured_ = true;
    return true;
}

DynamicsOutput DynamicsFeedforward::calculate(
    const JointState &joint, const MotionReference &reference,
    const Matrix3 &camera_motion_to_joint) const
{
    DynamicsOutput output {};
    if (!is_configured_ || joint.header.health != Health::Valid
        || joint.valid_axis_mask != kAllAxesMask
        || reference.header.health != Health::Valid
        || !finite_array(joint.angle_rad)
        || !finite_array(joint.speed_rad_s)
        || !finite_vector(reference.angular_acceleration_rad_s2)
        || !finite_matrix(camera_motion_to_joint)
        || std::abs(math::determinant(camera_motion_to_joint))
            <= kMinimumMappingDeterminant) {
        return output;
    }
    const Vector3 joint_acceleration = math::multiply(
        camera_motion_to_joint,
        reference.angular_acceleration_rad_s2);
    for (size_t axis = 0U; axis < kAxisCount; ++axis) {
        float torque = 0.0F;
        for (size_t coupled_axis = 0U;
             coupled_axis < kAxisCount; ++coupled_axis) {
            torque += config_.inertia_matrix.value[axis][coupled_axis]
                * component(joint_acceleration, coupled_axis);
        }
        const float position = joint.angle_rad[axis];
        const float speed = joint.speed_rad_s[axis];
        torque += config_.gravity_torque_nm[axis]
            * std::sin(position + config_.gravity_zero_rad[axis]);
        torque += config_.viscous_friction_nm_s[axis] * speed;
        if (std::abs(speed) > 1.0e-4F) {
            torque += std::copysign(
                config_.coulomb_friction_nm[axis], speed);
        }
        torque += config_.cogging_torque_nm[axis]
            * std::sin(static_cast<float>(config_.cogging_harmonic[axis])
                       * position);
        output.torque_nm[axis] = torque;
        output.current_a[axis] =
            torque / config_.torque_constant_nm_a[axis];
        const float electrical_speed = speed
            * static_cast<float>(config_.pole_pairs[axis]);
        output.voltage_v[axis] =
            output.current_a[axis] * config_.phase_resistance_ohm[axis]
            + electrical_speed * config_.back_emf_v_s_rad[axis];
        if (!std::isfinite(output.torque_nm[axis])
            || !std::isfinite(output.current_a[axis])
            || !std::isfinite(output.voltage_v[axis])) {
            return {};
        }
    }
    output.health = Health::Valid;
    return output;
}

} // namespace gimbal
