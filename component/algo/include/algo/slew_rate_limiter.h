#pragma once

#include <cmath>
#include <algorithm>

namespace algo {

class SlewRateLimiter {
public:
    SlewRateLimiter() = default;

    SlewRateLimiter(float rising_rate, float falling_rate)
        : rising_(rising_rate), falling_(falling_rate) {}

    void set_rate(float rate) { rising_ = rate; falling_ = rate; }
    void set_rates(float rising, float falling) { rising_ = rising; falling_ = falling; }

    float update(float target, float dt) {
        if (dt <= 0.0f) return output_;
        float delta = target - output_;
        float max_delta = (delta > 0.0f) ? rising_ * dt : falling_ * dt;
        if (std::abs(delta) > max_delta)
            output_ += (delta > 0.0f ? max_delta : -max_delta);
        else
            output_ = target;
        return output_;
    }

    float output() const { return output_; }
    void reset(float val = 0.0f) { output_ = val; }

private:
    float rising_ {1e30f};
    float falling_ {1e30f};
    float output_ {0.0f};
};

} // namespace algo
