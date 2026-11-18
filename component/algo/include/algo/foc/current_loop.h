#pragma once

#include <algo/controller/pid_sys.h>
#include <algo/foc/svpwm.h>
#include <cstdint>

namespace algo::motor {

enum class CurrentLoopMode : uint8_t {
    Disabled, Current, Voltage, OpenLoop, DirectAlphaBeta, HeldQVoltage,
};

struct CurrentLoopConfig {
    controller::PidSysConfig d_axis {};
    controller::PidSysConfig q_axis {};
    float minimum_bus_voltage_v {1.0F};
    float open_loop_phase_step_rad {0.02F};
    bool duty_inverted {true};
    float voltage_utilization {0.95F};
    float resistance_ohm {0.0F}, ld_h {0.0F}, lq_h {0.0F}, flux_wb {0.0F};
    bool decoupling {false};
};

struct CurrentLoopInput {
    foc::Phase3 current_a {};
    float bus_voltage_v {0.0F};
    float measured_electrical_angle_rad {0.0F};
    foc::Dq target_current_a {};
    foc::Dq voltage_v {};
    foc::AlphaBeta direct_voltage_v {};
    float target_open_loop_angle_rad {0.0F};
    float electrical_velocity_rad_s {0.0F};
    CurrentLoopMode mode {CurrentLoopMode::Disabled};
};

struct CurrentLoopOutput {
    bool valid {false};
    bool energize {false};
    foc::Dq current_a {};
    foc::Dq voltage_v {};
    foc::AlphaBeta voltage_alpha_beta_v {};
    foc::SvpwmSector sector {foc::SvpwmSector::None};
    foc::SvpwmTimes times {};
    foc::Phase3 duty_percent {50.0F, 50.0F, 50.0F};
    float applied_electrical_angle_rad {0.0F};
    float voltage_scale {1.0F};
};

// One control/ISR owner. Configuration and reset require quiescent execution.
// This computes commands only; the caller owns atomic PWM commit and shutdown.
class CurrentLoop {
public:
    bool configure(const CurrentLoopConfig& config);
    void reset();
    CurrentLoopOutput update(const CurrentLoopInput& input);
    CurrentLoopOutput update(const CurrentLoopInput& input, const foc::Dq& measured);
    static foc::Dq measure(const foc::Phase3& current, float electrical_angle_rad);
    static uint32_t duty_count(float percent, uint32_t auto_reload);
    static uint32_t adc_trigger_count(uint32_t auto_reload, float pwm_frequency_hz,
                                      uint32_t before_peak_ns);

private:
    CurrentLoopConfig config_ {};
    controller::PidSys d_pid_ {};
    controller::PidSys q_pid_ {};
    float open_loop_angle_rad_ {0.0F};
    float held_q_voltage_v_ {0.0F};
    bool configured_ {false};
};

}  // namespace algo::motor
