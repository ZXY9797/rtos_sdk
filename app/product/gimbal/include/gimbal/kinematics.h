#pragma once

#include <gimbal/types.h>

#include <array>

namespace gimbal {

struct KinematicConfig {
    std::array<Vector3, kAxisCount> joint_axis_handle {
        Vector3 {1.0F, 0.0F, 0.0F},
        Vector3 {0.0F, 1.0F, 0.0F},
        Vector3 {0.0F, 0.0F, 1.0F},
    };
    Matrix3 camera_home_rotation {};
    float minimum_jacobian_determinant {0.05F};
};

class NonOrthogonalKinematics {
public:
    [[nodiscard]] bool configure(const KinematicConfig &config);
    [[nodiscard]] Matrix3 camera_in_handle(
        const std::array<float, kAxisCount> &joint_angle_rad) const;
    [[nodiscard]] Matrix3 angular_jacobian_handle(
        const std::array<float, kAxisCount> &joint_angle_rad) const;
    [[nodiscard]] bool calculate_control_mappings(
        const std::array<float, kAxisCount> &joint_angle_rad,
        Matrix3 &camera_effort_to_joint,
        Matrix3 &camera_motion_to_joint) const;
    [[nodiscard]] HandleState estimate_handle(
        const AttitudeState &camera,
        const JointState &joint,
        uint32_t publish_time_us) const;
    [[nodiscard]] bool is_singular(
        const std::array<float, kAxisCount> &joint_angle_rad) const;

private:
    KinematicConfig config_ {};
    bool is_configured_ {false};
};

[[nodiscard]] bool valid_kinematic_config(const KinematicConfig &config);

} // namespace gimbal
