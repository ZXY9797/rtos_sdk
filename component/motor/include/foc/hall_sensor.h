#pragma once

#include <cstdint>

namespace foc {

class HallSensor {
public:
    HallSensor() = default;

    // Hall 状态更新 (3-bit: H1 | H2<<1 | H3<<2)
    void update(uint8_t hall_state);

    // 角度输出 (插值后)
    uint16_t angle() const { return angle_; }

    // 速度输出 (电频率 Hz)
    float speed_ehz() const { return speed_; }

    // Hall 序列方向
    void set_direction(bool forward) { forward_ = forward; }

    // 极对数
    void set_pole_pairs(uint8_t pp) { pole_pairs_ = pp; }

private:
    uint8_t hall_state_ {0};
    uint16_t angle_ {0};
    float speed_ {0.0f};
    bool forward_ {true};
    uint8_t pole_pairs_ {1};

    // 插值计数器
    uint32_t interp_count_ {0};
    uint32_t interp_period_ {0};
    uint16_t prev_angle_ {0};
    uint32_t last_update_tick_ {0};
};

} // namespace foc
