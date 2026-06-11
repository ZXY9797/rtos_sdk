#include "speed_controller.h"
#include <algorithm>
#include <cmath>

void SpeedController::init(const Config &cfg) {
    cfg_ = cfg;
    leso_.init(cfg.leso);
}

void SpeedController::enable() {
    if (!enabled_) {
        leso_.reset();
        enabled_ = true;
    }
}

void SpeedController::disable() {
    enabled_ = false;
    leso_.reset();
    iq_output_ = 0.0f;
    torque_ff_ = 0.0f;
}

float SpeedController::update(float speed_setpoint, float speed_feedback) {
    if (!enabled_) {
        iq_output_ = 0.0f;
        return 0.0f;
    }

    float iq_cmd = leso_.update(speed_setpoint, speed_feedback);

    float iq_ff = (-torque_ff_ / cfg_.torque_constant) + iq_cmd;

    iq_output_ = std::clamp(iq_ff, -cfg_.max_current, cfg_.max_current);

    return iq_output_;
}
