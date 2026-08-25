#include <foc/motor.h>

#include <foc/config.h>
#include <foc/math_utils.h>
#include <irq.h>

#include <cmath>
#include <cstdint>
#include <cstring>

namespace foc {

Motor::Motor(const MotorConfig& cfg,
             hal::PwmBase& pwm, hal::PwmChannel ch_u,
             hal::PwmChannel ch_v, hal::PwmChannel ch_w,
             hal::AdcBase& adc)
    : pwm_(pwm), ch_u_(ch_u), ch_v_(ch_v), ch_w_(ch_w), adc_(adc),
      cfg_(cfg), foc_(cfg.foc), hfi_(HfiInjector::Config {}),
      pos_(PositionController::Config {})
{
}

hal::Status Motor::init()
{
    if (cfg_.timer_clock_hz == 0U || cfg_.foc.pwm_frequency <= 0.0F
        || cfg_.adc_resolution_bits == 0U || cfg_.adc_resolution_bits > 16U
        || cfg_.adc_reference_voltage <= 0.0F
        || cfg_.current_sense_volts_per_amp <= 0.0F
        || cfg_.vbus_divider_ratio <= 0.0F) {
        state_ = MotorState::Error;
        errors_.set_error(ErrorCode::HardwareFault);
        return hal::Status::InvalidArgument;
    }

    const uint32_t counter_clock_hz = cfg_.timer_clock_hz
        / (static_cast<uint32_t>(cfg_.pwm_prescaler) + 1U);
    const uint32_t pwm_frequency_hz = static_cast<uint32_t>(cfg_.foc.pwm_frequency);
    const uint64_t period_counts = static_cast<uint64_t>(counter_clock_hz)
        / (2ULL * pwm_frequency_hz);
    const uint64_t dead_time_counts =
        (static_cast<uint64_t>(counter_clock_hz) * cfg_.dead_time_ns
         + 999999999ULL) / 1000000000ULL;
    if (period_counts < 2ULL || period_counts > UINT32_MAX
        || dead_time_counts > 0xFFULL) {
        state_ = MotorState::Error;
        errors_.set_error(ErrorCode::HardwareFault);
        return hal::Status::InvalidArgument;
    }

    hal::PwmConfig pwm_cfg {};
    pwm_cfg.prescaler = cfg_.pwm_prescaler;
    pwm_cfg.period = static_cast<uint32_t>(period_counts - 1ULL);
    pwm_cfg.dead_time = static_cast<uint32_t>(dead_time_counts);
    pwm_cfg.center_aligned = true;
    pwm_cfg.complementary = true;
    pwm_period_ = pwm_cfg.period;

    hal::Status status = pwm_.init(pwm_cfg);
    if (status != hal::Status::Ok) {
        state_ = MotorState::Error;
        errors_.set_error(ErrorCode::HardwareFault);
        return status;
    }
    if (!apply_duty(0U, 0U, 0U)
        || pwm_.disable_output() != hal::Status::Ok) {
        state_ = MotorState::Error;
        errors_.set_error(ErrorCode::HardwareFault);
        return hal::Status::HardwareError;
    }

    hal::AdcConfig adc_cfg {};
    adc_cfg.resolution = cfg_.adc_resolution_bits;
    status = adc_.init(adc_cfg);
    if (status != hal::Status::Ok) {
        state_ = MotorState::Error;
        errors_.set_error(ErrorCode::HardwareFault);
        return status;
    }

    hal::AdcInjectedConfig injected {};
    injected.trigger_source = cfg_.adc_trigger_source;
    injected.channel_count = 3U;
    injected.channels[0] = {cfg_.current_u_channel, hal::AdcSampleTime::Cycles64};
    injected.channels[1] = {cfg_.current_w_channel, hal::AdcSampleTime::Cycles64};
    injected.channels[2] = {cfg_.vbus_channel, hal::AdcSampleTime::Cycles64};
    status = adc_.config_injected(injected);
    if (status != hal::Status::Ok) {
        state_ = MotorState::Error;
        errors_.set_error(ErrorCode::HardwareFault);
        return status;
    }

    foc_.calculate_gains(cfg_.rs, cfg_.ld, cfg_.lq,
                         cfg_.flux_linkage, cfg_.pole_pairs);
    foc_.calculate_voltage_gain(cfg_.vmax);
    state_ = MotorState::Idle;
    return hal::Status::Ok;
}

void Motor::fast_loop_isr()
{
    if (!read_adc()) {
        errors_.set_error(ErrorCode::SensorFault);
        emergency_stop();
        return;
    }

    uint16_t sensor_angle = 0U;
    if (cfg_.sensor == SensorMode::Hall) {
        sensor_angle = hall_.angle();
    }
    const float speed_ehz = hall_.speed_ehz();
    const float dt = 1.0F / cfg_.foc.pwm_frequency;
    angle_estimator_.update(sensor_angle, speed_ehz, dt);

    const uint16_t raw_angle = angle_estimator_.angle();
    const float angle_rad = angle_to_rad(raw_angle);
    float correction = angle_correction_;
    if (angle_lut_.table != nullptr && angle_lut_.point_count >= 3U) {
        correction += angle_lut_lookup(angle_rad);
    }
    if (!std::isfinite(correction)) {
        errors_.set_error(ErrorCode::SensorFault);
        emergency_stop();
        return;
    }
    const int32_t correction_counts = static_cast<int32_t>(
        correction * (65536.0F / (2.0F * 3.14159265F)));
    const uint16_t corrected_angle = normalize_angle(
        static_cast<int32_t>(raw_angle) + correction_counts);
    SinCos sin_cos_value {};
    foc::sin_cos(corrected_angle, sin_cos_value.sin, sin_cos_value.cos);

    uint32_t duty_u = 0U;
    uint32_t duty_v = 0U;
    uint32_t duty_w = 0U;
    foc_.update_current(conv_i_, vbus_, corrected_angle, sin_cos_value,
                        pwm_period_, duty_u, duty_v, duty_w);
    if (!apply_duty(duty_u, duty_v, duty_w)) {
        errors_.set_error(ErrorCode::HardwareFault);
        emergency_stop();
    }
}

void Motor::slow_loop()
{
    input_.collect();
    state_machine();
    if (state_.load(std::memory_order_relaxed) == MotorState::Running
        && cfg_.control == ControlMode::Speed) {
        const float dt = 1.0F / CONFIG_FOC_SPEED_LOOP_HZ;
        const float rpm = ehz_to_rpm(hall_.speed_ehz(), cfg_.pole_pairs);
        foc_.update_speed(rpm, dt);
    }

    const float temperature = temp_celsius_.load(std::memory_order_relaxed);
    const float vbus = bus_voltage_.load(std::memory_order_relaxed);
    const float control_current =
        control_current_u_.load(std::memory_order_relaxed);
    errors_.check(vbus, control_current, temperature);
    if (errors_.has_error()) {
        emergency_stop();
    }
    logger_.log(foc_.id(), foc_.iq(), foc_.speed_rpm(), vbus, temperature);
    input_.clear_requests();
}

void Motor::enable()
{
    hal::IrqGuard guard;
    if (state_.load(std::memory_order_relaxed) != MotorState::Idle
        || errors_.has_error()) {
        return;
    }
    if (pwm_.enable_output() != hal::Status::Ok
        || pwm_.start() != hal::Status::Ok) {
        (void)pwm_.disable_output();
        errors_.set_error(ErrorCode::HardwareFault);
        state_ = MotorState::Error;
        return;
    }
    state_ = MotorState::Aligning;
}

void Motor::disable()
{
    hal::IrqGuard guard;
    (void)apply_duty(0U, 0U, 0U);
    (void)pwm_.stop();
    (void)pwm_.disable_output();
    state_ = MotorState::Idle;
}

void Motor::set_speed(float rpm)
{
    foc_.set_speed_ref(rpm);
}

void Motor::set_torque(float iq)
{
    foc_.set_iq_ref(iq);
}

void Motor::emergency_stop()
{
    hal::IrqGuard guard;
    (void)apply_duty(0U, 0U, 0U);
    (void)pwm_.stop();
    (void)pwm_.disable_output();
    state_ = MotorState::Error;
}

float Motor::speed_rpm() const
{
    return foc_.speed_rpm();
}

hal::Status Motor::start_measurement()
{
    // MotorMeasurement currently has excitation/estimation logic but no
    // board-owned sampled-current execution path. Do not energize a motor or
    // leave it stuck in Calibrating using synthetic measurements.
    return hal::Status::NotSupported;
}

bool Motor::read_adc()
{
    uint16_t iu_raw = 0U;
    uint16_t iw_raw = 0U;
    uint16_t vbus_raw = 0U;
    if (adc_.read_injected(0U, iu_raw) != hal::Status::Ok
        || adc_.read_injected(1U, iw_raw) != hal::Status::Ok
        || adc_.read_injected(2U, vbus_raw) != hal::Status::Ok) {
        return false;
    }

    const float adc_codes = static_cast<float>(1UL << cfg_.adc_resolution_bits);
    const float current_scale = cfg_.adc_reference_voltage / adc_codes
        / cfg_.current_sense_volts_per_amp;
    const float voltage_scale = cfg_.adc_reference_voltage / adc_codes
        * cfg_.vbus_divider_ratio;
    raw_i_.u = (static_cast<float>(iu_raw) - cfg_.current_zero_code) * current_scale;
    raw_i_.w = (static_cast<float>(iw_raw) - cfg_.current_zero_code) * current_scale;
    raw_i_.v = -raw_i_.u - raw_i_.w;
    vbus_ = static_cast<float>(vbus_raw) * voltage_scale;

    conv_i_.u = raw_i_.u * current_calib_.gain_u - current_calib_.offset_u;
    conv_i_.w = raw_i_.w * current_calib_.gain_w - current_calib_.offset_w;
    conv_i_.v = -conv_i_.u - conv_i_.w;
    current_u_.store(raw_i_.u, std::memory_order_relaxed);
    current_w_.store(raw_i_.w, std::memory_order_relaxed);
    control_current_u_.store(conv_i_.u, std::memory_order_relaxed);
    bus_voltage_.store(vbus_, std::memory_order_relaxed);
    return std::isfinite(vbus_) && std::isfinite(conv_i_.u)
        && std::isfinite(conv_i_.v) && std::isfinite(conv_i_.w);
}

void Motor::state_machine()
{
    switch (state_.load(std::memory_order_relaxed)) {
    case MotorState::Idle:
        if (input_.enable_requested()) {
            enable();
        }
        break;
    case MotorState::Aligning:
        ++align_count_;
        if (align_count_ > CONFIG_FOC_SPEED_LOOP_HZ * 2U) {
            align_count_ = 0U;
            hal::IrqGuard guard;
            if (cfg_.sensor == SensorMode::Sensorless) {
                state_ = MotorState::OpenLoop;
                angle_estimator_.set_open_loop(true, 10.0F);
            } else {
                state_ = MotorState::Running;
            }
        }
        break;
    case MotorState::OpenLoop:
        ++ol_count_;
        if (ol_count_ > CONFIG_FOC_SPEED_LOOP_HZ * 3U) {
            ol_count_ = 0U;
            hal::IrqGuard guard;
            angle_estimator_.set_open_loop(false);
            state_ = MotorState::Running;
        }
        break;
    case MotorState::Running:
        if (input_.disable_requested()) {
            disable();
        }
        break;
    case MotorState::Calibrating:
        break;
    case MotorState::Error:
    default:
        break;
    }
}

void Motor::set_current_calibration(const CurrentCalib& cal)
{
    hal::IrqGuard guard;
    current_calib_ = cal;
}

void Motor::set_angle_correction(float offset_rad)
{
    hal::IrqGuard guard;
    angle_correction_ = offset_rad;
}

bool Motor::set_angle_lut(const AngleLutConfig& lut)
{
    if (lut.table == nullptr || lut.point_count < 3U
        || lut.point_count > kMaxAngleLutPoints
        || !std::isfinite(lut.x_max) || lut.x_max <= 0.0F) {
        return false;
    }
    osal::LockGuard lock(angle_lut_mutex_);
    if (!lock.owns_lock()) {
        return false;
    }

    // Copy into the inactive bank while the ISR keeps using the active bank,
    // then publish the complete immutable table in one IRQ-protected swap.
    const uint8_t next = static_cast<uint8_t>(angle_lut_active_ ^ 1U);
    std::memcpy(angle_lut_storage_[next], lut.table,
                lut.point_count * sizeof(float));
    hal::IrqGuard guard;
    angle_lut_.table = angle_lut_storage_[next];
    angle_lut_.point_count = lut.point_count;
    angle_lut_.x_max = lut.x_max;
    angle_lut_active_ = next;
    return true;
}

void Motor::set_current_reference(float id, float iq)
{
    hal::IrqGuard guard;
    foc_.set_id_ref(id);
    foc_.set_iq_ref(iq);
}

void Motor::configure_current_loop(float rs, float ld, float lq,
                                   float flux, uint8_t pole_pairs)
{
    hal::IrqGuard guard;
    foc_.calculate_gains(rs, ld, lq, flux, pole_pairs);
}

bool Motor::apply_duty(uint32_t du, uint32_t dv, uint32_t dw)
{
    if (du > pwm_period_ || dv > pwm_period_ || dw > pwm_period_) {
        return false;
    }
    return pwm_.set_pulse(ch_u_, du) == hal::Status::Ok
        && pwm_.set_pulse(ch_v_, dv) == hal::Status::Ok
        && pwm_.set_pulse(ch_w_, dw) == hal::Status::Ok;
}

float Motor::angle_lut_lookup(float angle_rad) const
{
    if (angle_lut_.table == nullptr || angle_lut_.point_count < 3U
        || angle_lut_.x_max <= 0.0F) {
        return 0.0F;
    }

    float x = std::fmod(angle_rad, angle_lut_.x_max);
    if (x < 0.0F) {
        x += angle_lut_.x_max;
    }
    const uint32_t count = angle_lut_.point_count;
    const float spacing = angle_lut_.x_max / static_cast<float>(count);
    const uint32_t index = static_cast<uint32_t>(x / spacing) % count;
    const float t = (x - static_cast<float>(index) * spacing) / spacing;
    const float previous = angle_lut_.table[(index + count - 1U) % count];
    const float current = angle_lut_.table[index];
    const float next = angle_lut_.table[(index + 1U) % count];
    return previous * (t * (t - 1.0F) * 0.5F)
         - current * ((t + 1.0F) * (t - 1.0F))
         + next * (t * (t + 1.0F) * 0.5F);
}

} // namespace foc
