#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace app {
enum class BrakePhase : uint8_t { Locked, Preload, Releasing, Running, Stopping, Engaging };

struct BrakeConfig {
    float hold_a{}, tolerance_a{}, preload_ms{}, release_ms{300}, engage_ms{};
    float travel_rad{}, still_rad_s{}, deceleration{}, kp_a_rad{}, kd_a_s_rad{};
    float collision_nm{}, collision_ms{};

    bool valid(float current_limit) const {
        for (const float value : {hold_a, tolerance_a, preload_ms, release_ms, engage_ms, travel_rad,
                                  still_rad_s, deceleration, kp_a_rad, kd_a_s_rad, collision_nm, collision_ms})
            if (!std::isfinite(value)) return false;
        return std::fabs(hold_a) + tolerance_a <= current_limit &&
            tolerance_a > 0 && preload_ms >= 1 && preload_ms <= 1000 &&
            release_ms >= 1 && release_ms <= 2000 && engage_ms >= 1 && engage_ms <= 2000 &&
            travel_rad > 0 && travel_rad < 3.14F && still_rad_s > 0 && deceleration > 0 &&
            kp_a_rad > 0 && kd_a_s_rad > 0 && collision_nm >= 0 &&
            collision_ms >= 0 && (collision_nm == 0 || collision_ms >= 0.25F);
    }
};

// All durations are measured at the 4 kHz joint sampling point.
class BrakeControl {
public:
    BrakePhase phase() const { return phase_; }
    void lock() { phase_ = BrakePhase::Locked; }
    void start(float position, uint32_t now) {
        position_ = position;
        velocity_ = 0;
        settled_ = false;
        enter(BrakePhase::Preload, now);
    }
    void stop(float position, float velocity, uint32_t now) {
        if (phase_ == BrakePhase::Locked || phase_ == BrakePhase::Engaging ||
            phase_ == BrakePhase::Stopping) return;
        position_ = position;
        velocity_ = velocity;
        settled_ = false;
        enter(phase_ == BrakePhase::Running ? BrakePhase::Stopping : BrakePhase::Engaging, now);
    }
    bool step(const BrakeConfig& c, float support_a, uint32_t now, float dt, float q, float v, float iq,
              float& request) {
        const uint32_t elapsed = now - since_;
        if (phase_ == BrakePhase::Stopping) {
            const float before = velocity_;
            velocity_ = std::copysign(std::max(0.0F, std::fabs(before) - c.deceleration * dt), before);
            position_ += (before + velocity_) * (0.5F * dt);
        }
        const float error = position_ - q;
        request = support_a + c.kp_a_rad * error + c.kd_a_s_rad * (velocity_ - v);
        if (!std::isfinite(request) || !std::isfinite(iq) || std::fabs(error) > c.travel_rad)
            return false;
        const bool steady = std::fabs(v) <= c.still_rad_s &&
                            std::fabs(iq - request) <= c.tolerance_a;
        if (!steady) settled_ = false;
        else if (!settled_) { settled_ = true; steady_since_ = now; }
        const bool held = settled_ && now - steady_since_ >= uint32_t(c.preload_ms * 1000);
        switch (phase_) {
        case BrakePhase::Preload:
            if (held) enter(BrakePhase::Releasing, now);
            else if (elapsed > 2000000U) return false;
            break;
        case BrakePhase::Releasing:
            if (!steady) return false;
            if (elapsed >= uint32_t(c.release_ms * 1000)) enter(BrakePhase::Running, now);
            break;
        case BrakePhase::Stopping:
            if (velocity_ == 0 && held) enter(BrakePhase::Engaging, now);
            else if (elapsed > stop_timeout_us_) return false;
            break;
        case BrakePhase::Engaging:
            if (elapsed >= uint32_t(c.engage_ms * 1000)) lock();
            break;
        default: break;
        }
        return true;
    }
    void set_stop_timeout(float speed, const BrakeConfig& c) {
        stop_timeout_us_ = uint32_t(std::min(600.0F, std::fabs(speed) / c.deceleration + 2.0F) * 1e6F);
    }
private:
    void enter(BrakePhase p, uint32_t now) { phase_ = p; since_ = now; }
    BrakePhase phase_{BrakePhase::Locked};
    float position_{}, velocity_{};
    bool settled_{};
    uint32_t since_{}, steady_since_{}, stop_timeout_us_{2000000U};
};
} // namespace app
