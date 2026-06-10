#pragma once

#include <algo/leso.h>

class SpeedController {
public:
    struct Config {
        algo::LesoConfig leso;
        float torque_constant {4.074f};
        float max_current {8.0f};
    };

    SpeedController() = default;

    void init(const Config &cfg);
    void enable();
    void disable();
    bool is_enabled() const { return enabled_; }

    float update(float speed_setpoint, float speed_feedback);

    void set_torque_feedforward(float torque_nm) { torque_ff_ = torque_nm; }

    float iq_output() const { return iq_output_; }
    float disturbance() const { return leso_.disturbance(); }
    float torque_ff() const { return torque_ff_; }

private:
    Config cfg_;
    algo::Leso leso_;
    bool enabled_ {false};
    float torque_ff_ {0.0f};
    float iq_output_ {0.0f};
};
