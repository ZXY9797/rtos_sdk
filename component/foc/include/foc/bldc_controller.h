#pragma once

#include <cstdint>

namespace foc {

class BLDCController {
public:
    BLDCController() = default;

    // 梯形换相控制
    // hall_state: 3-bit Hall 状态
    // duty: 占空比 (0 ~ period)
    // period: PWM 周期
    void commutate(uint8_t hall_state, uint32_t duty, uint32_t period,
                   uint32_t &duty_u, uint32_t &duty_v,
                   uint32_t &duty_w);

    // 使能/禁用
    void set_enabled(bool enable) { enabled_ = enable; }
    bool is_enabled() const { return enabled_; }

    // 方向
    void set_direction(bool forward) { forward_ = forward; }

private:
    bool enabled_ {false};
    bool forward_ {true};
};

} // namespace foc
