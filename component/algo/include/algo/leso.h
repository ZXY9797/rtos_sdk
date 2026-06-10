#pragma once

#include <cstdint>

namespace algo {

enum class LesoType : uint8_t {
    FirstOrder,
    SecondOrder,
};

struct LesoConfig {
    LesoType type {LesoType::FirstOrder};
    float omega {3000.0f};
    float b0 {6000.0f};
    float ts {0.00025f};
    float kp {0.006f};
    float kd {0.0f};
    float output_min {-8.0f};
    float output_max {8.0f};
};

class Leso {
public:
    Leso() = default;
    explicit Leso(const LesoConfig &cfg);

    void init(const LesoConfig &cfg);
    float update(float setpoint, float feedback);
    void reset();

    float disturbance() const { return z2_; }
    float output() const { return u_prev_; }

    void set_output_limit(float min, float max);

private:
    LesoConfig cfg_;
    float z1_ {0.0f};
    float z2_ {0.0f};
    float z3_ {0.0f};
    float u_prev_ {0.0f};
    float beta1_ {0.0f};
    float beta2_ {0.0f};
    float beta3_ {0.0f};
    float kp_ {0.0f};
    float kd_ {0.0f};

    void calculate_gains();
    float update_first_order(float set, float fb);
    float update_second_order(float set, float fb);
};

} // namespace algo
