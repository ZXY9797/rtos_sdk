#pragma once

#include <gimbal/attitude_ekf.h>
#include <gimbal/controller.h>
#include <gimbal/dynamics.h>
#include <gimbal/hall_sensor.h>
#include <gimbal/kinematics.h>
#include <gimbal/motion_planner.h>
#include <gimbal/safety_manager.h>
#include <gimbal/thermal_controller.h>
#include <gimbal/types.h>
#include <gimbal/voltage_foc.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace gimbal {

inline constexpr uint32_t kFactoryParameterMagic = 0x314D4947U;
inline constexpr uint32_t kUserParameterMagic = 0x31525547U;
inline constexpr uint16_t kFactoryParameterSchema = 5U;
inline constexpr uint16_t kUserParameterSchema = 1U;
inline constexpr uint32_t kParameterCommitMarker = 0x434F4D4DU;

struct FactoryParameters {
    std::array<HallCalibration, kAxisCount> hall {};
    ImuCalibration imu {};
    AttitudeEkfConfig ekf {};
    ThermalConfig thermal {};
    KinematicConfig kinematics {};
    DynamicsConfig dynamics {};
    ControllerConfig controller {};
    MotionLimits factory_motion_limits {};
    Vector3 maximum_follow_deadband_rad {0.5F, 0.5F, 0.5F};
    Vector3 maximum_follow_gain {2.0F, 2.0F, 2.0F};
    SafetyConfig safety {};
    std::array<VoltageFocConfig, kAxisCount> voltage_foc {};
    CapabilitySet capabilities {};
    uint32_t hardware_revision {0U};
};

struct UserParameters {
    ProductMode mode {ProductMode::Follow};
    MotionLimits motion_limits {};
    Vector3 follow_deadband_rad {0.03F, 0.03F, 0.05F};
    Vector3 follow_gain {0.5F, 0.5F, 0.7F};
};

template <typename Payload>
struct ParameterRecord {
    uint32_t magic {0U};
    uint16_t schema {0U};
    uint16_t payload_size {sizeof(Payload)};
    uint32_t generation {0U};
    Payload payload {};
    uint32_t crc32 {0U};
    uint32_t commit_marker {0U};
};

static_assert(std::is_trivially_copyable_v<FactoryParameters>);
static_assert(std::is_trivially_copyable_v<UserParameters>);

[[nodiscard]] uint32_t parameter_crc32(const uint8_t *data, size_t length);
[[nodiscard]] bool valid_factory_parameters(
    const FactoryParameters &parameters);
[[nodiscard]] bool valid_user_parameters(
    const UserParameters &parameters,
    const FactoryParameters &factory_parameters);

template <typename Payload>
[[nodiscard]] ParameterRecord<Payload> make_parameter_record(
    uint32_t magic, uint16_t schema, uint32_t generation,
    const Payload &payload)
{
    static_assert(sizeof(Payload) <= UINT16_MAX);
    ParameterRecord<Payload> record {};
    record.magic = magic;
    record.schema = schema;
    record.payload_size = static_cast<uint16_t>(sizeof(Payload));
    record.generation = generation;
    record.payload = payload;
    const auto *bytes = reinterpret_cast<const uint8_t *>(&record);
    record.crc32 = parameter_crc32(
        bytes, offsetof(ParameterRecord<Payload>, crc32));
    record.commit_marker = kParameterCommitMarker;
    return record;
}

template <typename Payload>
[[nodiscard]] bool valid_parameter_record(
    const ParameterRecord<Payload> &record,
    uint32_t magic, uint16_t schema)
{
    static_assert(sizeof(Payload) <= UINT16_MAX);
    if (record.magic != magic || record.schema != schema
        || record.payload_size != sizeof(Payload)
        || record.commit_marker != kParameterCommitMarker) {
        return false;
    }
    const auto *bytes = reinterpret_cast<const uint8_t *>(&record);
    const uint32_t expected = parameter_crc32(
        bytes, offsetof(ParameterRecord<Payload>, crc32));
    return record.crc32 == expected;
}

} // namespace gimbal
