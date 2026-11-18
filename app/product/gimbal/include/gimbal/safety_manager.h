#pragma once

#include <gimbal/types.h>

#include <array>
#include <atomic>
#include <cstdint>

namespace gimbal {

enum class SystemState : uint8_t {
    Boot = 0U,
    SelfTest,
    Heating,
    Standby,
    Arming,
    Active,
    Calibration,
    Fault,
};

enum class Fault : uint32_t {
    None = 0U,
    FactoryParameters = 1U << 0U,
    HallInvalid = 1U << 1U,
    ImuInvalid = 1U << 2U,
    EstimatorInvalid = 1U << 3U,
    HeaterFault = 1U << 4U,
    CommandStale = 1U << 5U,
    Overspeed = 1U << 6U,
    Stall = 1U << 7U,
    JointLimit = 1U << 8U,
    Singularity = 1U << 9U,
    PhaseOvercurrent = 1U << 10U,
    BusVoltage = 1U << 11U,
    DriverOvertemperature = 1U << 12U,
    MosOvertemperature = 1U << 13U,
    FeedbackInvalid = 1U << 14U,
    DeadlineMissed = 1U << 15U,
};

[[nodiscard]] constexpr uint32_t fault_mask(Fault fault)
{
    return static_cast<uint32_t>(fault);
}

struct SafetyConfig {
    float maximum_joint_speed_rad_s {20.0F};
    float stall_command_threshold {0.5F};
    float stall_speed_threshold_rad_s {0.01F};
    float stall_time_s {0.5F};
    float minimum_bus_voltage_v {8.0F};
    float maximum_bus_voltage_v {16.8F};
    float maximum_phase_current_a {5.0F};
    float maximum_driver_temperature_c {100.0F};
    float maximum_mos_temperature_c {110.0F};
    uint32_t sensor_timeout_us {5000U};
    uint32_t command_timeout_us {20000U};
    float self_test_timeout_s {2.0F};
};

struct SafetyInput {
    uint32_t now_us {0U};
    bool factory_parameters_valid {false};
    bool heater_ready {false};
    bool heater_fault {false};
    bool estimator_valid {false};
    bool singular {false};
    bool joint_limit_exceeded {false};
    bool deadline_missed {false};
    JointState joint {};
    ImuSample imu {};
    MotorCommand command {};
    MotorFeedback feedback {};
};

struct FaultContext {
    uint32_t capture_time_us {0U};
    uint32_t faults {0U};
    std::array<float, kAxisCount> joint_angle_rad {};
    std::array<float, kAxisCount> joint_speed_rad_s {};
    std::array<float, kAxisCount> q_axis_command {};
    float imu_temperature_c {0.0F};
    float bus_voltage_v {0.0F};
};

struct SafetyOutput {
    SystemState state {SystemState::Boot};
    uint32_t active_faults {0U};
    uint32_t latched_faults {0U};
    FaultContext first_fault {};
    bool allow_motor_output {false};
};

class SafetyManager {
public:
    [[nodiscard]] bool configure(const SafetyConfig &config,
                                 const CapabilitySet &capabilities);
    void start_self_test();
    void request_arm();
    void request_disarm();
    [[nodiscard]] SafetyOutput update(const SafetyInput &input,
                                      float sample_period_s);
    [[nodiscard]] SafetyOutput output() const { return output_; }

private:
    [[nodiscard]] uint32_t detect_faults(const SafetyInput &input,
                                         float sample_period_s);
    [[nodiscard]] uint32_t detect_core_faults(
        const SafetyInput &input) const;
    [[nodiscard]] uint32_t detect_control_faults(
        const SafetyInput &input, float sample_period_s);
    [[nodiscard]] uint32_t detect_feedback_faults(
        const SafetyInput &input) const;
    void latch_active_faults(const SafetyInput &input);
    void update_state(const SafetyInput &input);

    SafetyConfig config_ {};
    CapabilitySet capabilities_ {};
    SafetyOutput output_ {};
    float stall_elapsed_s_ {0.0F};
    float self_test_elapsed_s_ {0.0F};
    std::atomic<bool> arm_requested_ {false};
    bool is_configured_ {false};
};

[[nodiscard]] bool valid_safety_config(const SafetyConfig &config);

} // namespace gimbal
