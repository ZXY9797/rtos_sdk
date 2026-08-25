#include "speed_controller.h"
#include <algorithm>
#include <cmath>

void SpeedController::init(const Config &cfg) {
    cfg_ = cfg;
    leso_.init(cfg.leso);
}

void SpeedController::enable() {
    reset_pending_.store(true, std::memory_order_release);
    enabled_.store(true, std::memory_order_release);
}

void SpeedController::disable() {
    enabled_.store(false, std::memory_order_release);
    reset_pending_.store(true, std::memory_order_release);
    iq_output_.store(0.0f, std::memory_order_release);
    disturbance_.store(0.0f, std::memory_order_release);
    torque_ff_.store(0.0f, std::memory_order_release);
}

float SpeedController::update(float speed_setpoint, float speed_feedback) {
    if (reset_pending_.exchange(false, std::memory_order_acq_rel)) {
        leso_.reset();
    }
    if (!enabled_.load(std::memory_order_acquire)) {
        iq_output_.store(0.0f, std::memory_order_release);
        disturbance_.store(0.0f, std::memory_order_release);
        return 0.0f;
    }

    float iq_cmd = leso_.update(speed_setpoint, speed_feedback);

    const float iq_ff =
        (-torque_ff_.load(std::memory_order_acquire) / cfg_.torque_constant)
        + iq_cmd;

    const float output = std::clamp(iq_ff, -cfg_.max_current, cfg_.max_current);
    iq_output_.store(output, std::memory_order_release);
    disturbance_.store(leso_.disturbance(), std::memory_order_release);

    return output;
}
