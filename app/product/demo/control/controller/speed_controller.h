#pragma once

#include <algo/leso.h>
#include <atomic>

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
    bool is_enabled() const { return enabled_.load(std::memory_order_acquire); }

    float update(float speed_setpoint, float speed_feedback);

    void set_torque_feedforward(float torque_nm) {
        torque_ff_.store(torque_nm, std::memory_order_release);
    }

    float iq_output() const { return iq_output_.load(std::memory_order_acquire); }
    float disturbance() const {
        return disturbance_.load(std::memory_order_acquire);
    }
    float torque_ff() const { return torque_ff_.load(std::memory_order_acquire); }

private:
    Config cfg_;
    algo::Leso leso_;
    std::atomic<bool> enabled_ {false};
    std::atomic<bool> reset_pending_ {true};
    std::atomic<float> torque_ff_ {0.0f};
    std::atomic<float> iq_output_ {0.0f};
    std::atomic<float> disturbance_ {0.0f};
};
