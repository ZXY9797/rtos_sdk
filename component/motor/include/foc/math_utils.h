#pragma once

#include "types.h"
#include <cstdint>

namespace foc {

// 正弦/余弦查表 (16-bit 角度 → float)
void sin_cos(uint16_t angle, float &s, float &c);

// 快速 atan2
float fast_atan2(float y, float x);

// Clarke 变换: 三相 → αβ
Vec2 clarke_transform(float iu, float iv, float iw);

// Park 变换: αβ → dq
Polar park_transform(const Vec2 &iab, const SinCos &sc);

// 逆 Park 变换: dq → αβ
Vec2 inverse_park(const Polar &vdq, const SinCos &sc);

// 电感解耦 (MTPA/弱磁用)
void get_lab(uint16_t angle, float ld, float lq_minus_ld, float &la, float &lb);

// 角度归一化
constexpr uint16_t normalize_angle(int32_t angle) {
    return static_cast<uint16_t>(angle & 0xFFFF);
}

// 弧度 ↔ 16-bit 角度
constexpr float angle_to_rad(uint16_t angle) {
    return static_cast<float>(angle) * (2.0f * 3.14159265f / 65536.0f);
}

constexpr uint16_t rad_to_angle(float rad) {
    return static_cast<uint16_t>(rad * (65536.0f / (2.0f * 3.14159265f)));
}

// RPM ↔ 电频率
constexpr float rpm_to_ehz(float rpm, uint8_t pole_pairs) {
    return rpm * static_cast<float>(pole_pairs) / 60.0f;
}

constexpr float ehz_to_rpm(float ehz, uint8_t pole_pairs) {
    return ehz * 60.0f / static_cast<float>(pole_pairs);
}

} // namespace foc
