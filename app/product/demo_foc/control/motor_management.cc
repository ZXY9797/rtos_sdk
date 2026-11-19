#include "control/motor_control.h"
#include <cmath>

namespace app {
using namespace algo::motor;

bool MotorControl::apply_network_target(const JointDriveTarget& target, uint32_t now_us)
{
    const auto& limits = interaction_config().execution;
    const auto sample = joint_feedback_sample();
    if (!axis_confirmed() || sample.faults ||
        !limits.position_limits || !sample.valid ||
        uint32_t(now_us - sample.sample_us) > 1000U ||
        sample.position_rad < limits.minimum_position_rad ||
        sample.position_rad > limits.maximum_position_rad)
        return false;
    constexpr JointTargetMode modes[] {JointTargetMode::Position, JointTargetMode::Velocity,
                                       JointTargetMode::Torque};
    const bool physical = target.extended && target.mode >= kDragMode;
    if (!physical && (target.mode < 8U || target.mode > 10U)) return false;
    const auto mode = physical ? static_cast<JointTargetMode>(5U + kImpedanceMode - target.mode)
                               : modes[target.mode - 8U];
    JointTargetOrder order;
    order.position_rad = target.position_urad * 1.0e-6F;
    order.velocity_rad_s = target.velocity_urad_s * 1.0e-6F;
    // CST is per mille of rated torque. The same k6216 torque constant
    // maps rated torque and target torque to rated and target q current.
    order.torque_a = target.torque_per_mille * 0.004F;
    order.host_timestamp_us = now_us;
    if ((mode == JointTargetMode::Position && (order.position_rad < limits.minimum_position_rad ||
                                               order.position_rad > limits.maximum_position_rad)) ||
        (mode == JointTargetMode::Velocity &&
         (order.velocity_rad_s < -limits.maximum_velocity_rad_s ||
          order.velocity_rad_s > limits.maximum_velocity_rad_s)) ||
        target.torque_per_mille < -1000 || target.torque_per_mille > 1000)
        return false;
    if (brake_phase() == BrakePhase::Stopping || brake_phase() == BrakePhase::Engaging)
        return false;
    const auto& b = interaction_config().brake;
    const float support = target.support_valid ? target.support_ma * .001F : b.hold_a;
    if (std::fabs(support) + b.tolerance_a > limits.continuous_current_a ||
        (support != 0 && std::fabs(support) <= b.tolerance_a)) return false;
    if (target.external_valid) {
        const float age = target.external_age_ms * .001F;
        if (age >= interaction_config_.joint.external_timeout_s) return false;
        const float values[9] {target.external_mnm * .001F};
        if (!external_feedback(target.external_sequence, 1U, values, age)) return false;
    }
    const bool model_available = physical && elastic_referenced_ && interaction_config_.joint.use_model_external &&
                                 interaction_config_.joint.elastic.stiffness_nm_rad > 0;
    const bool needs_external = b.collision_nm > 0 || target.mode == kAdmittanceMode || target.mode == kDragMode;
    if (needs_external && !model_available && !(external_flags() & 1U)) return false;
    const bool starting = !enabled_;
    if (physical) {
        const auto& j = interaction_config().joint;
        ModeCommand cmd;
        cmd.enabled = true;
        cmd.mode = mode;
        cmd.physical.position_rad = order.position_rad;
        cmd.physical.velocity_rad_s = order.velocity_rad_s;
        cmd.physical.torque_nm = target.torque_mnm * 0.001F;
        cmd.physical.stiffness = target.stiffness_milli * 0.001F;
        cmd.physical.damping = target.damping_milli * 0.001F;
        if (!j.torque.valid() || order.position_rad < limits.minimum_position_rad ||
            order.position_rad > limits.maximum_position_rad ||
            std::fabs(order.velocity_rad_s) > limits.maximum_velocity_rad_s ||
            std::fabs(cmd.physical.torque_nm) > j.torque_limit_nm) return false;
        if (drive_requested() && mode_status().mode != mode) return false;
        if (starting && !begin_handoff(now_us)) return false;
        if (!submit_interaction(cmd)) {
            if (starting) stop(true);
            else controlled_stop(now_us);
            return false;
        }
        support_current_a_ = support;
        mark_network_target(target.sequence, now_us);
        return true;
    }
    if (drive_requested()) {
        if (mode_status().mode != mode) return false;
    } else {
        if (!select_mode(mode) || !begin_handoff(now_us)) return false;
    }
    if (submit(order)) {
        support_current_a_ = support;
        mark_network_target(target.extended ? target.sequence : target.sync, now_us);
        return true;
    }
    if (starting) stop(true);
    else controlled_stop(now_us);
    return false;
}

bool MotorControl::mode_command(const ModeCommand& command)
{
    const auto& target = command.target;
    if (!configured_ || static_cast<unsigned>(command.action) > 4U ||
        static_cast<unsigned>(command.mode) > 14U || !std::isfinite(target.position_rad) ||
        !std::isfinite(target.velocity_rad_s) || !std::isfinite(target.torque_a) ||
        (command.action == ModeAction::Hold &&
         (command.mode != JointTargetMode::Position || !command.enabled)) ||
        (command.action == ModeAction::Disable && command.enabled))
        return false;
    // An old or repeated application sequence never restarts a transition.
    if (mode_managed_ && int32_t(command.sequence - accepted_sequence_) <= 0) return true;
    const bool disable = command.action == ModeAction::Disable;
    if (!disable) {
        if (motor_operation_active() || lut_.active() || lut_.pending_persistence() ||
            (local_disable_ && command.enabled) ||
            (holding_ &&
             (command.action == ModeAction::Target || command.action == ModeAction::SelectMode)) ||
            (command.enabled && !ready()) ||
            (command.action == ModeAction::SelectMode && command.enabled != enabled_) ||
            (unsigned(command.mode) < 4 && command.action != ModeAction::SelectMode && command.enabled &&
             std::fabs(target.torque_a) > parameters_.joint.target.torque_abs_limit_a))
            return false;
    }
    if (disable || !command.enabled) {
        const bool previously_disabled = local_disable_;
        stop(true);
        local_disable_ = previously_disabled;
    } else {
        if (!enabled_) reset_control();
        if (unsigned(command.mode) >= 4)
        {
            if (command.action == ModeAction::SelectMode || !interaction_command(command))
                return false;
        }
        else
        {
            const bool leaving_interaction = control_mode_ >= 4;
            joint_.track_applied(q_target_a_);
            if (!joint_.select_mode(command.mode, feedback_.loop_feedback(), true, leaving_interaction))
                return false;
            if (command.action != ModeAction::SelectMode && !joint_.submit(target))
            {
                fault();
                return false;
            }
            interaction_.stop();
            control_mode_ = uint8_t(command.mode);
        }
        stopping_ = false;
        enabled_ = true;
    }
    // Disable must not depend on usable feedback or attempt a mode transition.
    if (!command.enabled && !disable && unsigned(command.mode) < 4 &&
        !joint_.select_mode(command.mode, feedback_.loop_feedback()))
        return false;
    if(!command.enabled && !disable)control_mode_=uint8_t(command.mode);
    mode_managed_ = mode_pending_ = true;
    accepted_sequence_ = command.sequence;
    accepted_action_ = command.action;
    accepted_enabled_ = command.enabled;
    limits_.touch_command();
    if (command.action == ModeAction::Hold) holding_ = true;
    if (disable) local_disable_ = false;
    return true;
}

void MotorControl::confirm_mode(bool transition_complete)
{
    if (!mode_pending_) return;
    const bool disabled = !enabled_ && !current_output_.energize && !backlash_.active();
    const bool faulted = hardware_fault_ || protection_.active();
    if ((disabled && !accepted_enabled_ && (!faulted || accepted_action_ == ModeAction::Disable)) ||
        (!faulted && enabled_ && current_output_.energize && transition_complete)) {
        applied_sequence_ = accepted_sequence_;
        mode_pending_ = false;
        if (accepted_action_ == ModeAction::Restore || accepted_action_ == ModeAction::Disable)
            holding_ = false;
    }
}

ModeStatus MotorControl::mode_status() const
{
    ModeStatus result;
    result.mode = static_cast<JointTargetMode>(control_mode_);
    result.accepted_sequence = accepted_sequence_;
    result.applied_sequence = applied_sequence_;
    result.enabled = enabled_ && current_output_.energize;
    result.holding = holding_;
    result.busy = mode_pending_;
    result.locally_disabled = local_disable_;
    result.faults = protection_.fault_mask() | (hardware_fault_ ? 1U : 0U) | (limits_.state().faults << 6U);
    return result;
}

MotorObservation MotorControl::observation() const
{
    MotorObservation result;
    result.electrical = feedback_.electrical();
    result.joint = joint_.trace();
    result.joint.mode = static_cast<JointTargetMode>(control_mode_);
    result.temperature = temperature_;
    result.rotor_tmr[0] = sample_.rotor_tmr1;
    result.rotor_tmr[1] = sample_.rotor_tmr2;
    result.output_tmr[0] = sample_.output_tmr1;
    result.output_tmr[1] = sample_.output_tmr2;
    result.input_angle_rad = feedback_.rotor().mech_angle_calib;
    result.electrical_angle_rad = feedback_.rotor().elec_angle_calib;
    result.output_raw_rad = feedback_.joint().single_turn_angle_raw;
    result.position_rad = feedback_.joint().multi_turn_angle_filted;
    result.velocity_rad_s = feedback_.velocity().output_angular_velocity;
    result.motor_velocity_rad_s = feedback_.velocity().angular_velocity;
    result.q_target_a = q_target_a_;
    result.q_feedback_a = parameters_.telemetry_q_current_sign * current_output_.current_a.q;
    result.current_a = current_output_.current_a;
    result.voltage_v = current_output_.voltage_v;
    result.fast_sequence = fast_count_;
    result.joint_sequence = joint_count_;
    result.input_us = input_us_;
    result.joint_us = joint_us_;
    for (unsigned i = 0; i < 3U; ++i) result.timing[i] = timing_[i];
    return result;
}

JointFeedbackSample MotorControl::joint_feedback_sample() const
{
    JointFeedbackSample sample;
    sample.position_rad = feedback_.joint().multi_turn_angle_filted;
    sample.velocity_rad_s = feedback_.joint().angular_velocity;
    sample.current_a = parameters_.telemetry_q_current_sign * current_output_.current_a.q;
    sample.bus_voltage_v = feedback_.electrical().vbus_calib;
    sample.sample_us = joint_us_;
    sample.sequence = joint_count_;
    sample.faults = mode_status().faults | stop_reason_ | first_fault_;
    sample.first_fault = first_fault_;
    sample.fault_us = first_fault_us_;
    sample.fault_phase = uint8_t(first_fault_phase_);
    sample.applied_sequence = network_sequence_;
    sample.applied_us = network_applied_us_;
    sample.apply_delay_us = applied_delay_us_;
    sample.applied_valid = network_applied_;
    sample.brake_phase = uint8_t(brake_.phase());
    const auto& j = interaction_config_.joint;
    if (j.torque.valid()) {
        sample.output_nm = j.torque.torque(current_output_.current_a.q);
        sample.effort_flags |= 1U;
    }
    if ((external_flags() & 1U) != 0) {
        sample.external_nm = external_.external_nm;
        sample.external_age_us = uint32_t(std::min(4000.0F, external_age_s_) * 1e6F);
        sample.effort_flags |= 2U;
    } else if (elastic_referenced_ && j.use_model_external && j.elastic.stiffness_nm_rad > 0 &&
               interaction_.active() && interaction_snapshot_.valid && enabled_) {
        sample.external_nm = interaction_snapshot_.external_estimate_nm;
        sample.effort_flags |= 4U;
    }
    sample.valid = joint_valid_ && fast_count_ != 0U &&
                   int32_t(joint_us_ - input_us_) <= 1000 &&
                   calibration_.output.tmr1_calib.valid && calibration_.output.tmr2_calib.valid;
    return sample;
}

void MotorControl::record_timing(unsigned loop, uint32_t start_us, uint32_t end_us)
{
    constexpr uint32_t budgets[] {50U, 250U, 1000U};
    if (loop < 3U) timing_[loop].record(start_us, end_us, budgets[loop]);
}

bool MotorControl::configure_capture(bool enabled, uint16_t divider)
{
    // At most 1 kHz: the 2 Mbaud UART also carries control and diagnostic traffic.
    if (enabled && divider < 4U) return false;
    capture_divider_ = enabled ? divider : 0U;
    capture_size_ = capture_head_ = 0U;
    capture_phase_ = 0U;
    capture_dropped_ = capture_sequence_ = 0U;
    return true;
}

void MotorControl::capture_pair(uint32_t timestamp_us)
{
    if (!capture_divider_ || ++capture_phase_ < capture_divider_) return;
    capture_phase_ = 0U;
    const uint32_t sequence = ++capture_sequence_;
    if (capture_size_ == 16U) { ++capture_dropped_; return; }
    auto& pair = capture_[(capture_head_ + capture_size_) % 16U];
    pair = {};
    pair.sequence = sequence;
    pair.joint_sequence = joint_count_;
    pair.joint_us = timestamp_us;
    pair.input_us = input_us_;
    pair.dropped = capture_dropped_;
    pair.input_angle_rad = feedback_.rotor().mech_angle_calib;
    pair.output_raw_rad = feedback_.joint().single_turn_angle_raw;
    pair.output_calibrated_rad = feedback_.joint().single_turn_angle_calib;
    pair.input_valid = rotor_valid_;
    ++capture_size_;
}

bool MotorControl::peek_capture(CalibrationPair& pair) const
{
    if (!capture_size_) return false;
    pair = capture_[capture_head_];
    return true;
}

void MotorControl::consume_capture(uint32_t sequence)
{
    if (capture_size_ && capture_[capture_head_].sequence == sequence) {
        capture_head_ = uint8_t((capture_head_ + 1U) % 16U);
        --capture_size_;
    }
}

bool MotorControl::calibration_record(CalibrationRecordId id, RecordView& record) const
{
    switch (id) {
    case CalibrationRecordId::CurrentLinear:
        record = {id, &calibration_.current, sizeof(calibration_.current)}; break;
    case CalibrationRecordId::BusVoltage:
        record = {id, &calibration_.bus, sizeof(calibration_.bus)}; break;
    case CalibrationRecordId::InputTmr:
        record = {id, &calibration_.input, sizeof(calibration_.input)}; break;
    case CalibrationRecordId::OutputTmr:
        record = {id, &calibration_.output, sizeof(calibration_.output)}; break;
    case CalibrationRecordId::ElectricalAngle:
        record = {id, &calibration_.electrical, sizeof(calibration_.electrical)}; break;
    case CalibrationRecordId::JointZero:
        record = {id, &calibration_.joint.joint_zero_angle, sizeof(JointAngleCalibData)}; break;
    case CalibrationRecordId::JointOffset:
        record = {id, &calibration_.joint.joint_offset_angle, sizeof(JointAngleCalibData)}; break;
    default: return false; // LUT storage is queried through the transaction codec.
    }
    return true;
}

bool MotorControl::erase_record(CalibrationRecordId id, bool verified)
{
    // The adapter pauses acquisition around NVS removal and this publication.
    if (!verified || !exclusive_available()) return false;
    switch (id) {
    case CalibrationRecordId::CurrentLinear: calibration_.current = {}; break;
    case CalibrationRecordId::BusVoltage: calibration_.bus = {}; break;
    case CalibrationRecordId::InputTmr: calibration_.input = {}; break;
    case CalibrationRecordId::OutputTmr: calibration_.output = {}; break;
    case CalibrationRecordId::ElectricalAngle: calibration_.electrical = {}; break;
    case CalibrationRecordId::JointZero: calibration_.joint.joint_zero_angle = {}; break;
    case CalibrationRecordId::JointOffset: calibration_.joint.joint_offset_angle = {}; break;
    case CalibrationRecordId::OutputLut: output_lut_ = {}; break;
    case CalibrationRecordId::InputLut: break;
    default: return false;
    }
    auto_calibration_.reset(false);
    reset_control();
    return calibrator_.configure(parameters_.calibration) && configure_feedback();
}

} // namespace app
