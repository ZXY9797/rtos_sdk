#pragma once

namespace algo {

struct MotionProfileLimits {
    float maximum_speed {0.0F};
    float maximum_acceleration {0.0F};
    float maximum_deceleration {0.0F};
    float maximum_jerk {0.0F};
};

struct MotionProfileState {
    float rate {0.0F};
    float acceleration {0.0F};
};

class JerkLimitedMotionProfile {
public:
    [[nodiscard]] static bool step(
        float position_error, float sample_period_s,
        const MotionProfileLimits &limits,
        const MotionProfileState &state,
        MotionProfileState &next_state);
};

[[nodiscard]] bool valid_motion_profile_limits(
    const MotionProfileLimits &limits);

} // namespace algo
