#include <foc/position_controller.h>
#include <algorithm>
#include <cmath>

namespace foc {

PositionController::PositionController() : cfg_{} {}
PositionController::PositionController(const Config &cfg) : cfg_(cfg) {}

float PositionController::update(float setpoint, float measurement, float dt) {
    float error = setpoint - measurement;

    // P 控制器
    float velocity = cfg_.kp * error;

    // 限幅
    velocity = std::clamp(velocity, -cfg_.max_velocity, cfg_.max_velocity);

    return velocity;
}

void PositionController::reset() {
    error_prev_ = 0.0f;
}

void PositionController::set_config(const Config &cfg) {
    cfg_ = cfg;
}

} // namespace foc
