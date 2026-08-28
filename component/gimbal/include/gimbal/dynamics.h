#pragma once

#include <gimbal/types.h>

#include <array>

namespace gimbal {

struct DynamicsConfig {
    Matrix3 inertia_matrix {};
    std::array<float, kAxisCount> gravity_torque_nm {};
    std::array<float, kAxisCount> gravity_zero_rad {};
    std::array<float, kAxisCount> viscous_friction_nm_s {};
    std::array<float, kAxisCount> coulomb_friction_nm {};
    std::array<float, kAxisCount> cogging_torque_nm {};
    std::array<uint8_t, kAxisCount> cogging_harmonic {1U, 1U, 1U};
    std::array<float, kAxisCount> torque_constant_nm_a {
        0.05F, 0.05F, 0.05F,
    };
    std::array<float, kAxisCount> phase_resistance_ohm {
        2.0F, 2.0F, 2.0F,
    };
    std::array<float, kAxisCount> back_emf_v_s_rad {
        0.02F, 0.02F, 0.02F,
    };
    std::array<uint8_t, kAxisCount> pole_pairs {7U, 7U, 7U};
    float nominal_bus_voltage_v {12.0F};
};

struct DynamicsOutput {
    std::array<float, kAxisCount> torque_nm {};
    std::array<float, kAxisCount> current_a {};
    std::array<float, kAxisCount> voltage_v {};
    Health health {Health::Invalid};
};

class DynamicsFeedforward {
public:
    [[nodiscard]] bool configure(const DynamicsConfig &config);
    [[nodiscard]] DynamicsOutput calculate(
        const JointState &joint,
        const MotionReference &reference,
        const Matrix3 &camera_motion_to_joint) const;

private:
    DynamicsConfig config_ {};
    bool is_configured_ {false};
};

[[nodiscard]] bool valid_dynamics_config(const DynamicsConfig &config);

} // namespace gimbal
