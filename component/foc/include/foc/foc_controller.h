#pragma once

#include "types.h"
#include "pid.h"
#include "svpwm.h"
#include <cstdint>

namespace foc {

struct FOCConfig {
    float current_bandwidth {1000.0f};   // 电流环带宽 Hz
    float pwm_frequency {20000.0f};      // PWM 频率 Hz
    ObserverType observer {ObserverType::MxLemming};
    bool use_mtpa {false};               // 最大转矩/电流比
    uint8_t field_weakening {0};         // 弱磁等级 0=关
};

class FOCController {
public:
    explicit FOCController(const FOCConfig &cfg = {});

    // 电流环 — ISR 中调用
    void update_current(const Vec3 &i_abc, float vbus, uint16_t angle,
                        const SinCos &sc,
                        uint32_t &duty_u, uint32_t &duty_v, uint32_t &duty_w);

    // 速度环 — 慢循环中调用
    float update_speed(float speed_rpm, float dt);

    // 参考值设置
    void set_iq_ref(float iq) { iq_ref_ = iq; }
    void set_id_ref(float id) { id_ref_ = id; }
    void set_speed_ref(float rpm) { speed_ref_ = rpm; }

    // 参数计算
    void calculate_gains(float rs, float ld, float lq, float flux, uint8_t pp);
    void calculate_voltage_gain(float vbus);
    void run_mtpa(float ld, float lq_minus_ld);

    // 状态读取
    float id() const { return idq_.d; }
    float iq() const { return idq_.q; }
    float vd() const { return vdq_.d; }
    float vq() const { return vdq_.q; }
    float speed_rpm() const { return mech_rpm_; }
    float e_hz() const { return e_hz_; }
    float id_ref() const { return id_ref_; }
    float iq_ref() const { return iq_ref_; }

    // PLL 角度追踪
    void pll_update(uint16_t angle, float dt);
    uint16_t pll_angle() const { return pll_angle_; }

private:
    FOCConfig cfg_;
    Pid id_pid_;
    Pid iq_pid_;
    Pid speed_pid_;
    Svpwm svpwm_;

    // 电流/电压状态
    Polar idq_ {};
    Polar vdq_ {};
    Polar idq_req_ {};
    Vec2 iab_ {};
    Vec2 vab_ {};

    // 电压相关
    float vbus_ {0.0f};
    float modulation_max_ {0.95f};
    float vab_to_pwm_ {0.0f};

    // 速度相关
    float e_hz_ {0.0f};
    float mech_rpm_ {0.0f};
    float speed_ref_ {0.0f};
    float id_ref_ {0.0f};
    float iq_ref_ {0.0f};

    // 弱磁
    float fw_current_ {0.0f};

    // MTPA
    float id_mtpa_ {0.0f};

    // 电机电感参数 (用于前馈解耦)
    float ld_ {0.0f};
    float lq_ {0.0f};

    // PLL
    float pll_kp_ {0.0f};
    float pll_ki_ {0.0f};
    float pll_error_ {0.0f};
    float pll_integrator_ {0.0f};
    uint32_t pll_angle_ {0};

    void update_pll(int32_t angle_error, float dt);
};

} // namespace foc
