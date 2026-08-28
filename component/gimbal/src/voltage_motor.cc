#include <gimbal/voltage_motor.h>

#include <cmath>

namespace gimbal {

VoltageMotor::~VoltageMotor()
{
    if (is_initialized_) {
        (void)deinit();
    }
}

hal::Status VoltageMotor::init(const VoltageMotorConfig &config)
{
    if (is_initialized_
        || !hal::is_valid_pwm_channel(config.phase_u)
        || !hal::is_valid_pwm_channel(config.phase_v)
        || !hal::is_valid_pwm_channel(config.phase_w)
        || config.phase_u == hal::PwmChannel::Ch4
        || config.phase_v == hal::PwmChannel::Ch4
        || config.phase_w == hal::PwmChannel::Ch4
        || config.pwm_period_counts == 0U
        || config.phase_u == config.phase_v
        || config.phase_u == config.phase_w
        || config.phase_v == config.phase_w
        || !foc_.configure(config.foc)) {
        return hal::Status::InvalidArgument;
    }
    config_ = config;
    const PhaseDuty neutral {};
    if (write_duty(neutral) != hal::Status::Ok) {
        return hal::Status::HardwareError;
    }
    if (pwm_.start() != hal::Status::Ok) {
        return hal::Status::HardwareError;
    }
    if (pwm_.disable_output() != hal::Status::Ok) {
        (void)pwm_.stop();
        return hal::Status::HardwareError;
    }
    is_initialized_ = true;
    return hal::Status::Ok;
}

hal::Status VoltageMotor::deinit()
{
    disarm();
    if (!is_initialized_) {
        return hal::Status::Ok;
    }
    const hal::Status status = pwm_.stop();
    is_initialized_ = false;
    return status;
}

hal::Status VoltageMotor::arm()
{
    if (!is_initialized_) {
        return hal::Status::InvalidArgument;
    }
    const PhaseDuty neutral {};
    if (write_duty(neutral) != hal::Status::Ok
        || pwm_.enable_output() != hal::Status::Ok) {
        disarm();
        return hal::Status::HardwareError;
    }
    is_armed_.store(true, std::memory_order_release);
    return hal::Status::Ok;
}

void VoltageMotor::disarm()
{
    is_armed_.store(false, std::memory_order_release);
    (void)pwm_.disable_output();
}

hal::Status VoltageMotor::apply(float electrical_angle_rad,
                                float d_axis_voltage_v,
                                float q_axis_voltage_v,
                                float bus_voltage_v)
{
    if (!is_armed()) {
        return hal::Status::Busy;
    }
    if (!std::isfinite(electrical_angle_rad)
        || !std::isfinite(d_axis_voltage_v)
        || !std::isfinite(q_axis_voltage_v)
        || !std::isfinite(std::hypot(
            d_axis_voltage_v, q_axis_voltage_v))) {
        disarm();
        return hal::Status::InvalidArgument;
    }
    const PhaseDuty duty = foc_.calculate(
        electrical_angle_rad, d_axis_voltage_v,
        q_axis_voltage_v, bus_voltage_v);
    const hal::Status status = write_duty(duty);
    if (status != hal::Status::Ok) {
        disarm();
    }
    return status;
}

hal::Status VoltageMotor::write_duty(const PhaseDuty &duty)
{
    const bool valid = std::isfinite(duty.phase_u)
        && std::isfinite(duty.phase_v)
        && std::isfinite(duty.phase_w)
        && duty.phase_u >= 0.0F && duty.phase_u <= 1.0F
        && duty.phase_v >= 0.0F && duty.phase_v <= 1.0F
        && duty.phase_w >= 0.0F && duty.phase_w <= 1.0F;
    if (!valid) {
        return hal::Status::InvalidArgument;
    }
    const float period = static_cast<float>(config_.pwm_period_counts);
    const uint32_t phase_u = duty.phase_u >= 1.0F
        ? config_.pwm_period_counts
        : static_cast<uint32_t>(duty.phase_u * period);
    const uint32_t phase_v = duty.phase_v >= 1.0F
        ? config_.pwm_period_counts
        : static_cast<uint32_t>(duty.phase_v * period);
    const uint32_t phase_w = duty.phase_w >= 1.0F
        ? config_.pwm_period_counts
        : static_cast<uint32_t>(duty.phase_w * period);
    uint32_t channel_pulse[kAxisCount] {};
    channel_pulse[static_cast<uint8_t>(config_.phase_u)] = phase_u;
    channel_pulse[static_cast<uint8_t>(config_.phase_v)] = phase_v;
    channel_pulse[static_cast<uint8_t>(config_.phase_w)] = phase_w;
    if (pwm_.set_three_phase_pulses(
            channel_pulse[0], channel_pulse[1], channel_pulse[2])
        != hal::Status::Ok) {
        return hal::Status::HardwareError;
    }
    return hal::Status::Ok;
}

} // namespace gimbal
