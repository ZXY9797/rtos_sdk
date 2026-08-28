#include <gimbal/math.h>

#include <algorithm>
#include <cmath>

namespace gimbal::math {
namespace {

constexpr float kMinimumNorm = 1.0e-8F;

} // namespace

float clamp(float value, float minimum, float maximum)
{
    return std::clamp(value, minimum, maximum);
}

float wrap_pi(float angle_rad)
{
    if (!std::isfinite(angle_rad)) {
        return 0.0F;
    }
    float wrapped = std::fmod(angle_rad + kPi, kTwoPi);
    if (wrapped < 0.0F) {
        wrapped += kTwoPi;
    }
    return wrapped - kPi;
}

float dot(const Vector3 &left, const Vector3 &right)
{
    return left.x * right.x + left.y * right.y + left.z * right.z;
}

Vector3 cross(const Vector3 &left, const Vector3 &right)
{
    return {
        left.y * right.z - left.z * right.y,
        left.z * right.x - left.x * right.z,
        left.x * right.y - left.y * right.x,
    };
}

float norm(const Vector3 &value)
{
    return std::sqrt(dot(value, value));
}

Vector3 normalized(const Vector3 &value)
{
    const float length = norm(value);
    if (!std::isfinite(length) || length < kMinimumNorm) {
        return {};
    }
    return scale(value, 1.0F / length);
}

Vector3 add(const Vector3 &left, const Vector3 &right)
{
    return {left.x + right.x, left.y + right.y, left.z + right.z};
}

Vector3 subtract(const Vector3 &left, const Vector3 &right)
{
    return {left.x - right.x, left.y - right.y, left.z - right.z};
}

Vector3 scale(const Vector3 &value, float factor)
{
    return {value.x * factor, value.y * factor, value.z * factor};
}

Quaternion normalized(const Quaternion &value)
{
    const float length_squared = value.w * value.w + value.x * value.x
                               + value.y * value.y + value.z * value.z;
    if (!std::isfinite(length_squared)
        || length_squared < kMinimumNorm * kMinimumNorm) {
        return {};
    }
    const float inverse_length = 1.0F / std::sqrt(length_squared);
    return {
        value.w * inverse_length,
        value.x * inverse_length,
        value.y * inverse_length,
        value.z * inverse_length,
    };
}

Quaternion conjugate(const Quaternion &value)
{
    return {value.w, -value.x, -value.y, -value.z};
}

Quaternion multiply(const Quaternion &left, const Quaternion &right)
{
    return {
        left.w * right.w - left.x * right.x - left.y * right.y
            - left.z * right.z,
        left.w * right.x + left.x * right.w + left.y * right.z
            - left.z * right.y,
        left.w * right.y - left.x * right.z + left.y * right.w
            + left.z * right.x,
        left.w * right.z + left.x * right.y - left.y * right.x
            + left.z * right.w,
    };
}

Quaternion from_rotation_vector(const Vector3 &rotation)
{
    const float angle = norm(rotation);
    if (!std::isfinite(angle) || angle < kMinimumNorm) {
        const Vector3 half = scale(rotation, 0.5F);
        return normalized({1.0F, half.x, half.y, half.z});
    }
    const float half_angle = 0.5F * angle;
    const float gain = std::sin(half_angle) / angle;
    return {
        std::cos(half_angle),
        rotation.x * gain,
        rotation.y * gain,
        rotation.z * gain,
    };
}

Vector3 rotation_vector(const Quaternion &rotation)
{
    Quaternion unit = normalized(rotation);
    if (unit.w < 0.0F) {
        unit = {-unit.w, -unit.x, -unit.y, -unit.z};
    }
    const float vector_norm = std::sqrt(
        unit.x * unit.x + unit.y * unit.y + unit.z * unit.z);
    if (vector_norm < kMinimumNorm) {
        return {2.0F * unit.x, 2.0F * unit.y, 2.0F * unit.z};
    }
    const float angle = 2.0F * std::atan2(vector_norm, unit.w);
    const float gain = angle / vector_norm;
    return {unit.x * gain, unit.y * gain, unit.z * gain};
}

Vector3 rotate(const Quaternion &rotation, const Vector3 &value)
{
    const Quaternion vector {0.0F, value.x, value.y, value.z};
    const Quaternion result = multiply(
        multiply(normalized(rotation), vector),
        conjugate(normalized(rotation)));
    return {result.x, result.y, result.z};
}

Matrix3 rotation_matrix(const Quaternion &rotation)
{
    const Quaternion value = normalized(rotation);
    const float xx = value.x * value.x;
    const float yy = value.y * value.y;
    const float zz = value.z * value.z;
    const float xy = value.x * value.y;
    const float xz = value.x * value.z;
    const float yz = value.y * value.z;
    const float wx = value.w * value.x;
    const float wy = value.w * value.y;
    const float wz = value.w * value.z;
    Matrix3 result {};
    result.value[0][0] = 1.0F - 2.0F * (yy + zz);
    result.value[0][1] = 2.0F * (xy - wz);
    result.value[0][2] = 2.0F * (xz + wy);
    result.value[1][0] = 2.0F * (xy + wz);
    result.value[1][1] = 1.0F - 2.0F * (xx + zz);
    result.value[1][2] = 2.0F * (yz - wx);
    result.value[2][0] = 2.0F * (xz - wy);
    result.value[2][1] = 2.0F * (yz + wx);
    result.value[2][2] = 1.0F - 2.0F * (xx + yy);
    return result;
}

Quaternion quaternion(const Matrix3 &rotation)
{
    const float trace = rotation.value[0][0] + rotation.value[1][1]
                      + rotation.value[2][2];
    if (trace > 0.0F) {
        const float scale_value = 2.0F * std::sqrt(trace + 1.0F);
        return normalized({
            0.25F * scale_value,
            (rotation.value[2][1] - rotation.value[1][2]) / scale_value,
            (rotation.value[0][2] - rotation.value[2][0]) / scale_value,
            (rotation.value[1][0] - rotation.value[0][1]) / scale_value,
        });
    }
    if (rotation.value[0][0] > rotation.value[1][1]
        && rotation.value[0][0] > rotation.value[2][2]) {
        const float scale_value = 2.0F * std::sqrt(
            1.0F + rotation.value[0][0] - rotation.value[1][1]
            - rotation.value[2][2]);
        return normalized({
            (rotation.value[2][1] - rotation.value[1][2]) / scale_value,
            0.25F * scale_value,
            (rotation.value[0][1] + rotation.value[1][0]) / scale_value,
            (rotation.value[0][2] + rotation.value[2][0]) / scale_value,
        });
    }
    if (rotation.value[1][1] > rotation.value[2][2]) {
        const float scale_value = 2.0F * std::sqrt(
            1.0F + rotation.value[1][1] - rotation.value[0][0]
            - rotation.value[2][2]);
        return normalized({
            (rotation.value[0][2] - rotation.value[2][0]) / scale_value,
            (rotation.value[0][1] + rotation.value[1][0]) / scale_value,
            0.25F * scale_value,
            (rotation.value[1][2] + rotation.value[2][1]) / scale_value,
        });
    }
    const float scale_value = 2.0F * std::sqrt(
        1.0F + rotation.value[2][2] - rotation.value[0][0]
        - rotation.value[1][1]);
    return normalized({
        (rotation.value[1][0] - rotation.value[0][1]) / scale_value,
        (rotation.value[0][2] + rotation.value[2][0]) / scale_value,
        (rotation.value[1][2] + rotation.value[2][1]) / scale_value,
        0.25F * scale_value,
    });
}

Matrix3 identity_matrix()
{
    return {};
}

Matrix3 transpose(const Matrix3 &value)
{
    Matrix3 result {};
    for (size_t row = 0U; row < 3U; ++row) {
        for (size_t column = 0U; column < 3U; ++column) {
            result.value[row][column] = value.value[column][row];
        }
    }
    return result;
}

Matrix3 multiply(const Matrix3 &left, const Matrix3 &right)
{
    Matrix3 result {};
    for (size_t row = 0U; row < 3U; ++row) {
        for (size_t column = 0U; column < 3U; ++column) {
            result.value[row][column] = 0.0F;
            for (size_t inner = 0U; inner < 3U; ++inner) {
                result.value[row][column] +=
                    left.value[row][inner] * right.value[inner][column];
            }
        }
    }
    return result;
}

Vector3 multiply(const Matrix3 &left, const Vector3 &right)
{
    return {
        left.value[0][0] * right.x + left.value[0][1] * right.y
            + left.value[0][2] * right.z,
        left.value[1][0] * right.x + left.value[1][1] * right.y
            + left.value[1][2] * right.z,
        left.value[2][0] * right.x + left.value[2][1] * right.y
            + left.value[2][2] * right.z,
    };
}

float determinant(const Matrix3 &value)
{
    return value.value[0][0]
             * (value.value[1][1] * value.value[2][2]
                - value.value[1][2] * value.value[2][1])
         - value.value[0][1]
             * (value.value[1][0] * value.value[2][2]
                - value.value[1][2] * value.value[2][0])
         + value.value[0][2]
             * (value.value[1][0] * value.value[2][1]
                - value.value[1][1] * value.value[2][0]);
}

bool inverse(const Matrix3 &value, Matrix3 &result,
             float minimum_absolute_determinant)
{
    const float matrix_determinant = determinant(value);
    if (!std::isfinite(matrix_determinant)
        || !std::isfinite(minimum_absolute_determinant)
        || minimum_absolute_determinant <= 0.0F
        || std::abs(matrix_determinant)
            < minimum_absolute_determinant) {
        return false;
    }
    const float gain = 1.0F / matrix_determinant;
    result.value[0][0] = gain
        * (value.value[1][1] * value.value[2][2]
           - value.value[1][2] * value.value[2][1]);
    result.value[0][1] = gain
        * (value.value[0][2] * value.value[2][1]
           - value.value[0][1] * value.value[2][2]);
    result.value[0][2] = gain
        * (value.value[0][1] * value.value[1][2]
           - value.value[0][2] * value.value[1][1]);
    result.value[1][0] = gain
        * (value.value[1][2] * value.value[2][0]
           - value.value[1][0] * value.value[2][2]);
    result.value[1][1] = gain
        * (value.value[0][0] * value.value[2][2]
           - value.value[0][2] * value.value[2][0]);
    result.value[1][2] = gain
        * (value.value[0][2] * value.value[1][0]
           - value.value[0][0] * value.value[1][2]);
    result.value[2][0] = gain
        * (value.value[1][0] * value.value[2][1]
           - value.value[1][1] * value.value[2][0]);
    result.value[2][1] = gain
        * (value.value[0][1] * value.value[2][0]
           - value.value[0][0] * value.value[2][1]);
    result.value[2][2] = gain
        * (value.value[0][0] * value.value[1][1]
           - value.value[0][1] * value.value[1][0]);
    for (const auto &row : result.value) {
        for (float element : row) {
            if (!std::isfinite(element)) return false;
        }
    }
    return true;
}

Matrix3 axis_angle(const Vector3 &axis, float angle_rad)
{
    const Vector3 unit = normalized(axis);
    const float sine = std::sin(angle_rad);
    const float cosine = std::cos(angle_rad);
    const float one_minus_cosine = 1.0F - cosine;
    Matrix3 result {};
    result.value[0][0] = cosine + unit.x * unit.x * one_minus_cosine;
    result.value[0][1] = unit.x * unit.y * one_minus_cosine - unit.z * sine;
    result.value[0][2] = unit.x * unit.z * one_minus_cosine + unit.y * sine;
    result.value[1][0] = unit.y * unit.x * one_minus_cosine + unit.z * sine;
    result.value[1][1] = cosine + unit.y * unit.y * one_minus_cosine;
    result.value[1][2] = unit.y * unit.z * one_minus_cosine - unit.x * sine;
    result.value[2][0] = unit.z * unit.x * one_minus_cosine - unit.y * sine;
    result.value[2][1] = unit.z * unit.y * one_minus_cosine + unit.x * sine;
    result.value[2][2] = cosine + unit.z * unit.z * one_minus_cosine;
    return result;
}

} // namespace gimbal::math
