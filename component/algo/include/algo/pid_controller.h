#pragma once

#include <cstdint>

struct PidConfig {
    float kp {1.0f};
    float ki {0.0f};
    float kd {0.0f};
    float ts {0.001f};
    float i_limit {10.0f};
    float output_limit {10.0f};
    float output_min {0.0f};   // 非对称下限 (0 表示未启用，使用 -output_limit)
    float d_filter_alpha {0.0f};
};

class PidController {
public:
    void init(const PidConfig &cfg);

    float update(float error);
    float update(float setpoint, float measurement, float dt);

    void reset();
    void set_gains(float kp, float ki, float kd);
    void set_output_limit(float limit);
    void set_output_range(float min, float max);

    void set_config(const PidConfig &cfg) { cfg_ = cfg; }
    const PidConfig &config() const { return cfg_; }

    float output() const { return output_; }
    float integral() const { return integral_; }

private:
    PidConfig cfg_;
    float integral_ {0.0f};
    float prev_error_ {0.0f};
    float prev_d_ {0.0f};
    float output_ {0.0f};

    float clamp_output(float val) const;
};
