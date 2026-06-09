#pragma once

#include <algorithm>
#include <cmath>

namespace foc {

struct PidConfig {
    float kp {0.0f};
    float ki {0.0f};
    float kd {0.0f};
    float output_max {1e30f};
    float output_min {-1e30f};
    float integral_max {1e30f};
};

class Pid {
public:
    explicit Pid(const PidConfig &cfg = {}) : cfg_(cfg) {}

    float update(float setpoint, float measurement, float dt) {
        float error = setpoint - measurement;
        integral_ += error * dt;
        integral_ = std::clamp(integral_, -cfg_.integral_max, cfg_.integral_max);
        float derivative = (dt > 0.0f) ? (error - prev_error_) / dt : 0.0f;
        prev_error_ = error;
        float output = cfg_.kp * error + cfg_.ki * integral_ + cfg_.kd * derivative;
        return std::clamp(output, cfg_.output_min, cfg_.output_max);
    }

    void reset() {
        integral_ = 0.0f;
        prev_error_ = 0.0f;
    }

    void set_config(const PidConfig &cfg) { cfg_ = cfg; }
    const PidConfig &config() const { return cfg_; }

private:
    PidConfig cfg_;
    float integral_ {0.0f};
    float prev_error_ {0.0f};
};

} // namespace foc
