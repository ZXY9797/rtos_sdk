#pragma once

#include <gimbal/types.h>

namespace gimbal {

struct MotionLimits {
    Vector3 maximum_speed_rad_s {2.0F, 2.0F, 2.0F};
    Vector3 maximum_acceleration_rad_s2 {8.0F, 8.0F, 8.0F};
    Vector3 maximum_deceleration_rad_s2 {10.0F, 10.0F, 10.0F};
    Vector3 maximum_jerk_rad_s3 {80.0F, 80.0F, 80.0F};
    float capture_angle_rad {0.002F};
    float capture_rate_rad_s {0.01F};
};

class MotionPlanner {
public:
    [[nodiscard]] bool configure(const MotionLimits &limits);
    void reset(const Quaternion &attitude = {});
    [[nodiscard]] bool set_target(const Quaternion &target);
    [[nodiscard]] MotionReference update(float sample_period_s,
                                         uint32_t publish_time_us);
    [[nodiscard]] bool is_settled() const { return is_settled_; }

private:
    struct AxisLimits {
        float maximum_speed;
        float maximum_acceleration;
        float maximum_deceleration;
        float maximum_jerk;
    };

    struct AxisState {
        float rate;
        float acceleration;
    };

    [[nodiscard]] static AxisState plan_axis(
        float error, float sample_period_s,
        const AxisLimits &limits, const AxisState &state);

    MotionLimits limits_ {};
    Quaternion reference_ {};
    Quaternion target_ {};
    Vector3 rate_ {};
    Vector3 acceleration_ {};
    uint32_t sequence_ {0U};
    bool is_configured_ {false};
    bool is_settled_ {true};
};

[[nodiscard]] bool valid_motion_limits(const MotionLimits &limits);

} // namespace gimbal
