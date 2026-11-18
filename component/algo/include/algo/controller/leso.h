#pragma once

#include <stdint.h>

namespace algo::motor::controller {

enum class LesoType : uint8_t {
    FirstOrder,
    SecondOrder,
};

struct LesoConfig {
    LesoType type = LesoType::FirstOrder;
    float omega = 0.0F;
    float sample_time_s = 0.0F;
    float b0 = 0.0F;
    float kp = 0.0F;
    float kd = 0.0F;
    float output_min = 0.0F;
    float output_max = 0.0F;
};

class Leso {
public:
    bool configure(const LesoConfig& config);
    void reset();
    void seed(float feedback, float output);
    float update(float setpoint, float feedback);
    // Call after downstream limiting; preserves observed motion and tracks actual effort.
    bool track_output(float applied_output);

    float z1() const { return z1_; }
    float z2() const { return z2_; }
    float z3() const { return z3_; }
    float output() const { return output_; }
    bool configured() const { return configured_; }

private:
    float update_first_order(float setpoint, float feedback);
    float update_second_order(float setpoint, float feedback);

    LesoType type_ = LesoType::FirstOrder;
    float omega_ = 0.0F;
    float b0_ = 0.0F;
    float ts_ = 0.0F;
    float beta1_ = 0.0F;
    float beta2_ = 0.0F;
    float beta3_ = 0.0F;
    float kp_ = 0.0F;
    float kd_ = 0.0F;
    float z1_ = 0.0F;
    float z2_ = 0.0F;
    float z3_ = 0.0F;
    float last_output_ = 0.0F;
    float output_ = 0.0F;
    float output_min_ = 0.0F;
    float output_max_ = 0.0F;
    bool configured_ = false;
};

}  // namespace algo::motor::controller
