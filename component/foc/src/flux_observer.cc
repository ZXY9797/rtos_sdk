#include <foc/flux_observer.h>
#include <foc/math_utils.h>
#include <cmath>

namespace foc {

// ─── MxLemming Observer ───

MxLemmingObserver::MxLemmingObserver() : cfg_{} {}
MxLemmingObserver::MxLemmingObserver(const Config &cfg) : cfg_(cfg) {}

void MxLemmingObserver::set_params(float rs, float ld, float lq) {
    rs_ = rs;
    ld_ = ld;
    lq_ = lq;
}

void MxLemmingObserver::update(const Vec2 &v_ab, const Vec2 &i_ab, float dt) {
    // 磁链估算: ψ = ∫(v - Rs*i)dt - L*i
    float e_a = v_ab.a - rs_ * i_ab.a;
    float e_b = v_ab.b - rs_ * i_ab.b;

    psi_alpha_ += e_a * dt;
    psi_beta_ += e_b * dt;

    // 减去电阻磁链分量
    float psi_s_alpha = psi_alpha_ - ld_ * i_ab.a;
    float psi_s_beta = psi_beta_ - lq_ * i_ab.b;

    // 磁链幅值
    flux_mag_ = sqrtf(psi_s_alpha * psi_s_alpha + psi_s_beta * psi_s_beta);

    // 角度
    angle_ = static_cast<uint16_t>(fast_atan2(psi_s_beta, psi_s_alpha) * 65536.0f / (2.0f * 3.14159265f));

    // BEMF
    e_alpha_ = e_a;
    e_beta_ = e_b;

    // 速度估算
    if (flux_mag_ > 1e-6f) {
        float cross = psi_s_alpha * e_beta_ - psi_s_beta * e_alpha_;
        speed_est_ = cross / (flux_mag_ * flux_mag_);
    }
}

uint16_t MxLemmingObserver::angle() const { return angle_; }
float MxLemmingObserver::flux_magnitude() const { return flux_mag_; }
float MxLemmingObserver::bemf_d() const { return e_alpha_; }
float MxLemmingObserver::bemf_q() const { return e_beta_; }
float MxLemmingObserver::speed_estimate() const { return speed_est_; }

// ─── Ortega Observer ───

OrtegaObserver::OrtegaObserver() : cfg_{} {}
OrtegaObserver::OrtegaObserver(const Config &cfg) : cfg_(cfg) {}

void OrtegaObserver::set_params(float rs, float ld, float lq) {
    rs_ = rs;
    ld_ = ld;
    lq_ = lq;
}

void OrtegaObserver::update(const Vec2 &v_ab, const Vec2 &i_ab, float dt) {
    // 观测器: dψ/dt = v - Rs*i + γ*(ψ_hat - ψ_s)
    float e_a = v_ab.a - rs_ * i_ab.a;
    float e_b = v_ab.b - rs_ * i_ab.b;

    // 估算磁链
    float psi_s_alpha = psi_alpha_ - ld_ * i_ab.a;
    float psi_s_beta = psi_beta_ - lq_ * i_ab.b;

    // 反馈项
    float gamma = cfg_.gain;
    psi_alpha_ += (e_a + gamma * psi_s_alpha) * dt;
    psi_beta_ += (e_b + gamma * psi_s_beta) * dt;

    // 重新计算
    psi_s_alpha = psi_alpha_ - ld_ * i_ab.a;
    psi_s_beta = psi_beta_ - lq_ * i_ab.b;

    flux_mag_ = sqrtf(psi_s_alpha * psi_s_alpha + psi_s_beta * psi_s_beta);
    angle_ = static_cast<uint16_t>(fast_atan2(psi_s_beta, psi_s_alpha) * 65536.0f / (2.0f * 3.14159265f));

    e_alpha_ = e_a;
    e_beta_ = e_b;

    if (flux_mag_ > 1e-6f) {
        float cross = psi_s_alpha * e_beta_ - psi_s_beta * e_alpha_;
        speed_est_ = cross / (flux_mag_ * flux_mag_);
    }
}

uint16_t OrtegaObserver::angle() const { return angle_; }
float OrtegaObserver::flux_magnitude() const { return flux_mag_; }
float OrtegaObserver::bemf_d() const { return e_alpha_; }
float OrtegaObserver::bemf_q() const { return e_beta_; }
float OrtegaObserver::speed_estimate() const { return speed_est_; }

} // namespace foc
