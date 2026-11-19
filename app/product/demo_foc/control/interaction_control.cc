#include "control/motor_control.h"
#include <algorithm>
#include <cmath>

namespace app
{
bool MotorControl::torque_calibration(uint8_t action, float measured_nm, InteractionConfig &c, float &rms)
{
    if (action == 0 && idle())
    {
        torque_calibration_.reset();
        return true;
    }
    if (action == 1 && enabled_ && control_mode_ == 0 && ready())
        return torque_calibration_.add(current_output_.current_a.q, measured_nm);
    if (action == 2 && idle())
        return torque_calibration_.finish(c.joint.torque, rms);
    return false;
}
using namespace algo::interaction;
bool MotorControl::validate_interaction(const InteractionConfig &c) const
{
    algo::motor::ExecutionLimits limits;
    JointInteraction joint;
    auto current_config = parameters_.current;
    current_config.resistance_ohm = c.electrical.resistance;
    current_config.ld_h = c.electrical.ld;
    current_config.lq_h = c.electrical.lq;
    current_config.flux_wb = c.electrical.flux;
    current_config.decoupling = c.electrical.decoupling;
    algo::motor::CurrentLoop current;
    if (c.execution.mechanical_power && !c.joint.torque.valid())
        return false;
    return limits.configure(c.execution, parameters_.joint.loop.torque.q_current_limit_a,
                            parameters_.current_frequency_hz, parameters_.joint.frequency_hz) &&
           joint.configure(c.joint, parameters_.joint.frequency_hz) && joint.set_cogging(c.cogging_nm, 64) &&
           current.configure(current_config);
}
bool MotorControl::configure_interaction(const InteractionConfig &c)
{
    if (!idle() || !validate_interaction(c))
        return false;
    auto current_config = parameters_.current;
    current_config.resistance_ohm = c.electrical.resistance;
    current_config.ld_h = c.electrical.ld;
    current_config.lq_h = c.electrical.lq;
    current_config.flux_wb = c.electrical.flux;
    current_config.decoupling = c.electrical.decoupling;
    if (!limits_.configure(c.execution, parameters_.joint.loop.torque.q_current_limit_a,
                           parameters_.current_frequency_hz, parameters_.joint.frequency_hz, true) ||
        !interaction_.configure(c.joint, parameters_.joint.frequency_hz) ||
        !interaction_.set_cogging(c.cogging_nm, 64) || !current_.configure(current_config))
        return false;
    invalidate_axis();
    interaction_config_ = c;
    interaction_snapshot_ = {};
    if (c.execution.position_limits)
        (void)interaction_.set_position_limits(c.execution.minimum_position_rad,
                                               c.execution.maximum_position_rad);
    external_seen_ = wrench_valid_ = false;
    external_ = {};
    external_age_s_ = 0;
    return true;
}
bool MotorControl::external_feedback(uint32_t sequence, uint8_t flags, const float *v, float age_s)
{
    if (!v || flags > 15 || !std::isfinite(age_s) || age_s < 0)
        return false;
    for (unsigned i = 0; i < 9; ++i)
        if (!std::isfinite(v[i]))
            return false;
    if ((flags & 4U) && v[2] < 0)
        return false;
    if (external_seen_ && int32_t(sequence - external_sequence_) <= 0)
        return true;
    external_seen_ = true;
    external_sequence_ = sequence;
    external_age_s_ = age_s;
    external_.external_nm = v[0];
    external_.effort_nm = v[1];
    external_.force_n = v[2];
    external_.external_valid = flags & 1U;
    external_.effort_valid = flags & 2U;
    external_.force_valid = flags & 4U;
    wrench_valid_ = flags & 8U;
    for (unsigned i = 0; i < 6; ++i)
        external_wrench_[i] = v[3 + i];
    return true;
}
JointFeedback MotorControl::interaction_feedback() const
{
    auto f = external_;
    const auto &v = feedback_.velocity();
    f.position_rad = feedback_.joint().multi_turn_angle_filted;
    f.load_velocity_rad_s = feedback_.joint().angular_velocity;
    f.velocity_rad_s = f.load_velocity_rad_s;
    f.motor_equivalent_rad = v.output_equivalent_multi_turn_angle_filted_rad;
    f.motor_equivalent_velocity = v.output_angular_velocity;
    f.iq_a = current_output_.current_a.q;
    if (external_age_s_ > interaction_config_.joint.external_timeout_s)
        f.external_valid = f.effort_valid = f.force_valid = false;
    return f;
}
bool MotorControl::interaction_command(const ModeCommand &c)
{
    const unsigned mode = unsigned(c.mode);
    if (mode < 4 || mode > 14)
        return false;
    const auto f = interaction_feedback();
    if (mode >= 11 && mode <= 13) return false;
    const auto joint_mode = mode == 14 ? JointMode::InertiaShaping : static_cast<JointMode>(mode - 4);
    if (!interaction_.active() || control_mode_ != mode) {
        if (!interaction_.start(joint_mode, c.physical, f, q_target_a_)) return false;
    } else if (!interaction_.submit(c.physical)) return false;
    control_mode_ = uint8_t(mode);
    return true;
}
float MotorControl::interaction_update(bool &valid, bool &energize, bool &complete)
{
    const auto r = interaction_.update(interaction_feedback());
    interaction_snapshot_ = r;
    valid = r.valid;
    energize = r.energize;
    complete = r.transition_complete;
    return r.iq_a;
}
} // namespace app
