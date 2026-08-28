#pragma once

#include <gimbal/types.h>

#include <cstdint>

namespace gimbal {

enum class ThermalMode : uint8_t {
    Off = 0U,
    Warmup,
    Stabilizing,
    Stable,
    Fault,
};

enum class ThermalFault : uint8_t {
    None = 0U,
    InvalidTemperature,
    OverTemperature,
    WarmupTimeout,
    NoTemperatureRise,
};

struct ThermalConfig {
    float target_temperature_c {45.0F};
    float stable_tolerance_c {0.2F};
    float maximum_temperature_c {60.0F};
    float proportional_gain {0.08F};
    float integral_gain {0.02F};
    float maximum_duty {0.85F};
    float stable_time_s {3.0F};
    float warmup_timeout_s {60.0F};
    float rise_check_time_s {5.0F};
    float minimum_rise_c {0.5F};
};

struct ThermalOutput {
    ThermalMode mode {ThermalMode::Off};
    ThermalFault fault {ThermalFault::None};
    float heater_duty {0.0F};
    bool is_ready {false};
};

class ImuThermalController {
public:
    [[nodiscard]] bool configure(const ThermalConfig &config);
    void start(float initial_temperature_c);
    void stop();
    void clear_fault();
    [[nodiscard]] ThermalOutput update(float temperature_c,
                                       float sample_period_s);
    [[nodiscard]] ThermalOutput output() const { return output_; }

private:
    [[nodiscard]] ThermalOutput set_fault(ThermalFault fault);
    [[nodiscard]] ThermalOutput update_high_ambient(
        float temperature_rate_c_s, float sample_period_s);
    void update_regulation(float temperature_c, float error_c,
                           float sample_period_s);
    void update_rise_diagnostics(float temperature_c, float error_c,
                                 float sample_period_s);

    ThermalConfig config_ {};
    ThermalOutput output_ {};
    float integral_ {0.0F};
    float elapsed_s_ {0.0F};
    float stable_elapsed_s_ {0.0F};
    float rise_check_elapsed_s_ {0.0F};
    float rise_check_start_c_ {0.0F};
    float previous_temperature_c_ {0.0F};
    bool is_configured_ {false};
};

[[nodiscard]] bool valid_thermal_config(const ThermalConfig &config);

} // namespace gimbal
