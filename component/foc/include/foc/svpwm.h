#pragma once

#include "types.h"

namespace foc {

class Svpwm {
public:
    Svpwm() = default;

    // 生成 SVPWM 占空比
    // 输入: v_ab (αβ 电压), vbus (母线电压)
    // 输出: duty_u, duty_v, duty_w (0 ~ period)
    void generate(const Vec2 &v_ab, float vbus, uint32_t period,
                  uint32_t &duty_u, uint32_t &duty_v, uint32_t &duty_w);

    // 过调制处理
    void set_modulation_limit(float limit) { modulation_max_ = limit; }
    float modulation_limit() const { return modulation_max_; }

private:
    float modulation_max_ {0.95f};
};

} // namespace foc
