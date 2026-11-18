#pragma once
#include <algo/calibration/calibration.h>
#include <algo/foc/current_loop.h>
#include <algo/motor/joint_controller.h>
#include <algo/motor/diagnostics.h>
#include <algo/motor/protection.h>

namespace algo::motor {

enum class MotorVariant : uint8_t {
    Default = 0U, ProfileA = 1U, ProfileB = 2U, ProfileC = 3U,
    ProfileD = 4U, ProfileE = 5U, ProfileF = 6U,
};

// Explicit motor presets, never an automatic hardware selection. No pins,
// ADC channels, task/IRQ owners or physical enable callbacks are selected.
struct MotorParameters {
    MotorBasicConfig motor {};
    MotorElecCalibData raw_conversion {};
    CurrentLoopConfig current {};
    JointAlgorithmConfig joint {};
    CalibrationConfig calibration {};
    CurrentSweepConfig sweep {};
    TemperatureProtectConfig temperature {};
    VbusProtectConfig bus {};
    const TemperatureLookupPoint* ntc_table {nullptr};
    size_t ntc_count {0U};
    float current_frequency_hz {20000.0F};
    float current_filter_alpha {0.9F};
    float telemetry_q_current_sign {1.0F};
    bool input_channels_swapped {false};
    bool brake_enabled {false};
    bool auto_calibration_enabled {false};
};

bool motor_parameters(MotorVariant variant, MotorParameters& output);

}  // namespace algo::motor
