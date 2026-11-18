#include <algo/trajectory/s_curve_planner.h>

#include <algo/math/basic_math.h>

#include <math.h>

namespace algo::motor::trajectory {
namespace {

constexpr float kMinPositive = 1.0e-6F;
constexpr float kOneSixth = 1.0F / 6.0F;
constexpr float kLargeCost = 1.0e30F;
constexpr int kReplanSolveIterations = 4;
constexpr int kReplanSearchSamples = 96;
bool valid_positive(float value)
{
    return basic_math::is_finite_float(value) && (value > 0.0F);
}

float accel_distance(float acceleration, float jerk_time, float accel_time)
{
    return (acceleration * jerk_time * jerk_time) +
           (1.5F * acceleration * jerk_time * accel_time) +
           (0.5F * acceleration * accel_time * accel_time);
}

void integrate_segment(float jerk,
                       float dt,
                       float* position,
                       float* velocity,
                       float* acceleration)
{
    const float a0 = *acceleration;
    const float v0 = *velocity;
    *position += (v0 * dt) + (0.5F * a0 * dt * dt) +
                 (kOneSixth * jerk * dt * dt * dt);
    *velocity += (a0 * dt) + (0.5F * jerk * dt * dt);
    *acceleration += jerk * dt;
}

}  // namespace

float smoothstep(float progress)
{
    const float x = basic_math::clamp_float(progress, 0.0F, 1.0F);
    return x * x * (3.0F - (2.0F * x));
}

bool SCurvePlanner::configure(const SCurvePlannerConfig& config)
{
    if (!valid_positive(config.default_max_velocity_rad_s) ||
        !valid_positive(config.max_acceleration_rad_s2) ||
        !valid_positive(config.max_jerk_rad_s3) ||
        !valid_positive(config.sample_time_s) ||
        !valid_positive(config.position_epsilon_rad) ||
        !valid_positive(config.velocity_epsilon_rad_s) ||
        !basic_math::is_finite_float(config.max_velocity_limit_rad_s)) {
        configured_ = false;
        return false;
    }

    config_ = config;
    if (config_.max_velocity_limit_rad_s <= 0.0F) {
        config_.max_velocity_limit_rad_s = config_.default_max_velocity_rad_s;
    }
    if (config_.default_max_velocity_rad_s > config_.max_velocity_limit_rad_s) {
        config_.default_max_velocity_rad_s = config_.max_velocity_limit_rad_s;
    }

    configured_ = true;
    reset();
    return true;
}

void SCurvePlanner::reset(float position_rad)
{
    for (Segment& segment : segments_) {
        segment = {};
    }
    start_position_rad_ = position_rad;
    target_position_rad_ = position_rad;
    direction_ = 1.0F;
    profile_duration_s_ = 0.0F;
    elapsed_s_ = 0.0F;
    active_ = false;
    sample_ = {
        position_rad,
        0.0F,
        0.0F,
        0.0F,
        false,
        true,
    };
}

float SCurvePlanner::resolve_max_velocity(float max_velocity_rad_s) const
{
    float resolved = fabsf(max_velocity_rad_s);
    if (!basic_math::is_finite_float(resolved) || (resolved <= config_.velocity_epsilon_rad_s)) {
        resolved = config_.default_max_velocity_rad_s;
    }
    return basic_math::clamp_float(resolved,
                                   config_.velocity_epsilon_rad_s,
                                   config_.max_velocity_limit_rad_s);
}

bool SCurvePlanner::plan(float target_position_rad, float max_velocity_rad_s)
{
    if (!configured_ ||
        !basic_math::is_finite_float(target_position_rad) ||
        !basic_math::is_finite_float(max_velocity_rad_s)) {
        return false;
    }

    const SCurveSample current_sample = sample_;
    const float distance = target_position_rad - current_sample.position_rad;
    const float distance_abs = fabsf(distance);
    target_position_rad_ = target_position_rad;
    if (distance_abs <= config_.position_epsilon_rad) {
        profile_duration_s_ = 0.0F;
        start_position_rad_ = target_position_rad;
        finish_at_target();
        return true;
    }

    const float plan_direction = (distance >= 0.0F) ? 1.0F : -1.0F;
    const float velocity_abs = resolve_max_velocity(max_velocity_rad_s);
    const bool can_keep_motion =
        active_ &&
        basic_math::is_finite_float(current_sample.velocity_rad_s) &&
        basic_math::is_finite_float(current_sample.acceleration_rad_s2) &&
        ((current_sample.velocity_rad_s * plan_direction) >
         config_.velocity_epsilon_rad_s);
    if (can_keep_motion &&
        build_continuous_profile(distance_abs,
                                 plan_direction,
                                 velocity_abs,
                                 current_sample)) {
        return true;
    }

    start_position_rad_ = current_sample.position_rad;
    direction_ = plan_direction;
    if (!build_profile(distance_abs, velocity_abs)) {
        return false;
    }

    elapsed_s_ = 0.0F;
    sample_ = sample_profile(0.0F);
    sample_.active = true;
    sample_.finished = false;
    active_ = true;
    return true;
}

bool SCurvePlanner::build_profile(float distance_abs_rad,
                                          float max_velocity_rad_s)
{
    if (!valid_positive(distance_abs_rad) || !valid_positive(max_velocity_rad_s)) {
        return false;
    }

    for (Segment& segment : segments_) {
        segment = {};
    }

    const float a_max = config_.max_acceleration_rad_s2;
    const float j_max = config_.max_jerk_rad_s3;
    float jerk_time = a_max / j_max;
    float accel_time = 0.0F;
    float cruise_time = 0.0F;
    float acceleration = a_max;
    float peak_velocity = max_velocity_rad_s;

    if (peak_velocity < (a_max * jerk_time)) {
        jerk_time = sqrtf(peak_velocity / j_max);
        acceleration = j_max * jerk_time;
        accel_time = 0.0F;
    } else {
        accel_time = (peak_velocity / a_max) - jerk_time;
    }

    float accel_dist = accel_distance(acceleration, jerk_time, accel_time);
    if ((2.0F * accel_dist) > distance_abs_rad) {
        const float full_accel_jerk_time = a_max / j_max;
        const float min_full_accel_distance =
            2.0F * a_max * full_accel_jerk_time * full_accel_jerk_time;
        if (distance_abs_rad >= min_full_accel_distance) {
            jerk_time = full_accel_jerk_time;
            acceleration = a_max;
            const float discriminant =
                (jerk_time * jerk_time) + (4.0F * distance_abs_rad / acceleration);
            accel_time = (-3.0F * jerk_time + sqrtf(discriminant)) * 0.5F;
            if (accel_time < 0.0F) {
                accel_time = 0.0F;
            }
            peak_velocity = acceleration * (jerk_time + accel_time);
        } else {
            jerk_time = cbrtf(distance_abs_rad / (2.0F * j_max));
            acceleration = j_max * jerk_time;
            accel_time = 0.0F;
            peak_velocity = acceleration * jerk_time;
        }
        accel_dist = accel_distance(acceleration, jerk_time, accel_time);
    } else {
        cruise_time = (distance_abs_rad - (2.0F * accel_dist)) / peak_velocity;
        if (cruise_time < 0.0F) {
            cruise_time = 0.0F;
        }
    }

    if (!basic_math::is_finite_float(jerk_time) ||
        !basic_math::is_finite_float(accel_time) ||
        !basic_math::is_finite_float(cruise_time) ||
        (jerk_time <= 0.0F)) {
        return false;
    }

    segments_[0] = {j_max, jerk_time};
    segments_[1] = {0.0F, accel_time};
    segments_[2] = {-j_max, jerk_time};
    segments_[3] = {0.0F, cruise_time};
    segments_[4] = {-j_max, jerk_time};
    segments_[5] = {0.0F, accel_time};
    segments_[6] = {j_max, jerk_time};

    profile_duration_s_ = 0.0F;
    for (const Segment& segment : segments_) {
        if (segment.duration_s > kMinPositive) {
            profile_duration_s_ += segment.duration_s;
        }
    }
    return profile_duration_s_ > 0.0F;
}

bool SCurvePlanner::build_continuous_profile(float remaining_distance_abs_rad,
                                             float direction,
                                             float max_velocity_rad_s,
                                             const SCurveSample& current_sample)
{
    if (!valid_positive(remaining_distance_abs_rad) ||
        !valid_positive(max_velocity_rad_s) ||
        !basic_math::is_finite_float(direction) ||
        !basic_math::is_finite_float(current_sample.position_rad) ||
        !basic_math::is_finite_float(current_sample.velocity_rad_s) ||
        !basic_math::is_finite_float(current_sample.acceleration_rad_s2)) {
        return false;
    }

    const float velocity_target =
        basic_math::clamp_float(current_sample.velocity_rad_s * direction,
                                0.0F,
                                max_velocity_rad_s);
    const float acceleration_target = current_sample.acceleration_rad_s2 * direction;
    float total_distance = remaining_distance_abs_rad;
    float elapsed = 0.0F;
    SCurveSample local_sample {};

    for (int i = 0; i < kReplanSolveIterations; ++i) {
        if (!build_profile(total_distance, max_velocity_rad_s)) {
            return false;
        }
        elapsed = find_replan_elapsed(velocity_target, acceleration_target);
        local_sample = sample_profile_local(elapsed);

        const float next_total_distance =
            remaining_distance_abs_rad + local_sample.position_rad;
        if (!valid_positive(next_total_distance)) {
            return false;
        }
        if (fabsf(next_total_distance - total_distance) <= config_.position_epsilon_rad) {
            total_distance = next_total_distance;
            break;
        }
        total_distance = next_total_distance;
    }

    if (!build_profile(total_distance, max_velocity_rad_s)) {
        return false;
    }
    elapsed = find_replan_elapsed(velocity_target, acceleration_target);
    local_sample = sample_profile_local(elapsed);
    total_distance = remaining_distance_abs_rad + local_sample.position_rad;
    if (!build_profile(total_distance, max_velocity_rad_s)) {
        return false;
    }
    elapsed = find_replan_elapsed(velocity_target, acceleration_target);
    local_sample = sample_profile_local(elapsed);

    start_position_rad_ =
        current_sample.position_rad - (direction * local_sample.position_rad);
    direction_ = direction;
    elapsed_s_ = elapsed;
    sample_ = sample_profile(elapsed_s_);
    sample_.active = true;
    sample_.finished = false;
    active_ = true;
    return true;
}

float SCurvePlanner::find_replan_elapsed(float velocity_rad_s,
                                         float acceleration_rad_s2) const
{
    if ((profile_duration_s_ <= 0.0F) ||
        !basic_math::is_finite_float(velocity_rad_s) ||
        !basic_math::is_finite_float(acceleration_rad_s2)) {
        return 0.0F;
    }

    const float velocity_target = basic_math::clamp_float(velocity_rad_s,
                                                          0.0F,
                                                          config_.max_velocity_limit_rad_s);
    float best_elapsed = 0.0F;
    float best_cost = kLargeCost;
    for (int i = 0; i <= kReplanSearchSamples; ++i) {
        const float elapsed =
            profile_duration_s_ *
            (static_cast<float>(i) / static_cast<float>(kReplanSearchSamples));
        const SCurveSample sample = sample_profile_local(elapsed);
        const float velocity_error = fabsf(sample.velocity_rad_s - velocity_target);
        const float acceleration_error =
            fabsf(sample.acceleration_rad_s2 - acceleration_rad_s2);
        const float cost = velocity_error + acceleration_error * config_.sample_time_s;
        if (cost < best_cost) {
            best_cost = cost;
            best_elapsed = elapsed;
        }
    }
    return best_elapsed;
}

SCurveSample SCurvePlanner::sample_profile_local(float elapsed_s) const
{
    float local_position = 0.0F;
    float local_velocity = 0.0F;
    float local_acceleration = 0.0F;
    float remaining = elapsed_s;

    for (const Segment& segment : segments_) {
        if (remaining <= 0.0F) {
            break;
        }
        const float dt = (remaining < segment.duration_s) ? remaining : segment.duration_s;
        if (dt > 0.0F) {
            integrate_segment(segment.jerk_rad_s3,
                              dt,
                              &local_position,
                              &local_velocity,
                              &local_acceleration);
        }
        remaining -= dt;
    }

    SCurveSample sample {};
    sample.position_rad = local_position;
    sample.velocity_rad_s = local_velocity;
    sample.acceleration_rad_s2 = local_acceleration;
    sample.elapsed_s = elapsed_s;
    sample.active = active_;
    sample.finished = !active_;
    return sample;
}

SCurveSample SCurvePlanner::sample_profile(float elapsed_s) const
{
    SCurveSample sample = sample_profile_local(elapsed_s);
    sample.position_rad = start_position_rad_ + (direction_ * sample.position_rad);
    sample.velocity_rad_s = direction_ * sample.velocity_rad_s;
    sample.acceleration_rad_s2 = direction_ * sample.acceleration_rad_s2;
    return sample;
}

SCurveSample SCurvePlanner::update()
{
    if (!configured_) {
        return sample_;
    }
    if (!active_) {
        sample_.active = false;
        sample_.finished = true;
        return sample_;
    }

    elapsed_s_ += config_.sample_time_s;
    if (elapsed_s_ >= profile_duration_s_) {
        finish_at_target();
        return sample_;
    }

    sample_ = sample_profile(elapsed_s_);
    sample_.active = true;
    sample_.finished = false;
    return sample_;
}

void SCurvePlanner::finish_at_target()
{
    sample_.position_rad = target_position_rad_;
    sample_.velocity_rad_s = 0.0F;
    sample_.acceleration_rad_s2 = 0.0F;
    sample_.elapsed_s = profile_duration_s_;
    sample_.active = false;
    sample_.finished = true;
    elapsed_s_ = profile_duration_s_;
    start_position_rad_ = target_position_rad_;
    active_ = false;
}

}  // namespace algo::motor::trajectory
