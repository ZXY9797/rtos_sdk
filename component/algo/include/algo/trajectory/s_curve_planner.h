#pragma once

namespace algo::motor::trajectory {

// Cubic interpolation with zero slope at both ends; progress is clamped to [0, 1].
float smoothstep(float progress);

struct SCurvePlannerConfig {
    float default_max_velocity_rad_s = 0.0F;
    float max_velocity_limit_rad_s = 0.0F;
    float max_acceleration_rad_s2 = 0.0F;
    float max_jerk_rad_s3 = 0.0F;
    float sample_time_s = 0.0F;
    float position_epsilon_rad = 1.0e-4F;
    float velocity_epsilon_rad_s = 1.0e-3F;
};

struct SCurveSample {
    float position_rad = 0.0F;
    float velocity_rad_s = 0.0F;
    float acceleration_rad_s2 = 0.0F;
    float elapsed_s = 0.0F;
    bool active = false;
    bool finished = true;
};

class SCurvePlanner {
public:
    bool configure(const SCurvePlannerConfig& config);
    void reset(float position_rad = 0.0F);
    bool plan(float target_position_rad, float max_velocity_rad_s);
    SCurveSample update();

    const SCurveSample& sample() const { return sample_; }
    float target_position_rad() const { return target_position_rad_; }
    float profile_duration_s() const { return profile_duration_s_; }
    bool configured() const { return configured_; }
    bool active() const { return active_; }
    bool finished() const { return !active_; }

private:
    struct Segment {
        float jerk_rad_s3 = 0.0F;
        float duration_s = 0.0F;
    };

    float resolve_max_velocity(float max_velocity_rad_s) const;
    bool build_profile(float distance_abs_rad, float max_velocity_rad_s);
    bool build_continuous_profile(float remaining_distance_abs_rad,
                                  float direction,
                                  float max_velocity_rad_s,
                                  const SCurveSample& current_sample);
    float find_replan_elapsed(float velocity_rad_s,
                              float acceleration_rad_s2) const;
    SCurveSample sample_profile_local(float elapsed_s) const;
    SCurveSample sample_profile(float elapsed_s) const;
    void finish_at_target();

    SCurvePlannerConfig config_ {};
    Segment segments_[7] {};
    SCurveSample sample_ {};
    float start_position_rad_ = 0.0F;
    float target_position_rad_ = 0.0F;
    float direction_ = 1.0F;
    float profile_duration_s_ = 0.0F;
    float elapsed_s_ = 0.0F;
    bool configured_ = false;
    bool active_ = false;
};

}  // namespace algo::motor::trajectory
