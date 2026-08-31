#pragma once

#include <algo/spatial_types.h>

namespace algo::spatial {

inline constexpr float kPi = 3.14159265358979323846F;
inline constexpr float kTwoPi = 2.0F * kPi;
inline constexpr float kGravityMps2 = 9.80665F;

[[nodiscard]] float clamp(float value, float minimum, float maximum);
[[nodiscard]] float wrap_pi(float angle_rad);
[[nodiscard]] float dot(const Vector3 &left, const Vector3 &right);
[[nodiscard]] Vector3 cross(const Vector3 &left, const Vector3 &right);
[[nodiscard]] float norm(const Vector3 &value);
[[nodiscard]] Vector3 normalized(const Vector3 &value);
[[nodiscard]] Vector3 add(const Vector3 &left, const Vector3 &right);
[[nodiscard]] Vector3 subtract(const Vector3 &left, const Vector3 &right);
[[nodiscard]] Vector3 scale(const Vector3 &value, float factor);

[[nodiscard]] Quaternion normalized(const Quaternion &value);
[[nodiscard]] Quaternion conjugate(const Quaternion &value);
[[nodiscard]] Quaternion multiply(const Quaternion &left,
                                  const Quaternion &right);
[[nodiscard]] Quaternion from_rotation_vector(const Vector3 &rotation);
[[nodiscard]] Vector3 rotation_vector(const Quaternion &rotation);
[[nodiscard]] Vector3 rotate(const Quaternion &rotation,
                             const Vector3 &value);
[[nodiscard]] Matrix3 rotation_matrix(const Quaternion &rotation);
[[nodiscard]] Quaternion quaternion(const Matrix3 &rotation);

[[nodiscard]] Matrix3 identity_matrix();
[[nodiscard]] Matrix3 transpose(const Matrix3 &value);
[[nodiscard]] Matrix3 multiply(const Matrix3 &left, const Matrix3 &right);
[[nodiscard]] Vector3 multiply(const Matrix3 &left, const Vector3 &right);
[[nodiscard]] float determinant(const Matrix3 &value);
[[nodiscard]] bool inverse(const Matrix3 &value, Matrix3 &result,
                           float minimum_absolute_determinant);
[[nodiscard]] Matrix3 axis_angle(const Vector3 &axis, float angle_rad);

} // namespace algo::spatial
