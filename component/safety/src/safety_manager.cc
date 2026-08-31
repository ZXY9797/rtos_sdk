#include <gimbal/safety_manager.h>

#include <algorithm>
#include <cmath>

namespace gimbal {
namespace {

constexpr float kMaximumSamplePeriodS = 1.0F;

[[nodiscard]] bool stale(uint32_t now_us, uint32_t publish_time_us,
                         uint32_t timeout_us)
{
    return now_us - publish_time_us > timeout_us;
}

[[nodiscard]] bool valid_health(Health health)
{
    return health == Health::Valid || health == Health::Degraded;
}

[[nodiscard]] float maximum_absolute(
    const std::array<float, kAxisCount> &values)
{
    float result = 0.0F;
    for (float value : values) {
        result = std::max(result, std::abs(value));
    }
    return result;
}

[[nodiscard]] bool finite_array(
    const std::array<float, kAxisCount> &values)
{
    return std::all_of(values.begin(), values.end(),
                       [](float value) { return std::isfinite(value); });
}

[[nodiscard]] bool finite_vector(const Vector3 &value)
{
    return std::isfinite(value.x) && std::isfinite(value.y)
        && std::isfinite(value.z);
}

} // namespace

bool valid_safety_config(const SafetyConfig &config)
{
    return std::isfinite(config.maximum_joint_speed_rad_s)
        && std::isfinite(config.stall_command_threshold)
        && std::isfinite(config.stall_speed_threshold_rad_s)
        && std::isfinite(config.stall_time_s)
        && config.maximum_joint_speed_rad_s > 0.0F
        && config.stall_command_threshold > 0.0F
        && config.stall_speed_threshold_rad_s >= 0.0F
        && config.stall_time_s > 0.0F
        && std::isfinite(config.minimum_bus_voltage_v)
        && config.minimum_bus_voltage_v > 0.0F
        && std::isfinite(config.maximum_bus_voltage_v)
        && config.maximum_bus_voltage_v > config.minimum_bus_voltage_v
        && std::isfinite(config.maximum_phase_current_a)
        && config.maximum_phase_current_a > 0.0F
        && std::isfinite(config.maximum_driver_temperature_c)
        && config.maximum_driver_temperature_c > 0.0F
        && std::isfinite(config.maximum_mos_temperature_c)
        && config.maximum_mos_temperature_c > 0.0F
        && config.sensor_timeout_us > 0U
        && config.command_timeout_us > 0U
        && std::isfinite(config.self_test_timeout_s)
        && config.self_test_timeout_s > 0.0F;
}

bool SafetyManager::configure(const SafetyConfig &config,
                              const CapabilitySet &capabilities)
{
    if (!valid_safety_config(config)
        || static_cast<uint8_t>(capabilities.motor_mode)
            > static_cast<uint8_t>(MotorControlMode::CurrentFoc)
        || (capabilities.motor_mode == MotorControlMode::CurrentFoc
            && !capabilities.has_phase_current)) {
        is_configured_ = false;
        return false;
    }
    config_ = config;
    capabilities_ = capabilities;
    output_ = {};
    stall_elapsed_s_ = 0.0F;
    self_test_elapsed_s_ = 0.0F;
    arm_requested_.store(false, std::memory_order_release);
    is_configured_ = true;
    return true;
}

void SafetyManager::start_self_test()
{
    if (is_configured_ && output_.state == SystemState::Boot) {
        self_test_elapsed_s_ = 0.0F;
        output_.state = SystemState::SelfTest;
    }
}

void SafetyManager::request_arm()
{
    arm_requested_.store(true, std::memory_order_release);
}

void SafetyManager::request_disarm()
{
    arm_requested_.store(false, std::memory_order_release);
}

uint32_t SafetyManager::detect_faults(const SafetyInput &input,
                                      float sample_period_s)
{
    return detect_core_faults(input)
        | detect_control_faults(input, sample_period_s)
        | detect_feedback_faults(input);
}

uint32_t SafetyManager::detect_core_faults(
    const SafetyInput &input) const
{
    uint32_t faults = 0U;
    if (!input.factory_parameters_valid) {
        faults |= fault_mask(Fault::FactoryParameters);
    }
    if (input.joint.valid_axis_mask != kAllAxesMask
        || !valid_health(input.joint.header.health)
        || !finite_array(input.joint.angle_rad)
        || !finite_array(input.joint.speed_rad_s)
        || !finite_array(input.joint.electrical_angle_rad)
        || stale(input.now_us, input.joint.header.publish_time_us,
                 config_.sensor_timeout_us)) {
        faults |= fault_mask(Fault::HallInvalid);
    }
    if (!valid_health(input.imu.header.health)
        || !finite_vector(input.imu.acceleration_mps2)
        || !finite_vector(input.imu.angular_rate_rad_s)
        || !std::isfinite(input.imu.temperature_c)
        || stale(input.now_us, input.imu.header.publish_time_us,
                 config_.sensor_timeout_us)) {
        faults |= fault_mask(Fault::ImuInvalid);
    }
    if (!input.estimator_valid) {
        faults |= fault_mask(Fault::EstimatorInvalid);
    }
    if (input.heater_fault) {
        faults |= fault_mask(Fault::HeaterFault);
    }
    if (input.singular) {
        faults |= fault_mask(Fault::Singularity);
    }
    if (input.joint_limit_exceeded) {
        faults |= fault_mask(Fault::JointLimit);
    }
    if (maximum_absolute(input.joint.speed_rad_s)
        > config_.maximum_joint_speed_rad_s) {
        faults |= fault_mask(Fault::Overspeed);
    }
    return faults;
}

uint32_t SafetyManager::detect_control_faults(
    const SafetyInput &input, float sample_period_s)
{
    uint32_t faults = 0U;
    const bool active = output_.state == SystemState::Active;
    if (active && input.deadline_missed) {
        faults |= fault_mask(Fault::DeadlineMissed);
    }
    if (active && (!valid_health(input.command.header.health)
        || !finite_array(input.command.d_axis_command)
        || !finite_array(input.command.q_axis_command)
        || stale(input.now_us, input.command.header.publish_time_us,
                 config_.command_timeout_us))) {
        faults |= fault_mask(Fault::CommandStale);
    }
    const float maximum_command =
        maximum_absolute(input.command.q_axis_command);
    const float maximum_speed = maximum_absolute(input.joint.speed_rad_s);
    if (active && maximum_command >= config_.stall_command_threshold
        && maximum_speed <= config_.stall_speed_threshold_rad_s) {
        stall_elapsed_s_ += sample_period_s;
    } else {
        stall_elapsed_s_ = 0.0F;
    }
    if (stall_elapsed_s_ >= config_.stall_time_s) {
        faults |= fault_mask(Fault::Stall);
    }
    return faults;
}

uint32_t SafetyManager::detect_feedback_faults(
    const SafetyInput &input) const
{
    uint32_t faults = 0U;
    const bool feedback_required = output_.state == SystemState::Active
        || capabilities_.has_phase_current
        || capabilities_.has_bus_voltage
        || capabilities_.has_driver_temperature
        || capabilities_.has_mos_temperature;
    const bool feedback_valid = valid_health(input.feedback.header.health)
        && !stale(input.now_us, input.feedback.header.publish_time_us,
                  config_.sensor_timeout_us);
    if (feedback_required && !feedback_valid) {
        faults |= fault_mask(Fault::FeedbackInvalid);
        return faults;
    }
    if (capabilities_.has_phase_current) {
        if (!finite_array(input.feedback.phase_current_a)) {
            faults |= fault_mask(Fault::FeedbackInvalid);
        } else if (maximum_absolute(input.feedback.phase_current_a)
                   > config_.maximum_phase_current_a) {
            faults |= fault_mask(Fault::PhaseOvercurrent);
        }
    }
    if (capabilities_.has_bus_voltage) {
        if (!std::isfinite(input.feedback.bus_voltage_v)) {
            faults |= fault_mask(Fault::FeedbackInvalid);
        } else if (input.feedback.bus_voltage_v
                       < config_.minimum_bus_voltage_v
                   || input.feedback.bus_voltage_v
                       > config_.maximum_bus_voltage_v) {
            faults |= fault_mask(Fault::BusVoltage);
        }
    }
    if (capabilities_.has_driver_temperature) {
        if (!finite_array(input.feedback.driver_temperature_c)) {
            faults |= fault_mask(Fault::FeedbackInvalid);
        } else if (maximum_absolute(input.feedback.driver_temperature_c)
                   > config_.maximum_driver_temperature_c) {
            faults |= fault_mask(Fault::DriverOvertemperature);
        }
    }
    if (capabilities_.has_mos_temperature) {
        if (!finite_array(input.feedback.mos_temperature_c)) {
            faults |= fault_mask(Fault::FeedbackInvalid);
        } else if (maximum_absolute(input.feedback.mos_temperature_c)
                   > config_.maximum_mos_temperature_c) {
            faults |= fault_mask(Fault::MosOvertemperature);
        }
    }
    return faults;
}

void SafetyManager::latch_active_faults(const SafetyInput &input)
{
    if (output_.active_faults == 0U) {
        return;
    }
    if (output_.latched_faults == 0U) {
        output_.first_fault.capture_time_us = input.now_us;
        output_.first_fault.faults = output_.active_faults;
        output_.first_fault.joint_angle_rad = input.joint.angle_rad;
        output_.first_fault.joint_speed_rad_s = input.joint.speed_rad_s;
        output_.first_fault.q_axis_command =
            input.command.q_axis_command;
        output_.first_fault.imu_temperature_c = input.imu.temperature_c;
        output_.first_fault.bus_voltage_v = input.feedback.bus_voltage_v;
    }
    output_.latched_faults |= output_.active_faults;
}

void SafetyManager::update_state(const SafetyInput &input)
{
    if (output_.latched_faults != 0U) {
        output_.state = SystemState::Fault;
        output_.allow_motor_output = false;
        return;
    }
    switch (output_.state) {
    case SystemState::SelfTest:
        output_.state = input.heater_ready
            ? SystemState::Standby : SystemState::Heating;
        break;
    case SystemState::Heating:
        if (input.heater_ready) {
            output_.state = SystemState::Standby;
        }
        break;
    case SystemState::Standby:
        if (arm_requested_.load(std::memory_order_acquire)) {
            output_.state = SystemState::Arming;
        }
        break;
    case SystemState::Arming:
        output_.state = arm_requested_.load(std::memory_order_acquire)
            ? SystemState::Active : SystemState::Standby;
        break;
    case SystemState::Active:
        if (!arm_requested_.load(std::memory_order_acquire)) {
            output_.state = SystemState::Standby;
        }
        break;
    default:
        break;
    }
    output_.allow_motor_output = output_.state == SystemState::Active;
}

SafetyOutput SafetyManager::update(const SafetyInput &input,
                                   float sample_period_s)
{
    if (!is_configured_ || !std::isfinite(sample_period_s)
        || sample_period_s <= 0.0F
        || sample_period_s > kMaximumSamplePeriodS) {
        output_.active_faults = fault_mask(Fault::FactoryParameters);
    } else {
        output_.active_faults = detect_faults(input, sample_period_s);
    }
    if (!input.factory_parameters_valid) {
        output_.state = SystemState::Calibration;
        output_.allow_motor_output = false;
        arm_requested_.store(false, std::memory_order_release);
        return output_;
    }
    const bool self_testing = output_.state == SystemState::SelfTest
        || output_.state == SystemState::Heating;
    if (self_testing) {
        self_test_elapsed_s_ += sample_period_s;
        output_.allow_motor_output = false;
        if (output_.active_faults != 0U) {
            if (self_test_elapsed_s_ >= config_.self_test_timeout_s) {
                latch_active_faults(input);
                output_.state = SystemState::Fault;
            }
            return output_;
        }
    }
    latch_active_faults(input);
    update_state(input);
    return output_;
}

} // namespace gimbal
