#include <foc/foc_controller.h>
#include <foc/math_utils.h>
#include <foc/config.h>
#include <algorithm>
#include <cmath>

namespace foc {

FOCController::FOCController(const FOCConfig &cfg)
    : cfg_(cfg)
    , id_pid_(PidConfig{0.0f, 0.0f, 0.0f, cfg_.current_bandwidth * 10.0f, -cfg_.current_bandwidth * 10.0f})
    , iq_pid_(PidConfig{0.0f, 0.0f, 0.0f, cfg_.current_bandwidth * 10.0f, -cfg_.current_bandwidth * 10.0f})
    , speed_pid_(PidConfig{0.5f, 0.01f, 0.0f, 100.0f, -100.0f}) {
}

void FOCController::calculate_gains(float rs, float ld, float lq, float flux, uint8_t pp) {
    float bw = cfg_.current_bandwidth * 2.0f * 3.14159265f;

    // D轴 PI: Kp = Ld * bw, Ki = Rs * bw
    PidConfig d_cfg;
    d_cfg.kp = ld * bw;
    d_cfg.ki = rs * bw;
    d_cfg.output_max = cfg_.current_bandwidth * 10.0f;
    d_cfg.output_min = -d_cfg.output_max;
    id_pid_.set_config(d_cfg);

    // Q轴 PI: Kp = Lq * bw, Ki = Rs * bw
    PidConfig q_cfg;
    q_cfg.kp = lq * bw;
    q_cfg.ki = rs * bw;
    q_cfg.output_max = cfg_.current_bandwidth * 10.0f;
    q_cfg.output_min = -q_cfg.output_max;
    iq_pid_.set_config(q_cfg);

    // 保存电感参数用于前馈解耦
    ld_ = ld;
    lq_ = lq;
}

void FOCController::calculate_voltage_gain(float vbus) {
    vbus_ = vbus;
    if (vbus > 0.0f) {
        vab_to_pwm_ = 1.0f / (vbus * 0.5f);
    }
}

void FOCController::update_current(const Vec3 &i_abc, float vbus, uint16_t angle,
                                    const SinCos &sc,
                                    uint32_t &duty_u, uint32_t &duty_v, uint32_t &duty_w) {
    vbus_ = vbus;
    calculate_voltage_gain(vbus);

    // Clarke 变换
    iab_ = clarke_transform(i_abc.u, i_abc.v, i_abc.w);

    // Park 变换
    idq_ = park_transform(iab_, sc);

    // 电流环 PI
    float dt = 1.0f / cfg_.pwm_frequency;
    float vd = id_pid_.update(id_ref_, idq_.d, dt);
    float vq = iq_pid_.update(iq_ref_, idq_.q, dt);

    // 前馈解耦: vd -= ωLqIq, vq += ωLdId
    float we = e_hz_ * 2.0f * 3.14159265f;
    vd -= we * lq_ * idq_.q;
    vq += we * ld_ * idq_.d;

    vdq_.d = vd;
    vdq_.q = vq;

    // 电压限幅
    float v_mag = sqrtf(vd * vd + vq * vq);
    float v_max = vbus_ * modulation_max_;
    if (v_mag > v_max && v_mag > 0.0f) {
        float scale = v_max / v_mag;
        vdq_.d *= scale;
        vdq_.q *= scale;
    }

    // 逆 Park 变换
    vab_ = inverse_park(vdq_, sc);

    // SVPWM
    uint32_t period = static_cast<uint32_t>(cfg_.pwm_frequency);
    svpwm_.generate(vab_, vbus_, period, duty_u, duty_v, duty_w);
}

float FOCController::update_speed(float speed_rpm, float dt) {
    mech_rpm_ = speed_rpm;

    // 速度环 PI
    float iq_out = speed_pid_.update(speed_ref_, speed_rpm, dt);

    // 弱磁
    if (cfg_.field_weakening > 0) {
        float v_mag = sqrtf(vdq_.d * vdq_.d + vdq_.q * vdq_.q);
        float v_max = vbus_ * modulation_max_ * 0.9f;
        if (v_mag > v_max) {
            fw_current_ += 0.01f * (v_mag - v_max);
        } else {
            fw_current_ -= 0.01f;
        }
        fw_current_ = std::clamp(fw_current_, -cfg_.current_bandwidth * 0.1f, 0.0f);
        id_ref_ = fw_current_ + id_mtpa_;
    } else {
        id_ref_ = id_mtpa_;
    }

    iq_ref_ = std::clamp(iq_out, -cfg_.current_bandwidth, cfg_.current_bandwidth);
    return iq_ref_;
}

void FOCController::run_mtpa(float ld, float lq_minus_ld) {
    if (!cfg_.use_mtpa || lq_minus_ld == 0.0f) {
        id_mtpa_ = 0.0f;
        return;
    }
    // MTPA: id = (flux - sqrt(flux^2 + 8*(Ld-Lq)^2*iq^2)) / (4*(Ld-Lq))
    float iq_sq = iq_ref_ * iq_ref_;
    float l_diff = lq_minus_ld;
    float disc = 4.0f * l_diff * l_diff * iq_sq;  // simplified
    id_mtpa_ = -disc / (4.0f * l_diff);
    id_mtpa_ = std::clamp(id_mtpa_, -cfg_.current_bandwidth * 0.5f, 0.0f);
}

void FOCController::pll_update(uint16_t angle, float dt) {
    int32_t error = static_cast<int32_t>(angle) - static_cast<int32_t>(pll_angle_ & 0xFFFF);
    // 归一化到 [-32768, 32767]
    if (error > 32767) error -= 65536;
    if (error < -32768) error += 65536;

    update_pll(error, dt);
}

void FOCController::update_pll(int32_t angle_error, float dt) {
    pll_error_ = static_cast<float>(angle_error);
    pll_integrator_ += pll_ki_ * pll_error_ * dt;
    float speed = pll_kp_ * pll_error_ + pll_integrator_;

    pll_angle_ = static_cast<uint32_t>(static_cast<int32_t>(pll_angle_) + static_cast<int32_t>(speed * dt));
    pll_angle_ &= 0xFFFF;
}

} // namespace foc
