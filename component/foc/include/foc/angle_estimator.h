#pragma once

#include "types.h"
#include <cstdint>

namespace foc {

class AngleEstimator {
public:
    AngleEstimator() = default;

    // 更新角度估算
    // sensor_angle: 传感器角度 (Hall/Encoder), 无传感器时为 0
    // speed: 估算转速 (电频率 Hz)
    // dt: 时间步长
    void update(uint16_t sensor_angle, float speed, float dt);

    // 角度输出
    uint16_t angle() const { return angle_; }
    SinCos sin_cos() const;

    // 校准偏移
    void set_offset(int32_t offset) { offset_ = offset; }
    int32_t offset() const { return offset_; }

    // 角度源切换
    void set_open_loop(bool enable, float speed_hz = 0.0f);
    bool is_open_loop() const { return open_loop_; }

private:
    uint16_t angle_ {0};
    int32_t offset_ {0};
    bool open_loop_ {false};
    float open_loop_speed_ {0.0f};
    float open_loop_accum_ {0.0f};
    float accum_ {0.0f};
};

} // namespace foc
