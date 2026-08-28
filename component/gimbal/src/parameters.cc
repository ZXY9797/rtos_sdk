#include <gimbal/parameters.h>

#include <gimbal/math.h>

#include <cmath>

namespace gimbal {
namespace {

constexpr float kBusVoltageMatchToleranceV = 0.05F;

[[nodiscard]] float component(const Vector3 &value, size_t axis)
{
    const float values[kAxisCount] {value.x, value.y, value.z};
    return axis < kAxisCount ? values[axis] : 0.0F;
}

[[nodiscard]] bool finite_vector(const Vector3 &value)
{
    return std::isfinite(value.x) && std::isfinite(value.y)
        && std::isfinite(value.z);
}

[[nodiscard]] bool positive_vector(const Vector3 &value)
{
    return finite_vector(value) && value.x > 0.0F
        && value.y > 0.0F && value.z > 0.0F;
}

[[nodiscard]] bool valid_voltage_foc_parameters(
    const FactoryParameters &parameters)
{
    for (const VoltageFocConfig &config : parameters.voltage_foc) {
        if (!valid_voltage_foc_config(config)) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool within_factory_limits(
    const MotionLimits &user, const MotionLimits &factory)
{
    for (size_t axis = 0U; axis < kAxisCount; ++axis) {
        if (component(user.maximum_speed_rad_s, axis)
                > component(factory.maximum_speed_rad_s, axis)
            || component(user.maximum_acceleration_rad_s2, axis)
                > component(factory.maximum_acceleration_rad_s2, axis)
            || component(user.maximum_deceleration_rad_s2, axis)
                > component(factory.maximum_deceleration_rad_s2, axis)
            || component(user.maximum_jerk_rad_s3, axis)
                > component(factory.maximum_jerk_rad_s3, axis)) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool within_vector_limits(const Vector3 &user,
                                        const Vector3 &factory)
{
    for (size_t axis = 0U; axis < kAxisCount; ++axis) {
        if (component(user, axis) > component(factory, axis)) {
            return false;
        }
    }
    return true;
}

} // namespace

uint32_t parameter_crc32(const uint8_t *data, size_t length)
{
    if (data == nullptr && length != 0U) {
        return 0U;
    }
    uint32_t crc = 0xFFFFFFFFU;
    for (size_t index = 0U; index < length; ++index) {
        crc ^= data[index];
        for (uint8_t bit = 0U; bit < 8U; ++bit) {
            const uint32_t mask = 0U - (crc & 1U);
            crc = (crc >> 1U) ^ (0xEDB88320U & mask);
        }
    }
    return ~crc;
}

bool valid_factory_parameters(const FactoryParameters &parameters)
{
    const auto motor_mode =
        static_cast<uint8_t>(parameters.capabilities.motor_mode);
    if (motor_mode > static_cast<uint8_t>(MotorControlMode::CurrentFoc)) {
        return false;
    }
    for (const HallCalibration &hall : parameters.hall) {
        if (!valid_hall_calibration(hall)) {
            return false;
        }
    }
    for (size_t axis = 0U; axis < kAxisCount; ++axis) {
        if (parameters.hall[axis].pole_pairs
                != parameters.dynamics.pole_pairs[axis]
            || parameters.hall[axis].maximum_speed_rad_s
                < parameters.safety.maximum_joint_speed_rad_s
            || std::abs(
                parameters.voltage_foc[axis].nominal_bus_voltage_v
                - parameters.dynamics.nominal_bus_voltage_v)
                > kBusVoltageMatchToleranceV) {
            return false;
        }
    }
    if (!valid_imu_calibration(parameters.imu)
        || !valid_attitude_ekf_config(parameters.ekf)
        || !valid_thermal_config(parameters.thermal)
        || !valid_kinematic_config(parameters.kinematics)
        || !valid_dynamics_config(parameters.dynamics)
        || !valid_controller_config(parameters.controller)
        || !valid_motion_limits(parameters.factory_motion_limits)
        || !positive_vector(parameters.maximum_follow_deadband_rad)
        || !positive_vector(parameters.maximum_follow_gain)
        || !valid_safety_config(parameters.safety)
        || !valid_voltage_foc_parameters(parameters)) {
        return false;
    }
    return parameters.capabilities.motor_mode
            != MotorControlMode::CurrentFoc
        || parameters.capabilities.has_phase_current;
}

bool valid_user_parameters(
    const UserParameters &parameters,
    const FactoryParameters &factory_parameters)
{
    const auto mode = static_cast<uint8_t>(parameters.mode);
    if (mode > static_cast<uint8_t>(ProductMode::Recenter)
        || !valid_motion_limits(parameters.motion_limits)
        || !finite_vector(parameters.follow_deadband_rad)
        || !finite_vector(parameters.follow_gain)
        || !within_factory_limits(parameters.motion_limits,
                                  factory_parameters.factory_motion_limits)
        || !within_vector_limits(
            parameters.follow_deadband_rad,
            factory_parameters.maximum_follow_deadband_rad)
        || !within_vector_limits(
            parameters.follow_gain,
            factory_parameters.maximum_follow_gain)) {
        return false;
    }
    for (size_t axis = 0U; axis < kAxisCount; ++axis) {
        if (component(parameters.follow_deadband_rad, axis) < 0.0F
            || component(parameters.follow_gain, axis) < 0.0F) {
            return false;
        }
    }
    return true;
}

} // namespace gimbal
