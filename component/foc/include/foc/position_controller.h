#pragma once

#include "pid.h"

namespace foc {

class PositionController {
public:
    struct Config {
        float kp {1.0f};
        float max_velocity {1000.0f};  // 最大速度 (RPM)
    };

    PositionController();
    explicit PositionController(const Config &cfg);

    // 位置环更新
    // setpoint: 目标位置 (rad)
    // measurement: 当前位置 (rad)
    // dt: 时间步长
    // 返回: 目标速度 (RPM)
    float update(float setpoint, float measurement, float dt);

    void reset();
    void set_config(const Config &cfg);

private:
    Config cfg_;
    float error_prev_ {0.0f};
};

} // namespace foc
