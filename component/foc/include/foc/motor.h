#pragma once

#include "types.h"
#include "foc_controller.h"
#include "svpwm.h"
#include "flux_observer.h"
#include "angle_estimator.h"
#include "hall_sensor.h"
#include "hfi.h"
#include "motor_measurement.h"
#include "bldc_controller.h"
#include "position_controller.h"
#include "input_manager.h"
#include "temperature.h"
#include "error_handler.h"
#include "data_logger.h"
#include <drivers/pwm.h>
#include <drivers/adc.h>

namespace foc {

struct MotorConfig {
    // 电机参数
    float rs {0.05f};
    float ld {0.0001f};
    float lq {0.0001f};
    float flux_linkage {0.0f};
    uint8_t pole_pairs {7};
    float imax {20.0f};
    float vmax {48.0f};
    float pmax {1000.0f};

    // FOC 配置
    FOCConfig foc;

    // 传感器模式
    SensorMode sensor {SensorMode::Sensorless};

    // 控制模式
    ControlMode control {ControlMode::Speed};
};

class Motor {
public:
    Motor(const MotorConfig &cfg,
          hal::PwmBase &pwm, hal::PwmChannel ch_u, hal::PwmChannel ch_v, hal::PwmChannel ch_w,
          hal::AdcBase &adc);

    // 初始化
    void init();

    // 快循环 — PWM ISR 中调用
    void fast_loop_isr();

    // 慢循环 — PeriodicThread 中调用
    void slow_loop();

    // 控制接口
    void enable();
    void disable();
    void set_speed(float rpm);
    void set_torque(float iq);
    void emergency_stop();

    // 状态查询
    MotorState state() const { return state_; }
    float speed_rpm() const;
    float phase_current_u() const { return raw_i_.u; }
    float phase_current_w() const { return raw_i_.w; }
    float bus_voltage() const { return vbus_; }
    float temperature() const { return temp_celsius_; }

    // 子系统访问
    const ErrorHandler &errors() const { return errors_; }
    FOCController &foc_controller() { return foc_; }
    MotorMeasurement &measurement() { return meas_; }
    DataLogger &logger() { return logger_; }

    // 电机参数自动检测
    void start_measurement();

private:
    // 硬件引用
    hal::PwmBase &pwm_;
    hal::PwmChannel ch_u_, ch_v_, ch_w_;
    hal::AdcBase &adc_;

    // 配置
    MotorConfig cfg_;

    // 子模块
    FOCController foc_;
    Svpwm svpwm_;
    AngleEstimator angle_estimator_;
    HallSensor hall_;
    HfiInjector hfi_;
    MotorMeasurement meas_;
    BLDCController bldc_;
    PositionController pos_;
    InputManager input_;
    ErrorHandler errors_;
    DataLogger logger_;

    // 状态
    MotorState state_ {MotorState::Idle};
    uint32_t align_count_ {0};
    uint32_t ol_count_ {0};

    // 电流/电压
    Vec3 raw_i_ {};
    Vec3 conv_i_ {};
    float vbus_ {0.0f};
    float temp_celsius_ {25.0f};

    // PWM 参数
    uint32_t pwm_period_ {0};

    // 内部方法
    void read_adc();
    void state_machine();
    void update_observer(float dt);
    void apply_duty(uint32_t du, uint32_t dv, uint32_t dw);
};

} // namespace foc
