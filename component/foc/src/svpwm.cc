#include <foc/svpwm.h>
#include <algorithm>
#include <cmath>

namespace foc {

void Svpwm::generate(const Vec2 &v_ab, float vbus, uint32_t period,
                     uint32_t &duty_u, uint32_t &duty_v, uint32_t &duty_w) {
    if (vbus <= 0.0f) {
        duty_u = duty_v = duty_w = 0;
        return;
    }

    float v_max = vbus * modulation_max_;
    float va = v_ab.a;
    float vb = v_ab.b;

    // 限幅
    float v_mag = sqrtf(va * va + vb * vb);
    if (v_mag > v_max && v_mag > 0.0f) {
        float scale = v_max / v_mag;
        va *= scale;
        vb *= scale;
    }

    // 逆 Clarke 变换 → 三相电压
    float vu = va;
    float vv = -0.5f * va + 0.8660254f * vb;   // -Va/2 + sqrt(3)/2 * Vb
    float vw = -0.5f * va - 0.8660254f * vb;   // -Va/2 - sqrt(3)/2 * Vb

    // SVM: 中心对齐，加入零序分量
    float v_min = std::min({vu, vv, vw});
    float v_max_ph = std::max({vu, vv, vw});
    float v_zero = -(v_min + v_max_ph) * 0.5f;

    vu += v_zero;
    vv += v_zero;
    vw += v_zero;

    // 归一化到 [0, period]
    float inv_vbus = static_cast<float>(period) / vbus;
    duty_u = static_cast<uint32_t>(std::clamp(vu * inv_vbus, 0.0f, static_cast<float>(period)));
    duty_v = static_cast<uint32_t>(std::clamp(vv * inv_vbus, 0.0f, static_cast<float>(period)));
    duty_w = static_cast<uint32_t>(std::clamp(vw * inv_vbus, 0.0f, static_cast<float>(period)));
}

} // namespace foc
