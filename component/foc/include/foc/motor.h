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
#include <osal.h>
#include <atomic>

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

    // Board-specific acquisition and timer parameters. These values must be
    // generated from the board description rather than inferred by the FOC core.
    uint32_t timer_clock_hz {120000000U};
    uint16_t pwm_prescaler {0U};
    uint32_t dead_time_ns {500U};
    uint8_t adc_resolution_bits {12U};
    hal::AdcChannel current_u_channel {hal::AdcChannel::Ch0};
    hal::AdcChannel current_w_channel {hal::AdcChannel::Ch1};
    hal::AdcChannel vbus_channel {hal::AdcChannel::Ch2};
    uint32_t adc_trigger_source {0U};
    float adc_reference_voltage {3.3F};
    float current_zero_code {2048.0F};
    float current_sense_volts_per_amp {0.1F};
    float vbus_divider_ratio {20.0F};
};

class Motor {
public:
    Motor(const MotorConfig &cfg,
          hal::PwmBase &pwm, hal::PwmChannel ch_u, hal::PwmChannel ch_v, hal::PwmChannel ch_w,
          hal::AdcBase &adc);

    // 初始化
    [[nodiscard]] hal::Status init();

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
    MotorState state() const { return state_.load(std::memory_order_relaxed); }
    float speed_rpm() const;
    float phase_current_u() const { return current_u_.load(std::memory_order_relaxed); }
    float phase_current_w() const { return current_w_.load(std::memory_order_relaxed); }
    float bus_voltage() const { return bus_voltage_.load(std::memory_order_relaxed); }
    float temperature() const { return temp_celsius_.load(std::memory_order_relaxed); }
    const MotorConfig &config() const { return cfg_; }

    // 子系统访问
    const ErrorHandler &errors() const { return errors_; }
    const FOCController &foc_controller() const { return foc_; }
    const MotorMeasurement &measurement() const { return meas_; }
    DataLogger &logger() { return logger_; }

    // 电机参数自动检测
    // Unavailable until a board-specific synchronized current/voltage
    // acquisition path is installed. The API fails closed in the meantime.
    [[nodiscard]] hal::Status start_measurement();

    // ── 校准注入接口 ──

    // 电流零偏校准: calib = raw * gain - offset
    struct CurrentCalib {
        float offset_u {0.0f};
        float offset_v {0.0f};
        float offset_w {0.0f};
        float gain_u {1.0f};
        float gain_v {1.0f};
        float gain_w {1.0f};
    };
    void set_current_calibration(const CurrentCalib &cal);

    // 电角度校正偏移量 (rad)，在 fast_loop_isr 中叠加到估算角度上
    void set_angle_correction(float offset_rad);

    // 电角度 Lagrange 插值校正器（每 ISR 周期根据当前角度查表校正）
    // table: 校准偏移表, point_count: 点数, x_max: 角度范围上限 (rad)
    struct AngleLutConfig {
        const float *table {nullptr};
        uint32_t point_count {0};
        float x_max {0.0f};
    };
    [[nodiscard]] bool set_angle_lut(const AngleLutConfig &lut);

    // Cross-context current-reference mailbox. The ISR consumes these atomics.
    void set_current_reference(float id, float iq);
    void configure_current_loop(float rs, float ld, float lq,
                                float flux, uint8_t pole_pairs);

    // 外部温度注入 (°C)，用于独立 NTC 传感器
    void set_temperature(float board_temp, float motor_temp) {
        temp_celsius_.store(motor_temp, std::memory_order_relaxed);
        board_temp_celsius_.store(board_temp, std::memory_order_relaxed);
    }
    float board_temperature() const { return board_temp_celsius_.load(std::memory_order_relaxed); }

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
    std::atomic<MotorState> state_ {MotorState::Idle};
    uint32_t align_count_ {0};
    uint32_t ol_count_ {0};

    // 电流/电压
    Vec3 raw_i_ {};
    Vec3 conv_i_ {};
    float vbus_ {0.0f};
    std::atomic<float> current_u_ {0.0F};
    std::atomic<float> current_w_ {0.0F};
    std::atomic<float> control_current_u_ {0.0F};
    std::atomic<float> bus_voltage_ {0.0F};
    std::atomic<float> temp_celsius_ {25.0F};
    std::atomic<float> board_temp_celsius_ {25.0F};

    // 校准数据
    CurrentCalib current_calib_ {};
    float angle_correction_ {0.0f};
    AngleLutConfig angle_lut_ {};
    static constexpr uint32_t kMaxAngleLutPoints = 128U;
    float angle_lut_storage_[2][kMaxAngleLutPoints] {};
    uint8_t angle_lut_active_ {0U};
    osal::Mutex angle_lut_mutex_;

    // Lagrange 查表（内联，避免 ISR 中分配）
    float angle_lut_lookup(float angle_rad) const;

    // PWM 参数
    uint32_t pwm_period_ {0};

    // 内部方法
    [[nodiscard]] bool read_adc();
    void state_machine();
    void update_observer(float dt);
    [[nodiscard]] bool apply_duty(uint32_t du, uint32_t dv, uint32_t dw);
};

} // namespace foc
