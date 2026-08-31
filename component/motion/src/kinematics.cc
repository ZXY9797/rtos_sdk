#include <gimbal/kinematics.h>

#include <gimbal/math.h>

#include <cmath>

namespace gimbal {
namespace {

constexpr float kAxisNormTolerance = 0.02F;
constexpr float kRotationDeterminantTolerance = 0.05F;

[[nodiscard]] bool finite_joint_angles(
    const std::array<float, kAxisCount> &angles)
{
    for (float angle : angles) {
        if (!std::isfinite(angle)) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool finite_vector(const Vector3 &value)
{
    return std::isfinite(value.x) && std::isfinite(value.y)
        && std::isfinite(value.z);
}

[[nodiscard]] bool finite_quaternion(const Quaternion &value)
{
    return std::isfinite(value.w) && std::isfinite(value.x)
        && std::isfinite(value.y) && std::isfinite(value.z);
}

void set_column(Matrix3 &matrix, size_t column, const Vector3 &value)
{
    matrix.value[0][column] = value.x;
    matrix.value[1][column] = value.y;
    matrix.value[2][column] = value.z;
}

} // namespace

bool valid_kinematic_config(const KinematicConfig &config)
{
    for (const Vector3 &axis : config.joint_axis_handle) {
        const float axis_norm = math::norm(axis);
        if (!std::isfinite(axis_norm)
            || std::abs(axis_norm - 1.0F) > kAxisNormTolerance) {
            return false;
        }
    }
    const Matrix3 transpose = math::transpose(config.camera_home_rotation);
    const Matrix3 orthogonality = math::multiply(
        transpose, config.camera_home_rotation);
    for (size_t row = 0U; row < kAxisCount; ++row) {
        for (size_t column = 0U; column < kAxisCount; ++column) {
            const float expected = row == column ? 1.0F : 0.0F;
            if (!std::isfinite(orthogonality.value[row][column])
                || std::abs(orthogonality.value[row][column] - expected)
                    > kRotationDeterminantTolerance) {
                return false;
            }
        }
    }
    const float home_determinant =
        math::determinant(config.camera_home_rotation);
    return std::isfinite(home_determinant)
        && std::abs(home_determinant - 1.0F)
               <= kRotationDeterminantTolerance
        && std::isfinite(config.minimum_jacobian_determinant)
        && config.minimum_jacobian_determinant > 0.0F;
}

bool NonOrthogonalKinematics::configure(const KinematicConfig &config)
{
    if (!valid_kinematic_config(config)) {
        is_configured_ = false;
        return false;
    }
    config_ = config;
    is_configured_ = true;
    return true;
}

Matrix3 NonOrthogonalKinematics::camera_in_handle(
    const std::array<float, kAxisCount> &joint_angle_rad) const
{
    if (!is_configured_) {
        return math::identity_matrix();
    }
    Matrix3 rotation = math::identity_matrix();
    for (size_t axis = 0U; axis < kAxisCount; ++axis) {
        rotation = math::multiply(
            rotation,
            math::axis_angle(config_.joint_axis_handle[axis],
                             joint_angle_rad[axis]));
    }
    return math::multiply(rotation, config_.camera_home_rotation);
}

Matrix3 NonOrthogonalKinematics::angular_jacobian_handle(
    const std::array<float, kAxisCount> &joint_angle_rad) const
{
    Matrix3 jacobian {};
    Matrix3 preceding_rotation = math::identity_matrix();
    for (size_t axis = 0U; axis < kAxisCount; ++axis) {
        set_column(jacobian, axis, math::multiply(
            preceding_rotation, config_.joint_axis_handle[axis]));
        preceding_rotation = math::multiply(
            preceding_rotation,
            math::axis_angle(config_.joint_axis_handle[axis],
                             joint_angle_rad[axis]));
    }
    return jacobian;
}

bool NonOrthogonalKinematics::calculate_control_mappings(
    const std::array<float, kAxisCount> &joint_angle_rad,
    Matrix3 &camera_effort_to_joint,
    Matrix3 &camera_motion_to_joint) const
{
    if (!is_configured_ || !finite_joint_angles(joint_angle_rad)) {
        return false;
    }
    const Matrix3 camera_rotation = camera_in_handle(joint_angle_rad);
    const Matrix3 jacobian = angular_jacobian_handle(joint_angle_rad);
    Matrix3 inverse_jacobian {};
    if (!math::inverse(jacobian, inverse_jacobian,
                       config_.minimum_jacobian_determinant)) {
        return false;
    }
    camera_effort_to_joint = math::multiply(
        math::transpose(jacobian), camera_rotation);
    camera_motion_to_joint = math::multiply(
        inverse_jacobian, camera_rotation);
    return true;
}

HandleState NonOrthogonalKinematics::estimate_handle(
    const AttitudeState &camera, const JointState &joint,
    uint32_t publish_time_us) const
{
    HandleState output {};
    output.header.sequence = camera.header.sequence;
    output.header.capture_time_us = camera.header.capture_time_us;
    output.header.publish_time_us = publish_time_us;
    if (!is_configured_ || camera.header.health != Health::Valid
        || joint.header.health != Health::Valid
        || joint.valid_axis_mask != kAllAxesMask
        || !finite_quaternion(camera.camera_in_world)
        || !finite_vector(camera.angular_rate_world_rad_s)
        || !finite_joint_angles(joint.angle_rad)
        || !finite_joint_angles(joint.speed_rad_s)) {
        output.header.health = Health::Invalid;
        return output;
    }
    const Matrix3 rotation_world_camera =
        math::rotation_matrix(camera.camera_in_world);
    const Matrix3 rotation_handle_camera =
        camera_in_handle(joint.angle_rad);
    const Matrix3 rotation_world_handle = math::multiply(
        rotation_world_camera,
        math::transpose(rotation_handle_camera));
    const Matrix3 jacobian = angular_jacobian_handle(joint.angle_rad);
    const Vector3 joint_speed {
        joint.speed_rad_s[0],
        joint.speed_rad_s[1],
        joint.speed_rad_s[2],
    };
    const Vector3 relative_rate_handle =
        math::multiply(jacobian, joint_speed);
    const Vector3 relative_rate_world =
        math::multiply(rotation_world_handle, relative_rate_handle);
    output.handle_in_world = math::quaternion(rotation_world_handle);
    output.angular_rate_world_rad_s = math::subtract(
        camera.angular_rate_world_rad_s, relative_rate_world);
    output.jacobian_determinant = math::determinant(jacobian);
    output.header.valid_flags = kAllValidFlags;
    output.header.health = std::abs(output.jacobian_determinant)
            < config_.minimum_jacobian_determinant
        ? Health::Degraded
        : Health::Valid;
    return output;
}

bool NonOrthogonalKinematics::is_singular(
    const std::array<float, kAxisCount> &joint_angle_rad) const
{
    if (!is_configured_) {
        return true;
    }
    if (!finite_joint_angles(joint_angle_rad)) {
        return true;
    }
    const Matrix3 jacobian = angular_jacobian_handle(joint_angle_rad);
    return std::abs(math::determinant(jacobian))
        < config_.minimum_jacobian_determinant;
}

} // namespace gimbal
