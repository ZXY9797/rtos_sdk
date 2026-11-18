#include <foc/hfi.h>
#include <foc/math_utils.h>
#include <cmath>

namespace foc {

HfiInjector::HfiInjector() : cfg_{} {}
HfiInjector::HfiInjector(const Config &cfg) : cfg_(cfg) {}

Vec2 HfiInjector::inject(float dt) {
    if (!active_) return {0.0f, 0.0f};

    phase_ += cfg_.frequency * dt;
    if (phase_ >= 1.0f) phase_ -= 1.0f;

    float v_inj = cfg_.amplitude * sinf(phase_ * 2.0f * 3.14159265f);

    Vec2 result;
    if (cfg_.dual_axis) {
        result.a = v_inj;
        result.b = cfg_.amplitude * cosf(phase_ * 2.0f * 3.14159265f);
    } else {
        result.a = v_inj;
        result.b = 0.0f;
    }
    return result;
}

void HfiInjector::analyze(const Vec2 &i_ab, uint16_t angle) {
    if (!active_) return;

    // 简化的 HFI 角度误差检测
    // 基于 d/q 轴电流差分
    float s, c;
    sin_cos(angle, s, c);

    float i_d = i_ab.a * c + i_ab.b * s;
    float i_q = -i_ab.a * s + i_ab.b * c;

    // 正半周 → d 轴响应
    if (sinf(phase_ * 2.0f * 3.14159265f) > 0) {
        i_d_pos_ = i_d;
        i_q_pos_ = i_q;
    } else {
        i_d_neg_ = i_d;
        i_q_neg_ = i_q;
    }

    // 角度误差 = (id_pos - id_neg) / (iq_pos - iq_neg)
    float d_diff = i_d_pos_ - i_d_neg_;
    float q_diff = i_q_pos_ - i_q_neg_;
    if (fabsf(q_diff) > 1e-6f) {
        angle_error_ = d_diff / q_diff;
    }
}

} // namespace foc
