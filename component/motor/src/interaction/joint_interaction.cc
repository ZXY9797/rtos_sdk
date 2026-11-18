#include <algo/interaction/joint_interaction.h>
#include <algorithm>
#include <cmath>

namespace algo::interaction
{
namespace
{
constexpr float pi = 3.14159265358979323846F;
float clip(float x, float limit) { return std::clamp(x, -limit, limit); }
float approach(float x, float target, float step) { return x + clip(target - x, step); }
float deadband(float x, float width) { return std::copysign(std::max(0.0F, std::fabs(x) - width), x); }
bool target_valid(const JointTarget &t)
{
    return std::isfinite(t.position_rad) && std::isfinite(t.velocity_rad_s) && std::isfinite(t.torque_nm) &&
           std::isfinite(t.stiffness) && t.stiffness >= 0.0F && std::isfinite(t.damping) &&
           t.damping >= 0.0F && std::isfinite(t.force_n) && t.force_n >= 0.0F;
}
bool feedback_valid(const JointFeedback &f)
{
    const float values[]{f.position_rad,
                         f.velocity_rad_s,
                         f.load_velocity_rad_s,
                         f.motor_equivalent_rad,
                         f.motor_equivalent_velocity,
                         f.iq_a};
    for (float x : values)
        if (!std::isfinite(x))
            return false;
    return (!f.external_valid || std::isfinite(f.external_nm)) &&
           (!f.effort_valid || std::isfinite(f.effort_nm)) &&
           (!f.force_valid || (std::isfinite(f.force_n) && f.force_n >= 0.0F));
}
} // namespace

SpringDamperOutput spring_damper(float error, float velocity_error, float stiffness, float damping,
                                 float feedforward, float position_limit, float damping_limit,
                                 float total_limit)
{
    SpringDamperOutput r;
    r.position = clip(stiffness * error, position_limit);
    r.damping = clip(damping * velocity_error, damping_limit);
    r.total = clip(r.position + r.damping + feedforward, total_limit);
    return r;
}

bool TorqueMap::valid() const
{
    return std::isfinite(joint_nm_per_iq_a) && std::fabs(joint_nm_per_iq_a) > 1.0e-6F &&
           std::isfinite(bias_nm);
}
void TorqueCalibration::reset() { *this = {}; }
bool TorqueCalibration::add(float x, float y)
{
    if (!std::isfinite(x) || !std::isfinite(y) || count_ == UINT32_MAX)
        return false;
    ++count_;
    sx_ += x;
    sy_ += y;
    sxx_ += double(x) * x;
    sxy_ += double(x) * y;
    syy_ += double(y) * y;
    return true;
}
bool TorqueCalibration::finish(TorqueMap &output, float &rms) const
{
    const double n = count_, d = n * sxx_ - sx_ * sx_;
    if (count_ < 3U || d < n * n * 1.0e-6)
        return false;
    const double k = (n * sxy_ - sx_ * sy_) / d, b = (sy_ - k * sx_) / n;
    TorqueMap candidate{float(k), float(b)};
    if (!candidate.valid())
        return false;
    rms = float(std::sqrt(
        std::max(0.0, (syy_ + k * k * sxx_ + n * b * b - 2 * k * sxy_ - 2 * b * sy_ + 2 * k * b * sx_) / n)));
    output = candidate;
    return std::isfinite(rms);
}

bool Admittance::configure(const AdmittanceConfig &c, float dt)
{
    const float values[]{c.inertia,        c.damping,        c.stiffness,
                         c.position_limit, c.velocity_limit, c.acceleration_limit,
                         c.jerk_limit,     c.force_deadband};
    for (float x : values)
        if (!std::isfinite(x) || x < 0.0F)
            return false;
    if (c.inertia <= 0.0F || c.position_limit <= 0.0F || c.velocity_limit <= 0.0F ||
        c.acceleration_limit <= 0.0F || c.jerk_limit <= 0.0F || !std::isfinite(dt) || dt <= 0.0F)
        return false;
    config_ = c;
    dt_ = dt;
    configured_ = true;
    reset();
    return true;
}
bool Admittance::update(float external, float reference, MotionState &output)
{
    if (!configured_ || !std::isfinite(external) || !std::isfinite(reference))
        return false;
    const auto &c = config_;
    const float force = deadband(external - reference, c.force_deadband);
    // Backward Euler solves damping and stiffness implicitly before motion limits.
    const float v = (c.inertia * state_.velocity + dt_ * (force - c.stiffness * state_.position)) /
                    (c.inertia + c.damping * dt_ + c.stiffness * dt_ * dt_);
    const float acceleration = approach(
        state_.acceleration, clip((v - state_.velocity) / dt_, c.acceleration_limit), c.jerk_limit * dt_);
    const float velocity = clip(state_.velocity + dt_ * acceleration, c.velocity_limit);
    const float position = clip(state_.position + dt_ * velocity, c.position_limit);
    const bool constrained = (position >= c.position_limit && velocity > 0.0F) ||
                             (position <= -c.position_limit && velocity < 0.0F);
    state_ = {position, constrained ? 0.0F : velocity, constrained ? 0.0F : acceleration};
    output = state_;
    return std::isfinite(position) && std::isfinite(velocity);
}
void Admittance::accept_position(float position)
{
    if (!configured_ || !std::isfinite(position))
        return;
    const float accepted = clip(position, config_.position_limit);
    if (accepted != state_.position)
    {
        state_.velocity = state_.acceleration = 0.0F;
    }
    state_.position = accepted;
}

bool EnergyTank::configure(float initial, float maximum)
{
    if (!std::isfinite(initial) || !std::isfinite(maximum) || initial < 0.0F || maximum < initial)
        return false;
    energy_ = initial;
    maximum_ = maximum;
    return true;
}
float EnergyTank::limit(float effort, float velocity, float dt)
{
    effort = preview(effort, velocity, dt);
    const float work = effort * velocity * dt;
    if (std::isfinite(work) && work > 0)
        energy_ = std::max(0.0F, energy_ - work);
    return effort;
}
float EnergyTank::preview(float effort, float velocity, float dt) const
{
    if (!std::isfinite(effort) || !std::isfinite(velocity) || !std::isfinite(dt) || dt <= 0.0F)
        return 0.0F;
    const float work = effort * velocity * dt;
    // Conservative command-work budget: never credit unmeasured regeneration.
    if (work > energy_ && work > 0.0F)
    {
        effort *= energy_ / work;
    }
    return effort;
}

bool JointInteraction::configure(const InteractionConfig &c, float hz)
{
    const float nonnegative[]{c.servo_stiffness,
                              c.servo_damping,
                              c.stiffness_rate,
                              c.damping_rate,
                              c.torque_limit_nm,
                              c.torque_rate_nm_s,
                              c.external_timeout_s,
                              c.desired_inertia,
                              c.compensation.viscous_nm_s,
                              c.compensation.coulomb_nm,
                              c.compensation.breakaway_nm,
                              c.compensation.backlash_rad,
                              c.compensation.backlash_rate_rad_s,
                              c.compensation.load_inertia,
                              c.elastic.stiffness_nm_rad,
                              c.elastic.damping_nm_s,
                              c.elastic.backlash_rad,
                              c.elastic.resonance_damping,
                              c.force.kp,
                              c.force.ki,
                              c.force.maximum_nm,
                              c.force.rate_nm_s,
                              c.force.contact_n,
                              c.force.release_n,
                              c.force.contact_time_s,
                              c.force.lever_arm_m,
                              c.force.approach_velocity,
                              c.force.release_distance,
                              c.force.maximum_travel_rad,
                              c.force.approach_timeout_s};
    for (float x : nonnegative)
        if (!std::isfinite(x) || x < 0.0F)
            return false;
    const float finite[]{c.torque.joint_nm_per_iq_a,       c.torque.bias_nm,
                         c.compensation.gravity_bias_nm,   c.compensation.gravity_amplitude_nm,
                         c.compensation.gravity_phase_rad, c.elastic.zero_rad};
    for (float x : finite)
        if (!std::isfinite(x))
            return false;
    if (!std::isfinite(hz) || hz <= 0.0F || c.torque_limit_nm <= 0.0F || c.torque_rate_nm_s <= 0.0F ||
        c.stiffness_rate <= 0.0F || c.damping_rate <= 0.0F || c.external_timeout_s <= 0.0F ||
        !std::isfinite(c.compensation.stribeck_velocity) || c.compensation.stribeck_velocity <= 0.0F ||
        !std::isfinite(c.compensation.sign_velocity) || c.compensation.sign_velocity <= 0.0F ||
        !std::isfinite(c.elastic.observer_alpha) || c.elastic.observer_alpha <= 0.0F ||
        c.elastic.observer_alpha > 1.0F || !std::isfinite(c.force.closing_direction) ||
        std::fabs(c.force.closing_direction) != 1.0F || c.force.release_n > c.force.contact_n ||
        c.force.maximum_travel_rad <= 0.0F || c.force.approach_timeout_s <= 0.0F ||
        c.force.rate_nm_s <= 0.0F || c.force.approach_velocity <= 0 ||
        c.force.release_distance > c.force.maximum_travel_rad ||
        !admittance_.configure(c.admittance, 1.0F / hz) ||
        !tank_.configure(c.initial_energy_j, c.maximum_energy_j))
        return false;
    config_ = c;
    minimum_rad_ = -3.4e38F;
    maximum_rad_ = 3.4e38F;
    dt_ = 1.0F / hz;
    configured_ = true;
    stop();
    return true;
}
bool JointInteraction::start(JointMode mode, const JointTarget &t, const JointFeedback &f, float iq)
{
    if (!configured_ || !config_.torque.valid() || unsigned(mode) > unsigned(JointMode::InertiaShaping) ||
        !target_valid(t) || !feedback_valid(f) || !std::isfinite(iq))
        return false;
    if ((mode == JointMode::Admittance || mode == JointMode::Drag || mode == JointMode::Grip) &&
        config_.servo_stiffness <= 0.0F)
        return false;
    if (mode == JointMode::Elastic && config_.elastic.stiffness_nm_rad <= 0.0F)
        return false;
    if ((mode == JointMode::Force || mode == JointMode::Grip) &&
        (config_.force.lever_arm_m <= 0.0F ||
         (!f.force_valid && !f.effort_valid &&
          !(config_.use_model_external && config_.elastic.stiffness_nm_rad > 0.0F))))
        return false;
    if ((mode == JointMode::Admittance || mode == JointMode::Drag) && !f.external_valid &&
        !(config_.use_model_external && config_.elastic.stiffness_nm_rad > 0.0F))
        return false;
    if (mode == JointMode::InertiaShaping &&
        (config_.desired_inertia <= 0 || config_.compensation.load_inertia <= 0 ||
         (!f.external_valid && !(config_.use_model_external && config_.elastic.stiffness_nm_rad > 0))))
        return false;
    stop();
    mode_ = mode;
    target_ = t;
    origin_ = reference_ = f.position_rad;
    previous_velocity_ = f.load_velocity_rad_s;
    applied_nm_ = config_.torque.torque(iq);
    grip_ = t.force_n > 0.0F ? GripState::Approach : GripState::Release;
    active_ = true;
    return true;
}
bool JointInteraction::submit(const JointTarget &t)
{
    if (!active_ || !target_valid(t))
        return false;
    if (mode_ == JointMode::Grip && t.force_n == 0.0F && target_.force_n > 0.0F)
    {
        grip_ = GripState::Release;
        reference_ = result_.reference_rad;
        elapsed_ = 0.0F;
    }
    target_ = t;
    return true;
}
void JointInteraction::stop()
{
    active_ = false;
    result_ = {};
    grip_ = GripState::Idle;
    admittance_.reset();
    (void)tank_.configure(config_.initial_energy_j, config_.maximum_energy_j);
    force_integral_ = force_unlimited_ = applied_nm_ = stiffness_ = damping_ = 0.0F;
    backlash_ = external_filtered_ = elastic_filtered_ = elapsed_ = contact_elapsed_ = 0.0F;
}
bool JointInteraction::set_cogging(const float *table, size_t count)
{
    if (active_ || count > 64U || (count && (!table || count < 2U)))
        return false;
    for (size_t i = 0; i < count; ++i)
        if (!std::isfinite(table[i]))
            return false;
    for (size_t i = 0; i < count; ++i)
        cogging_[i] = table[i];
    cogging_count_ = uint8_t(count);
    return true;
}
float JointInteraction::compensation(float q, float velocity) const
{
    const auto &c = config_.compensation;
    const float ratio = velocity / c.stribeck_velocity;
    const float friction = c.coulomb_nm + (c.breakaway_nm - c.coulomb_nm) / (1.0F + ratio * ratio);
    float value = c.gravity_bias_nm + c.gravity_amplitude_nm * std::sin(q + c.gravity_phase_rad) +
                  c.viscous_nm_s * velocity + friction * velocity / (std::fabs(velocity) + c.sign_velocity);
    if (cogging_count_)
    {
        float phase = std::fmod(q / (2.0F * pi), 1.0F);
        if (phase < 0.0F)
            phase += 1.0F;
        const float index = phase * cogging_count_;
        const auto i = unsigned(index) % cogging_count_;
        value +=
            cogging_[i] + (index - std::floor(index)) * (cogging_[(i + 1U) % cogging_count_] - cogging_[i]);
    }
    return value;
}
bool JointInteraction::set_position_limits(float low, float high)
{
    if (active_ || !std::isfinite(low) || !std::isfinite(high) || low >= high)
        return false;
    minimum_rad_ = low;
    maximum_rad_ = high;
    return true;
}
float JointInteraction::impedance(float q, float v, float ff, const JointFeedback &f)
{
    q = std::clamp(q, minimum_rad_, maximum_rad_);
    return spring_damper(q - f.position_rad, v - f.velocity_rad_s, stiffness_, damping_, ff,
                         config_.torque_limit_nm, config_.torque_limit_nm, config_.torque_limit_nm)
        .total;
}
float JointInteraction::force_control(float target, float measured, float velocity)
{
    const auto &c = config_.force;
    const float error = target - measured;
    force_integral_ = clip(force_integral_ + c.ki * dt_ * error, c.maximum_nm);
    force_unlimited_ = target + c.kp * error + force_integral_ - damping_ * velocity;
    const float limited = clip(force_unlimited_, c.maximum_nm);
    force_integral_ = clip(force_integral_ + limited - force_unlimited_, c.maximum_nm);
    force_unlimited_ = limited;
    return approach(applied_nm_, limited, c.rate_nm_s * dt_);
}
void JointInteraction::track_applied(float iq)
{
    if (!active_ || !std::isfinite(iq))
        return;
    const float applied = config_.torque.torque(iq);
    if (mode_ == JointMode::Force || (mode_ == JointMode::Grip && grip_ == GripState::Hold))
        force_integral_ =
            clip(force_integral_ + applied - compensation_nm_ - force_unlimited_, config_.force.maximum_nm);
    applied_nm_ = applied;
}
float JointInteraction::limit_applied(float iq, float velocity)
{
    if (!active_ || !std::isfinite(iq) || !std::isfinite(velocity))
        return 0;
    if (config_.use_energy_tank)
        iq = config_.torque.current(tank_.limit(config_.torque.torque(iq), velocity, dt_));
    result_.energy_j = tank_.energy();
    return iq;
}
JointResult JointInteraction::update(const JointFeedback &f)
{
    result_ = {};
    if (!active_)
        return result_;
    if (!feedback_valid(f))
    {
        stop();
        return result_;
    }
    elapsed_ += dt_;
    const auto &c = config_;
    const auto &t = target_;
    const float deflection =
        deadband(f.motor_equivalent_rad - f.position_rad - c.elastic.zero_rad, c.elastic.backlash_rad);
    const float relative_v = f.motor_equivalent_velocity - f.load_velocity_rad_s;
    const float elastic = c.elastic.stiffness_nm_rad * deflection + c.elastic.damping_nm_s * relative_v;
    elastic_filtered_ += c.elastic.observer_alpha * (elastic - elastic_filtered_);
    const float acceleration = (f.load_velocity_rad_s - previous_velocity_) / dt_;
    previous_velocity_ = f.load_velocity_rad_s;
    const float model_external = c.compensation.load_inertia * acceleration +
                                 compensation(f.position_rad, f.load_velocity_rad_s) - elastic_filtered_;
    const bool model_valid = c.use_model_external && c.elastic.stiffness_nm_rad > 0.0F;
    const bool external_valid = f.external_valid || model_valid;
    const float external = f.external_valid ? f.external_nm : model_external;
    external_filtered_ += c.elastic.observer_alpha * (external - external_filtered_);
    const bool effort_valid = f.effort_valid || f.force_valid || model_valid;
    const float effort = f.effort_valid  ? f.effort_nm
                         : f.force_valid ? c.force.closing_direction * c.force.lever_arm_m * f.force_n
                                         : elastic_filtered_;
    const float force =
        f.force_valid
            ? f.force_n
            : std::max(0.0F, c.force.closing_direction * effort / std::max(c.force.lever_arm_m, 1.0e-6F));
    if (((mode_ == JointMode::Admittance || mode_ == JointMode::Drag || mode_ == JointMode::InertiaShaping) &&
         !external_valid) ||
        ((mode_ == JointMode::Force || mode_ == JointMode::Grip) && !effort_valid))
    {
        stop();
        return result_;
    }
    const bool servo = mode_ == JointMode::Admittance || mode_ == JointMode::Drag || mode_ == JointMode::Grip;
    stiffness_ = approach(stiffness_, servo ? c.servo_stiffness : t.stiffness, c.stiffness_rate * dt_);
    damping_ = approach(damping_, servo ? c.servo_damping : t.damping, c.damping_rate * dt_);
    float q = std::clamp(t.position_rad, minimum_rad_, maximum_rad_), v = t.velocity_rad_s,
          torque = t.torque_nm;
    const float backlash_target =
        std::fabs(v) > 1.0e-4F ? std::copysign(0.5F * c.compensation.backlash_rad, v) : backlash_;
    backlash_ = approach(backlash_, backlash_target, c.compensation.backlash_rate_rad_s * dt_);
    switch (mode_)
    {
    case JointMode::Torque:
        break;
    case JointMode::Impedance:
        torque = impedance(q + backlash_, v, torque, f);
        break;
    case JointMode::InertiaShaping:
        // J*qdd = tau + external - compensation; virtual desired acceleration is
        // (K*position_error + D*velocity_error + feedforward + external)/Mdes.
        torque = c.compensation.load_inertia / c.desired_inertia *
                     (stiffness_ * (q - f.position_rad) + damping_ * (v - f.velocity_rad_s) + t.torque_nm +
                      external_filtered_) -
                 external_filtered_;
        break;
    case JointMode::Elastic:
        torque = impedance(q + backlash_, v, torque, f) - c.elastic.resonance_damping * relative_v;
        break;
    case JointMode::Admittance:
    case JointMode::Drag:
    {
        MotionState motion;
        if (!admittance_.update(external_filtered_, mode_ == JointMode::Drag ? 0.0F : -t.torque_nm, motion))
        {
            stop();
            return result_;
        }
        q = origin_ + motion.position;
        v = motion.velocity;
        const float accepted = std::clamp(q, minimum_rad_, maximum_rad_);
        if (accepted != q)
        {
            q = accepted;
            v = 0;
            admittance_.accept_position(q - origin_);
        }
        torque = impedance(q, v, 0.0F, f);
        break;
    }
    case JointMode::Force:
        torque = force_control(c.force.closing_direction * c.force.lever_arm_m * t.force_n + t.torque_nm,
                               effort, f.velocity_rad_s);
        break;
    case JointMode::Grip:
    {
        const float direction = c.force.closing_direction;
        if (grip_ == GripState::Approach)
        {
            reference_ += direction * c.force.approach_velocity * dt_;
            if (force >= c.force.contact_n)
                contact_elapsed_ += dt_;
            else
                contact_elapsed_ = 0.0F;
            if (contact_elapsed_ >= c.force.contact_time_s)
            {
                grip_ = GripState::Hold;
                force_integral_ = 0.0F;
                contact_elapsed_ = 0;
            }
            if (std::fabs(reference_ - origin_) >= c.force.maximum_travel_rad ||
                elapsed_ > c.force.approach_timeout_s)
                grip_ = GripState::Fault;
        }
        else if (grip_ == GripState::Hold)
        {
            if (force < c.force.release_n)
                contact_elapsed_ += dt_;
            else
                contact_elapsed_ = 0;
            if (contact_elapsed_ > c.force.contact_time_s)
                grip_ = GripState::Fault;
        }
        else if (grip_ == GripState::Release)
        {
            const float goal = origin_ - direction * c.force.release_distance;
            reference_ = approach(reference_, goal, c.force.approach_velocity * dt_);
            if (reference_ == goal && std::fabs(f.position_rad - goal) < 0.01F && force <= c.force.release_n)
                grip_ = GripState::Finished;
            if (elapsed_ > c.force.approach_timeout_s)
                grip_ = GripState::Fault;
        }
        q = reference_;
        v = 0.0F;
        if (std::fabs(f.position_rad - origin_) > c.force.maximum_travel_rad || q < minimum_rad_ ||
            q > maximum_rad_)
            grip_ = GripState::Fault;
        torque = grip_ == GripState::Hold
                     ? force_control(direction * c.force.lever_arm_m * t.force_n, effort, f.velocity_rad_s)
                     : impedance(q, v, 0.0F, f);
        if (grip_ == GripState::Fault)
        {
            active_ = false;
            result_.grip = grip_;
            return result_;
        }
        if (grip_ == GripState::Finished)
        {
            active_ = false;
            result_.valid = true;
            result_.grip = grip_;
            result_.reference_rad = q;
            return result_;
        }
        break;
    }
    }
    compensation_nm_ = compensation(f.position_rad, f.velocity_rad_s);
    torque += compensation_nm_;
    torque = approach(applied_nm_, clip(torque, c.torque_limit_nm), c.torque_rate_nm_s * dt_);
    if (c.use_energy_tank)
        torque = tank_.preview(torque, f.velocity_rad_s, dt_);
    const float iq = c.torque.current(torque);
    if (!std::isfinite(iq))
    {
        stop();
        return result_;
    }
    result_ = {iq,
               torque,
               q,
               v,
               external_filtered_,
               elastic_filtered_,
               tank_.energy(),
               stiffness_,
               damping_,
               grip_,
               true,
               true,
               elapsed_ >= 0.020F,
               force >= c.force.contact_n};
    return result_;
}
} // namespace algo::interaction
