#pragma once

#include <cstdint>

namespace app {

inline constexpr uint8_t kImpedanceMode = 0xFFU;
inline constexpr uint8_t kAdmittanceMode = 0xFEU;
inline constexpr uint8_t kDragMode = 0xFDU;
inline constexpr uint32_t kNetworkTimeoutUs = 6000U;
enum class JointFault : uint32_t {
    RejectedTarget = 1U << 27, Contact = 1U << 28, SourceTimeout = 1U << 29,
    LocalInhibit = 1U << 30, NetworkTimeout = 1U << 31,
};

struct JointDriveTarget {
    uint16_t controlword {};
    uint8_t mode {};
    int32_t position_urad {};
    int32_t velocity_urad_s {};
    int32_t torque_per_mille {};
    bool extended{}, external_valid{}, support_valid{};
    int16_t support_ma{};
    uint8_t sync{};
    uint32_t sequence{}, source{}, external_sequence{};
    uint16_t source_age_ms{}, external_age_ms{};
    int32_t torque_mnm{}, external_mnm{};
    uint32_t stiffness_milli{}, damping_milli{};
};

struct JointFeedbackSample {
    float position_rad {};
    float velocity_rad_s {};
    float current_a {};
    float bus_voltage_v {};
    uint32_t sample_us {};
    uint32_t sequence {};
    uint32_t faults {}, first_fault{}, fault_us{};
    uint8_t fault_phase{};
    uint32_t applied_sequence{}, applied_us{}, apply_delay_us{}, external_age_us{};
    float output_nm{}, external_nm{};
    uint8_t brake_phase{}, effort_flags{};
    bool applied_valid{};
    bool valid {};
    bool output_enabled {};
};

} // namespace app
