#pragma once

#include <cstddef>
#include <cmath>
#include <stdint.h>

namespace algo::motor::basic_math {

inline constexpr float kPi = 3.1415926F;
inline constexpr float kTwoPi = 2.0F * kPi;
inline constexpr float kOneDivSqrt3 = 0.577350F;
inline constexpr float kSqrt3Over2 = 0.866025F;
inline constexpr float kSqrt3 = 1.732051F;
inline constexpr float kMinTmrAmp = 0.000001F;

inline float clamp_float(float value, float min, float max)
{
    if (value < min) {
        return min;
    }
    if (value > max) {
        return max;
    }
    return value;
}

inline bool is_nonzero_float(float value, float epsilon = 1.0e-6F)
{
    return fabsf(value) > epsilon;
}

inline bool is_finite_float(float value)
{
    return std::isfinite(value);
}

inline float clamp_symmetric(float value, float limit_abs)
{
    if (!is_finite_float(value)) {
        return 0.0F;
    }
    if (!is_finite_float(limit_abs) || (limit_abs <= 0.0F)) {
        return value;
    }
    return clamp_float(value, -limit_abs, limit_abs);
}

/*
 * 计算 uint32_t 微秒时间戳差值。
 * 支持自然回绕；超过 INT32_MAX 的差值按负数处理，用于识别乱序或异常时间戳。
 */
inline int32_t timestamp_delta_us(uint32_t current_us, uint32_t last_us)
{
    constexpr uint32_t kInt32MaxU = 0x7FFFFFFFU;
    constexpr int64_t kUint32Range = 0x100000000LL;
    const uint32_t delta = current_us - last_us;
    if (delta <= kInt32MaxU) {
        return static_cast<int32_t>(delta);
    }
    return static_cast<int32_t>(static_cast<int64_t>(delta) - kUint32Range);
}

inline float normalize_angle_0_to_2pi(float angle_rad)
{
    float output = fmodf(angle_rad, kTwoPi);
    if (output < 0.0F) {
        output += kTwoPi;
    }
    return output;
}

inline float wrap_angle_minus_pi_to_pi(float angle_rad)
{
    float output = fmodf(angle_rad + kPi, kTwoPi);
    if (output < 0.0F) {
        output += kTwoPi;
    }
    return output - kPi;
}

enum class LagrangeInterpOrder : uint8_t {
    First = 1U,
    Second = 2U,
};

struct UniformLagrangeInterpConfig {
    LagrangeInterpOrder order = LagrangeInterpOrder::Second;
    float x_min = 0.0F;
    float x_max = 0.0F;
    size_t point_count = 0U;
};

inline bool uniform_lagrange_interpolate(const UniformLagrangeInterpConfig& config,
                                         const float* y_points,
                                         float target,
                                         float* result)
{
    if ((y_points == nullptr) || (result == nullptr) ||
        !is_finite_float(target) ||
        !is_finite_float(config.x_min) ||
        !is_finite_float(config.x_max) ||
        (config.x_max <= config.x_min) ||
        (config.point_count < 2U)) {
        return false;
    }
    if ((target < config.x_min) || (target > config.x_max)) {
        return false;
    }

    const float dx = (config.x_max - config.x_min) /
                     static_cast<float>(config.point_count);
    if (dx <= 0.0F) {
        return false;
    }

    int index = static_cast<int>((target - config.x_min) / dx) %
                static_cast<int>(config.point_count);
    if (index < 0) {
        index = 0;
    }

    if (config.order == LagrangeInterpOrder::First) {
        if (config.point_count < 2U) {
            return false;
        }
        if (index >= static_cast<int>(config.point_count - 1U)) {
            index = static_cast<int>(config.point_count - 2U);
        }

        const float x0 = config.x_min + (static_cast<float>(index) * dx);
        const float x1 = config.x_min + (static_cast<float>(index + 1) * dx);
        const float y0 = y_points[index];
        const float y1 = y_points[index + 1];
        const float denom = x1 - x0;
        if (fabsf(denom) < 1.0e-9F) {
            *result = y0;
            return true;
        }

        const float w0 = (x1 - target) / denom;
        const float w1 = (target - x0) / denom;
        *result = (y0 * w0) + (y1 * w1);
        return is_finite_float(*result);
    }

    if ((config.order != LagrangeInterpOrder::Second) ||
        (config.point_count < 3U)) {
        return false;
    }

    const float range = config.x_max - config.x_min;
    float relative_target = target - config.x_min;
    if (relative_target >= range) {
        relative_target = 0.0F;
    }
    int center_index = static_cast<int>(relative_target / dx) %
                       static_cast<int>(config.point_count);
    if (center_index < 0) {
        center_index = 0;
    }

    const int start_index = center_index - 1;
    const size_t i0 = static_cast<size_t>(
        (start_index < 0) ? static_cast<int>(config.point_count - 1U) : start_index);
    const size_t i1 = static_cast<size_t>(center_index);
    const size_t i2 = (static_cast<size_t>(center_index) + 1U) % config.point_count;
    const float x0 = config.x_min + (static_cast<float>(start_index) * dx);
    const float x1 = x0 + dx;
    const float x2 = x1 + dx;
    const float y0 = y_points[i0];
    const float y1 = y_points[i1];
    const float y2 = y_points[i2];
    const float target_x = config.x_min + relative_target;

    const float denom0 = (x0 - x1) * (x0 - x2);
    const float denom1 = (x1 - x0) * (x1 - x2);
    const float denom2 = (x2 - x0) * (x2 - x1);
    if ((fabsf(denom0) < 1.0e-9F) ||
        (fabsf(denom1) < 1.0e-9F) ||
        (fabsf(denom2) < 1.0e-9F)) {
        const float denom = x2 - x1;
        if (fabsf(denom) < 1.0e-9F) {
            *result = y1;
        } else {
            *result = y1 + ((y2 - y1) * (target - x1) / denom);
        }
        return is_finite_float(*result);
    }

    const float w0 = ((target_x - x1) * (target_x - x2)) / denom0;
    const float w1 = ((target_x - x0) * (target_x - x2)) / denom1;
    const float w2 = ((target_x - x0) * (target_x - x1)) / denom2;
    *result = (y0 * w0) + (y1 * w1) + (y2 * w2);
    return is_finite_float(*result);
}

struct LinearMap {
    float input_min = 0.0F;
    float input_max = 0.0F;
    float output_min = 0.0F;
    float output_max = 0.0F;
};

inline float linear_map_update(const LinearMap& map, float input)
{
    if (map.input_max == map.input_min) {
        return map.output_min;
    }

    const bool forward = map.input_max > map.input_min;
    if ((forward && (input <= map.input_min)) ||
        (!forward && (input >= map.input_min))) {
        return map.output_min;
    }
    if ((forward && (input >= map.input_max)) ||
        (!forward && (input <= map.input_max))) {
        return map.output_max;
    }

    const float ratio =
        (input - map.input_min) / (map.input_max - map.input_min);
    return map.output_min + (ratio * (map.output_max - map.output_min));
}

}  // namespace algo::motor::basic_math
