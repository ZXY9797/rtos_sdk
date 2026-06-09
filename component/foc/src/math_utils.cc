#include <foc/math_utils.h>
#include <cmath>

namespace foc {

// 编译期正弦查表 (256 entries, 16-bit angle → float)
static constexpr int SIN_LUT_SIZE = 256;

static constexpr float sin_approx(float x) {
    // 5 阶 Taylor 展开, |x| <= pi/2
    float x2 = x * x;
    return x * (1.0f - x2 * (1.0f / 6.0f - x2 * (1.0f / 120.0f
             - x2 / 5040.0f)));
}

static constexpr float sin_0_2pi(float angle_rad) {
    // 归一化到 [0, 2*pi)
    float pi = 3.14159265f;
    float half_pi = 1.57079632f;
    // 利用 sin 的对称性映射到 [0, pi/2]
    float a = angle_rad;
    if (a < 0.0f) a += 2.0f * pi;
    int quadrant = static_cast<int>(a / half_pi);
    float remainder = a - static_cast<float>(quadrant) * half_pi;
    float s;
    switch (quadrant % 4) {
    case 0: s = sin_approx(remainder); break;
    case 1: s = sin_approx(half_pi - remainder); break;
    case 2: s = -sin_approx(remainder); break;
    default: s = -sin_approx(half_pi - remainder); break;
    }
    return s;
}

// 生成 sin_lut 的辅助模板 (C++14 constexpr)
template<int N, int I>
struct SinLutFiller {
    static constexpr void fill(float *arr) {
        arr[I] = sin_0_2pi(static_cast<float>(I) * 2.0f * 3.14159265f / N);
        SinLutFiller<N, I + 1>::fill(arr);
    }
};

template<int N>
struct SinLutFiller<N, N> {
    static constexpr void fill(float *) {}
};

struct SinLut {
    float data[SIN_LUT_SIZE];
    constexpr SinLut() : data{} {
        SinLutFiller<SIN_LUT_SIZE, 0>::fill(data);
    }
};

static constexpr SinLut sin_lut_table{};
static const float *sin_lut = sin_lut_table.data;

void sin_cos(uint16_t angle, float &s, float &c) {
    // 16-bit angle → 8-bit index
    uint8_t idx = static_cast<uint8_t>(angle >> 8);
    // 插值
    uint8_t idx_next = idx + 1;
    float frac = static_cast<float>(angle & 0xFF) / 256.0f;

    float s0 = sin_lut[idx];
    float s1 = sin_lut[idx_next];
    s = s0 + (s1 - s0) * frac;

    // cos = sin(angle + 90°) → index + 64
    uint8_t cos_idx = idx + 64;
    uint8_t cos_idx_next = cos_idx + 1;
    float c0 = sin_lut[cos_idx];
    float c1 = sin_lut[cos_idx_next];
    c = c0 + (c1 - c0) * frac;
}

float fast_atan2(float y, float x) {
    // 快速 atan2 近似
    float abs_x = fabsf(x);
    float abs_y = fabsf(y);
    float min_val = (abs_x < abs_y) ? abs_x : abs_y;
    float max_val = (abs_x > abs_y) ? abs_x : abs_y;

    if (max_val < 1e-10f) return 0.0f;

    float a = min_val / max_val;
    float s = a * a;
    float r = ((-0.0464964749f * s + 0.15931422f) * s - 0.327622764f) * s * a + a;

    if (abs_y > abs_x) r = 1.57079637f - r;
    if (x < 0.0f) r = 3.14159274f - r;
    if (y < 0.0f) r = -r;

    return r;
}

Vec2 clarke_transform(float iu, float iv, float iw) {
    Vec2 result;
    result.a = iu;
    result.b = (iu + 2.0f * iv) * 0.57735027f; // 1/sqrt(3)
    return result;
}

Polar park_transform(const Vec2 &iab, const SinCos &sc) {
    Polar result;
    result.d = iab.a * sc.cos + iab.b * sc.sin;
    result.q = -iab.a * sc.sin + iab.b * sc.cos;
    return result;
}

Vec2 inverse_park(const Polar &vdq, const SinCos &sc) {
    Vec2 result;
    result.a = vdq.d * sc.cos - vdq.q * sc.sin;
    result.b = vdq.d * sc.sin + vdq.q * sc.cos;
    return result;
}

void get_lab(uint16_t angle, float ld, float lq_minus_ld, float &la, float &lb) {
    float s, c;
    sin_cos(angle, s, c);
    float l_avg = ld + lq_minus_ld * 0.5f;
    float l_diff = lq_minus_ld * 0.5f;
    la = l_avg - l_diff * c;
    lb = -l_diff * s;
}

} // namespace foc
