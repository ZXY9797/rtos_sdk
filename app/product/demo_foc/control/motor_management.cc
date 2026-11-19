#include "control/motor_control.h"
#include <cmath>

namespace app {
using namespace algo::motor;

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
            cartesian_active_ = false;
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

#if defined(CONFIG_DEMO_FOC_MD007_V1)
JointFeedbackSample MotorControl::joint_feedback_sample() const
{
    JointFeedbackSample sample;
    sample.position_rad = feedback_.joint().multi_turn_angle_filted;
    sample.velocity_rad_s = feedback_.joint().angular_velocity;
    sample.current_a = parameters_.telemetry_q_current_sign * current_output_.current_a.q;
    sample.bus_voltage_v = feedback_.electrical().vbus_calib;
    sample.sample_us = joint_us_;
    sample.sequence = joint_count_;
    sample.faults = mode_status().faults;
    sample.valid = joint_valid_ && fast_count_ != 0U &&
                   calibration_.output.tmr1_calib.valid && calibration_.output.tmr2_calib.valid;
    return sample;
}
#endif

void MotorControl::record_timing(unsigned loop, uint32_t start_us, uint32_t end_us)
{
    constexpr uint32_t budgets[] {50U, 250U, 1000U};
    if (loop < 3U) timing_[loop].record(start_us, end_us, budgets[loop]);
}

bool MotorControl::encoder_frame(uint32_t id, const uint8_t* data, size_t size,
                                uint32_t processed_us)
{
    if ((id & ~0xFU) != 0x680U || !data || size != 3U) return false;
    encoder_.valid = true;
    encoder_.raw_count = ((uint32_t(data[0]) << 16U) | (uint32_t(data[1]) << 8U) |
                          uint32_t(data[2])) & 0x001FFFFFU;
    encoder_.processed_us = processed_us;
    ++encoder_.sequence;
    return true;
}

bool MotorControl::configure_capture(bool enabled, uint16_t divider)
{
    // At most 1 kHz: the 2 Mbaud UART also carries control and diagnostic traffic.
    if (enabled && divider < 4U) return false;
    capture_divider_ = enabled ? divider : 0U;
    capture_size_ = capture_head_ = 0U;
    capture_phase_ = 0U;
    capture_dropped_ = capture_sequence_ = paired_encoder_ = 0U;
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
    pair.encoder_sequence = encoder_.sequence;
    pair.encoder_processed_us = encoder_.processed_us;
    pair.encoder_raw_count = encoder_.raw_count;
    pair.dropped = capture_dropped_;
    pair.input_angle_rad = feedback_.rotor().mech_angle_calib;
    pair.output_raw_rad = feedback_.joint().single_turn_angle_raw;
    pair.output_calibrated_rad = feedback_.joint().single_turn_angle_calib;
    pair.flags = uint8_t(encoder_.valid) |
        uint8_t((encoder_.valid && encoder_.sequence != paired_encoder_) << 1U) |
        uint8_t(rotor_valid_ << 2U);
    paired_encoder_ = encoder_.sequence;
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
