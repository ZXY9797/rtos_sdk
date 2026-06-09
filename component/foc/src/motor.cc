#include <foc/motor.h>
#include <foc/math_utils.h>
#include <foc/config.h>
#include <algorithm>

namespace foc {

Motor::Motor(const MotorConfig &cfg,
             hal::PwmBase &pwm, hal::PwmChannel ch_u, hal::PwmChannel ch_v, hal::PwmChannel ch_w,
             hal::AdcBase &adc)
    : pwm_(pwm), ch_u_(ch_u), ch_v_(ch_v), ch_w_(ch_w), adc_(adc)
    , cfg_(cfg)
    , foc_(cfg.foc)
    , hfi_(HfiInjector::Config{})
    , pos_(PositionController::Config{}) {
}

void Motor::init() {
    // 配置 PWM — 三相互补模式
    hal::PwmConfig pwm_cfg;
    pwm_cfg.prescaler = 0;
    pwm_cfg.period = 100000000U / CONFIG_FOC_PWM_FREQUENCY_HZ; // 假设 100MHz 定时器时钟
    pwm_cfg.dead_time = 50; // ~500ns 死区
    pwm_cfg.center_aligned = true;
    pwm_cfg.complementary = true;
    pwm_period_ = pwm_cfg.period;

    (void)pwm_.init(pwm_cfg);

    // 配置 ADC 注入通道 (TIMER0 TRGO 触发)
    hal::AdcConfig adc_cfg;
    adc_cfg.resolution = 12;
    (void)adc_.init(adc_cfg);

    hal::AdcInjectedConfig inj_cfg;
    inj_cfg.trigger_source = 0; // TIMER0 TRGO
    inj_cfg.channel_count = 3;
    inj_cfg.channels[0] = {hal::AdcChannel::Ch0, hal::AdcSampleTime::Cycles64}; // Iu
    inj_cfg.channels[1] = {hal::AdcChannel::Ch1, hal::AdcSampleTime::Cycles64}; // Iw
    inj_cfg.channels[2] = {hal::AdcChannel::Ch2, hal::AdcSampleTime::Cycles64}; // Vbus
    (void)adc_.config_injected(inj_cfg);

    // FOC 参数计算
    foc_.calculate_gains(cfg_.rs, cfg_.ld, cfg_.lq, cfg_.flux_linkage, cfg_.pole_pairs);
    foc_.calculate_voltage_gain(cfg_.vmax);

    // 磁链观测器参数
    // observer_.set_params(cfg_.rs, cfg_.ld, cfg_.lq);

    state_ = MotorState::Idle;
}

void Motor::fast_loop_isr() {
    // 1. 读取 ADC
    read_adc();

    // 2. 角度估算
    uint16_t sensor_angle = 0;
    if (cfg_.sensor == SensorMode::Hall) {
        sensor_angle = hall_.angle();
    }
    float speed_ehz = hall_.speed_ehz();
    float dt = 1.0f / CONFIG_FOC_PWM_FREQUENCY_HZ;
    angle_estimator_.update(sensor_angle, speed_ehz, dt);
    SinCos sc = angle_estimator_.sin_cos();

    // 3. Clarke + Park + PI 电流环
    uint32_t duty_u, duty_v, duty_w;
    foc_.update_current(conv_i_, vbus_, angle_estimator_.angle(), sc,
                        duty_u, duty_v, duty_w);

    // 4. 写 PWM
    apply_duty(duty_u, duty_v, duty_w);
}

void Motor::slow_loop() {
    input_.collect();

    // 状态机
    state_machine();

    // 速度环
    if (state_ == MotorState::Running) {
        float dt = 1.0f / CONFIG_FOC_SPEED_LOOP_HZ;
        float speed_rpm = ehz_to_rpm(hall_.speed_ehz(), cfg_.pole_pairs);
        foc_.update_speed(speed_rpm, dt);
    }

    // 安全检查
    errors_.check(vbus_, conv_i_.u, temp_celsius_);
    if (errors_.has_error()) {
        emergency_stop();
    }

    // 数据记录
    logger_.log(foc_.id(), foc_.iq(), foc_.speed_rpm(), vbus_, temp_celsius_);

    input_.clear_requests();
}

void Motor::enable() {
    if (state_ == MotorState::Idle && !errors_.has_error()) {
        state_ = MotorState::Aligning;
        (void)pwm_.enable_output();
        (void)pwm_.start();
    }
}

void Motor::disable() {
    state_ = MotorState::Idle;
    apply_duty(0, 0, 0);
    (void)pwm_.disable_output();
}

void Motor::set_speed(float rpm) {
    foc_.set_speed_ref(rpm);
}

void Motor::set_torque(float iq) {
    foc_.set_iq_ref(iq);
}

void Motor::emergency_stop() {
    apply_duty(0, 0, 0);
    (void)pwm_.disable_output();
    state_ = MotorState::Error;
}

float Motor::speed_rpm() const {
    return foc_.speed_rpm();
}

void Motor::start_measurement() {
    state_ = MotorState::Calibrating;
    meas_.start();
    meas_.set_motor_pole_pairs(cfg_.pole_pairs);
    meas_.set_test_current(cfg_.imax * 0.1f);
}

void Motor::read_adc() {
    uint16_t iu_raw, iw_raw, vbus_raw;
    (void)adc_.read_injected(0, iu_raw);
    (void)adc_.read_injected(1, iw_raw);
    (void)adc_.read_injected(2, vbus_raw);

    // 转换为实际值 (需要根据硬件校准)
    // 假设: 12-bit ADC, 3.3V 参考, 电流传感器增益 0.1V/A, 偏置 1.65V
    float scale_i = 3.3f / 4096.0f / 0.1f;
    float scale_v = 3.3f / 4096.0f * 20.0f; // 分压比 1:20

    raw_i_.u = (static_cast<float>(iu_raw) - 2048.0f) * scale_i;
    raw_i_.v = -raw_i_.u - raw_i_.w; // iu + iv + iw = 0
    raw_i_.w = (static_cast<float>(iw_raw) - 2048.0f) * scale_i;
    vbus_ = static_cast<float>(vbus_raw) * scale_v;

    conv_i_ = raw_i_;
}

void Motor::state_machine() {
    switch (state_) {
    case MotorState::Idle:
        if (input_.enable_requested()) {
            enable();
        }
        break;

    case MotorState::Aligning:
        // 对齐阶段: 施加固定角度电流
        align_count_++;
        if (align_count_ > CONFIG_FOC_SPEED_LOOP_HZ * 2) { // 2秒对齐
            align_count_ = 0;
            if (cfg_.sensor == SensorMode::Sensorless) {
                state_ = MotorState::OpenLoop;
                angle_estimator_.set_open_loop(true, 10.0f); // 10Hz 开环
            } else {
                state_ = MotorState::Running;
            }
        }
        break;

    case MotorState::OpenLoop:
        // 开环启动 → 切换到闭环
        ol_count_++;
        if (ol_count_ > CONFIG_FOC_SPEED_LOOP_HZ * 3) { // 3秒开环
            ol_count_ = 0;
            angle_estimator_.set_open_loop(false);
            state_ = MotorState::Running;
        }
        break;

    case MotorState::Running:
        if (input_.disable_requested()) {
            disable();
        }
        break;

    case MotorState::Error:
        // 需要手动复位
        break;

    default:
        break;
    }
}

void Motor::apply_duty(uint32_t du, uint32_t dv, uint32_t dw) {
    (void)pwm_.set_pulse(ch_u_, du);
    (void)pwm_.set_pulse(ch_v_, dv);
    (void)pwm_.set_pulse(ch_w_, dw);
}

} // namespace foc
