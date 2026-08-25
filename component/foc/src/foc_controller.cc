#include <foc/foc_controller.h>
#include <foc/math_utils.h>
#include <foc/config.h>
#include <algorithm>
#include <cmath>

static constexpr float TWO_PI = 2.0f * 3.14159265f;

namespace foc {

FOCController::FOCController(const FOCConfig &cfg)
    : cfg_(cfg) {
    float v_lim = cfg_.current_bandwidth * 10.0f;
    id_pid_.set_output_range(-v_lim, v_lim);
    iq_pid_.set_output_range(-v_lim, v_lim);
    speed_pid_.set_gains(0.5f, 0.01f, 0.0f);
    speed_pid_.set_output_range(-100.0f, 100.0f);
}

void FOCController::calculate_gains(float rs, float ld, float lq, float flux, uint8_t pp) {
    float bw = cfg_.current_bandwidth * 2.0f * 3.14159265f;
    float v_lim = cfg_.current_bandwidth * 10.0f;

    // D轴 PI: Kp = Ld * bw, Ki = Rs * bw
    id_pid_.set_gains(ld * bw, rs * bw, 0.0f);
    id_pid_.set_output_range(-v_lim, v_lim);

    // Q轴 PI: Kp = Lq * bw, Ki = Rs * bw
    iq_pid_.set_gains(lq * bw, rs * bw, 0.0f);
    iq_pid_.set_output_range(-v_lim, v_lim);

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
                                    uint32_t pwm_period,
                                    uint32_t &duty_u, uint32_t &duty_v, uint32_t &duty_w) {
    vbus_ = vbus;
    calculate_voltage_gain(vbus);

    // Clarke 变换
    iab_ = clarke_transform(i_abc.u, i_abc.v, i_abc.w);

    // Park 变换
    idq_ = park_transform(iab_, sc);
    id_snapshot_.store(idq_.d, std::memory_order_relaxed);
    iq_snapshot_.store(idq_.q, std::memory_order_relaxed);
    vbus_snapshot_.store(vbus, std::memory_order_relaxed);

    // 死区补偿: 根据电流方向叠加补偿电压
    float dt_comp_d = 0.0f, dt_comp_q = 0.0f;
    if (dt_comp_.enabled && vbus_ > 0.0f) {
        float t_pwm_ns = 1.0f / cfg_.pwm_frequency * 1e9f;
        float v_dead = vbus_ * dt_comp_.dead_time_ns / t_pwm_ns;
        dt_comp_d = (idq_.d > 0.0f) ? v_dead : ((idq_.d < 0.0f) ? -v_dead : 0.0f);
        dt_comp_q = (idq_.q > 0.0f) ? v_dead : ((idq_.q < 0.0f) ? -v_dead : 0.0f);
    }

    // 电流环 PI
    float dt = 1.0f / cfg_.pwm_frequency;
    const float id_ref = id_ref_.load(std::memory_order_relaxed);
    const float iq_ref = iq_ref_.load(std::memory_order_relaxed);
    float vd = id_pid_.update(id_ref, idq_.d, dt);
    float vq = iq_pid_.update(iq_ref, idq_.q, dt);

    // 应用死区补偿
    vd += dt_comp_d;
    vq += dt_comp_q;

    // 前馈解耦: vd -= ωLqIq, vq += ωLdId
    float we = e_hz_.load(std::memory_order_relaxed) * TWO_PI;
    vd -= we * lq_ * idq_.q;
    vq += we * ld_ * idq_.d;

    // 谐波抑制: 在 q 轴注入反相谐波电压
    if (harm_comp_.enabled && harm_comp_.amplitude != 0.0f) {
        float theta_e = static_cast<float>(angle) * TWO_PI / 65536.0f;
        float harm_v = harm_comp_.amplitude *
            sinf(static_cast<float>(harm_comp_.harmonic_order) * theta_e + harm_comp_.phase_offset);
        vq += harm_v;
    }

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
    vd_snapshot_.store(vdq_.d, std::memory_order_relaxed);
    vq_snapshot_.store(vdq_.q, std::memory_order_relaxed);

    // 逆 Park 变换
    vab_ = inverse_park(vdq_, sc);

    // SVPWM
    svpwm_.generate(vab_, vbus_, pwm_period, duty_u, duty_v, duty_w);
}

float FOCController::update_speed(float speed_rpm, float dt) {
    mech_rpm_.store(speed_rpm, std::memory_order_relaxed);

    // 速度环 PI
    const float speed_ref = speed_ref_.load(std::memory_order_relaxed);
    float iq_out = speed_pid_.update(speed_ref, speed_rpm, dt);

    // 弱磁
    if (cfg_.field_weakening > 0) {
        const float vd = vd_snapshot_.load(std::memory_order_relaxed);
        const float vq = vq_snapshot_.load(std::memory_order_relaxed);
        const float vbus = vbus_snapshot_.load(std::memory_order_relaxed);
        float v_mag = sqrtf(vd * vd + vq * vq);
        float v_max = vbus * modulation_max_ * 0.9f;
        if (v_mag > v_max) {
            fw_current_ += 0.01f * (v_mag - v_max);
        } else {
            fw_current_ -= 0.01f;
        }
        fw_current_ = std::clamp(fw_current_, -cfg_.current_bandwidth * 0.1f, 0.0f);
        id_ref_.store(fw_current_ + id_mtpa_, std::memory_order_relaxed);
    } else {
        id_ref_.store(id_mtpa_, std::memory_order_relaxed);
    }

    const float iq_ref = std::clamp(
        iq_out, -cfg_.current_bandwidth, cfg_.current_bandwidth);
    iq_ref_.store(iq_ref, std::memory_order_relaxed);
    return iq_ref;
}

void FOCController::run_mtpa(float ld, float lq_minus_ld) {
    if (!cfg_.use_mtpa || lq_minus_ld == 0.0f) {
        id_mtpa_ = 0.0f;
        return;
    }
    // MTPA: id = (flux - sqrt(flux^2 + 8*(Ld-Lq)^2*iq^2)) / (4*(Ld-Lq))
    const float iq_ref = iq_ref_.load(std::memory_order_relaxed);
    float iq_sq = iq_ref * iq_ref;
    float l_diff = lq_minus_ld;
    float disc = 4.0f * l_diff * l_diff * iq_sq;  // simplified
    id_mtpa_ = -disc / (4.0f * l_diff);
    id_mtpa_ = std::clamp(id_mtpa_, -cfg_.current_bandwidth * 0.5f, 0.0f);
}

void FOCController::pll_update(uint16_t angle, float dt) {
    const uint32_t pll_angle = pll_angle_.load(std::memory_order_relaxed);
    int32_t error = static_cast<int32_t>(angle)
        - static_cast<int32_t>(pll_angle & 0xFFFFU);
    // 归一化到 [-32768, 32767]
    if (error > 32767) error -= 65536;
    if (error < -32768) error += 65536;

    update_pll(error, dt);
}

void FOCController::update_pll(int32_t angle_error, float dt) {
    pll_error_ = static_cast<float>(angle_error);
    pll_integrator_ += pll_ki_ * pll_error_ * dt;
    float speed = pll_kp_ * pll_error_ + pll_integrator_;

    const uint32_t pll_angle = pll_angle_.load(std::memory_order_relaxed);
    const uint32_t next_angle = static_cast<uint32_t>(
        static_cast<int32_t>(pll_angle) + static_cast<int32_t>(speed * dt));
    pll_angle_.store(next_angle & 0xFFFFU, std::memory_order_relaxed);
}

} // namespace foc
