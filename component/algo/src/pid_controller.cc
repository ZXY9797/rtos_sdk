#include "algo/pid_controller.h"
#include <algorithm>
#include <cmath>

void PidController::init(const PidConfig &cfg) {
    cfg_ = cfg;
    reset();
}

float PidController::update(float error) {
    float p_term = cfg_.kp * error;

    integral_ += error * cfg_.ki * cfg_.ts;
    integral_ = std::clamp(integral_, -cfg_.i_limit, cfg_.i_limit);

    float d_raw = (error - prev_error_) / cfg_.ts;
    float d_term;
    if (cfg_.d_filter_alpha > 0.0f) {
        d_term = prev_d_ + cfg_.d_filter_alpha * (d_raw - prev_d_);
    } else {
        d_term = d_raw;
    }
    d_term *= cfg_.kd;
    prev_error_ = error;
    prev_d_ = d_raw;

    output_ = p_term + integral_ + d_term;
    output_ = clamp_output(output_);

    return output_;
}

float PidController::update(float setpoint, float measurement, float dt) {
    float error = setpoint - measurement;

    float p_term = cfg_.kp * error;

    integral_ += error * cfg_.ki * dt;
    integral_ = std::clamp(integral_, -cfg_.i_limit, cfg_.i_limit);

    float derivative = (dt > 0.0f) ? (error - prev_error_) / dt : 0.0f;
    prev_error_ = error;

    output_ = p_term + integral_ + cfg_.kd * derivative;
    output_ = clamp_output(output_);

    return output_;
}

void PidController::reset() {
    integral_ = 0.0f;
    prev_error_ = 0.0f;
    prev_d_ = 0.0f;
    output_ = 0.0f;
}

void PidController::set_gains(float kp, float ki, float kd) {
    cfg_.kp = kp;
    cfg_.ki = ki;
    cfg_.kd = kd;
}

void PidController::set_output_limit(float limit) {
    cfg_.output_limit = limit;
    cfg_.output_min = -limit;
    cfg_.i_limit = limit;
}

void PidController::set_output_range(float min, float max) {
    cfg_.output_min = min;
    cfg_.output_limit = max;
    cfg_.i_limit = std::max(std::abs(min), std::abs(max));
}

float PidController::clamp_output(float val) const {
    float lo = (cfg_.output_min != 0.0f) ? cfg_.output_min : -cfg_.output_limit;
    return std::clamp(val, lo, cfg_.output_limit);
}
