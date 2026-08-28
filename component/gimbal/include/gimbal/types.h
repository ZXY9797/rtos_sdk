#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace gimbal {

inline constexpr size_t kAxisCount = 3U;
inline constexpr uint16_t kTopicSchemaVersion = 1U;
inline constexpr uint8_t kAllAxesMask =
    static_cast<uint8_t>((1U << kAxisCount) - 1U);
inline constexpr uint32_t kAllValidFlags = UINT32_MAX;

enum class Axis : uint8_t {
    Roll = 0U,
    Pitch = 1U,
    Yaw = 2U,
};

enum class Health : uint8_t {
    Invalid = 0U,
    Degraded,
    Valid,
    Fault,
};

enum class MotorControlMode : uint8_t {
    VoltageFoc = 0U,
    CurrentFoc,
};

enum class ProductMode : uint8_t {
    Lock = 0U,
    Follow,
    Fpv,
    Recenter,
};

struct Vector2 {
    float x {0.0F};
    float y {0.0F};
};

struct Vector3 {
    float x {0.0F};
    float y {0.0F};
    float z {0.0F};
};

struct Quaternion {
    float w {1.0F};
    float x {0.0F};
    float y {0.0F};
    float z {0.0F};
};

struct Matrix3 {
    float value[3][3] {
        {1.0F, 0.0F, 0.0F},
        {0.0F, 1.0F, 0.0F},
        {0.0F, 0.0F, 1.0F},
    };
};

struct TopicHeader {
    uint16_t schema_version {kTopicSchemaVersion};
    uint16_t reserved {0U};
    uint32_t sequence {0U};
    uint32_t capture_time_us {0U};
    uint32_t publish_time_us {0U};
    uint32_t valid_flags {0U};
    Health health {Health::Invalid};
    uint8_t padding[3] {};
};

struct CapabilitySet {
    MotorControlMode motor_mode {MotorControlMode::VoltageFoc};
    bool has_phase_current {false};
    bool has_bus_voltage {false};
    bool has_driver_temperature {false};
    bool has_mos_temperature {false};
    bool has_imu_heater {true};
    uint8_t reserved[2] {};
};

struct ImuSample {
    TopicHeader header {};
    Vector3 acceleration_mps2 {};
    Vector3 angular_rate_rad_s {};
    float temperature_c {0.0F};
};

struct JointState {
    TopicHeader header {};
    std::array<float, kAxisCount> angle_rad {};
    std::array<float, kAxisCount> speed_rad_s {};
    std::array<float, kAxisCount> electrical_angle_rad {};
    uint8_t valid_axis_mask {0U};
    uint8_t reserved[3] {};
};

struct AttitudeState {
    TopicHeader header {};
    Quaternion camera_in_world {};
    Vector3 angular_rate_world_rad_s {};
    Vector3 gyro_bias_rad_s {};
    Vector3 attitude_variance {};
};

struct HandleState {
    TopicHeader header {};
    Quaternion handle_in_world {};
    Vector3 angular_rate_world_rad_s {};
    float jacobian_determinant {0.0F};
};

struct MotionReference {
    TopicHeader header {};
    Quaternion attitude {};
    Vector3 angular_rate_rad_s {};
    Vector3 angular_acceleration_rad_s2 {};
};

struct MotorCommand {
    TopicHeader header {};
    std::array<float, kAxisCount> d_axis_command {};
    std::array<float, kAxisCount> q_axis_command {};
    bool enable {false};
    uint8_t reserved[3] {};
};

struct MotorFeedback {
    TopicHeader header {};
    std::array<float, kAxisCount> applied_q_axis {};
    std::array<float, kAxisCount> phase_current_a {};
    float bus_voltage_v {0.0F};
    std::array<float, kAxisCount> driver_temperature_c {};
    std::array<float, kAxisCount> mos_temperature_c {};
};

struct ImuThermalState {
    TopicHeader header {};
    float temperature_c {0.0F};
    float heater_duty {0.0F};
    bool is_stable {false};
    uint8_t reserved[3] {};
};

} // namespace gimbal
