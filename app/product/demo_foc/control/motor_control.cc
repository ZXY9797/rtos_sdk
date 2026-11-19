#include "control/motor_control.h"

#include <cmath>
#include <cstring>

namespace app {
using namespace algo::motor;

// Construct the large filter and calibration state in RAM at startup.
MotorControl::MotorControl() = default;

MotorSensorCalibData MotorControl::sensors() const
{
    MotorSensorCalibData result;
    result.input_sensor_calib = calibration_.input;
    result.elec_angle_calib = calibration_.electrical;
    result.output_tmr1_calib = calibration_.output.tmr1_calib;
    result.output_tmr2_calib = calibration_.output.tmr2_calib;
    result.output_angle_calib = &output_view_;
    return result;
}

bool MotorControl::configure_feedback()
{
    MotorElecCalibData electrical;
    electrical.iu_calib = calibration_.current.iu_calib;
    electrical.iv_calib = calibration_.current.iv_calib;
    electrical.iw_calib = calibration_.current.iw_calib;
    electrical.vbus_calib = calibration_.bus;
    invalidate_axis();
    rotor_valid_ = joint_valid_ = false;
    sample_.valid = false;
    return feedback_.configure(parameters_, electrical, sensors(), calibration_.joint);
}

bool MotorControl::configure(const MotorParameters& parameters, const CalibrationValues& calibration)
{
    if (configured_ && !idle()) return false;
    configured_ = false;
    parameters_ = parameters;
    if (!std::isfinite(parameters.telemetry_q_current_sign) ||
        std::fabs(parameters.telemetry_q_current_sign) != 1.0F) return false;
    calibration_ = calibration;
    hardware_fault_ = enabled_ = stopping_ = false;
    mode_managed_ = mode_pending_ = holding_ = local_disable_ = false;
    accepted_sequence_ = applied_sequence_ = 0U;
    accepted_enabled_ = false;
    input_us_ = joint_us_ = 0U;
    temperature_ = {};
    temperature_age_s_ = 0;
    for (auto& timing : timing_) timing = {};
    capture_divider_ = 0U;
    capture_size_ = 0U;
    capture_head_ = 0U;
    capture_phase_ = 0U;
    capture_sequence_ = capture_dropped_ = 0U;
    fast_count_ = joint_count_ = checked_fast_count_ = checked_joint_count_ = 0U;
    auto_calibration_.reset(parameters.auto_calibration_enabled);
    if (!configure_feedback() || !current_.configure(parameters.current) ||
        !joint_.configure(parameters.joint) || !calibrator_.configure(parameters.calibration) ||
        !backlash_.configure(parameters.joint.frequency_hz, {}) ||
        !protection_.configure(parameters.temperature, parameters.bus)) return false;
    configured_ = true;
    control_mode_ = static_cast<uint8_t>(parameters.joint.default_mode);
    return configure_interaction({});
}

bool MotorControl::motor_operation_active() const
{
    const auto state = calibrator_.state();
    return diagnostics_.active() || backlash_.active() || state == CalibrationState::Collecting ||
           state == CalibrationState::PendingPersistence;
}

bool MotorControl::exclusive_available() const
{
    return configured_ && !enabled_ && !motor_operation_active() &&
           !lut_.active() && !lut_.pending_persistence();
}

bool MotorControl::idle() const { return exclusive_available(); }

MotorStatus MotorControl::status() const
{
    MotorStatus result;
    result.enabled = enabled_;
    result.hardware_fault = hardware_fault_;
    result.protection_mask = protection_.fault_mask() | (limits_.state().faults << 6U);
    result.ready = ready();
    result.position_rad = feedback_.joint().multi_turn_angle_filted;
    result.velocity_rad_s = feedback_.velocity().output_angular_velocity;
    result.current_a = parameters_.telemetry_q_current_sign * current_output_.current_a.q;
    result.bus_voltage_v = feedback_.electrical().vbus_calib;
    result.calibration = calibrator_.state();
    result.calibration_kind = calibrator_.kind();
    result.calibration_samples = calibrator_.sample_count();
    result.calibration_motions = calibrator_.motion_count();
    result.diagnostic_active = diagnostics_.active();
    result.auto_calibration_failed = auto_calibration_.failed();
    result.backlash = backlash_.result();
    result.rl = diagnostics_.rl_result();
    result.sweep = diagnostics_.sweep_result();
    result.dropped_rl = diagnostics_.dropped_rl_bursts();
    result.dropped_sweep = diagnostics_.dropped_sweep_bursts();
    return result;
}

bool MotorControl::ready() const
{
    return configured_ && !hardware_fault_ && !protection_.active() && !limits_.state().faults &&
           rotor_valid_ && joint_valid_ && calibration_.input.valid && calibration_.electrical.valid &&
           calibration_.output.tmr1_calib.valid && calibration_.output.tmr2_calib.valid;
}

void MotorControl::reset_control()
{
    q_target_a_ = 0.0F;
    current_.reset();
    joint_.reset(feedback_.loop_feedback());
    interaction_.stop();
    interaction_snapshot_.energize = false;
    (void)limits_.apply(0, 0, 0, 0, false);
}

bool MotorControl::confirm_axis(float known_position, int direction, uint32_t now, bool unloaded)
{
    if (!idle()) return false;
    invalidate_axis();
    const auto& config = interaction_config_;
    if (first_fault_ || !ready() || !calibration_.joint.joint_zero_angle.valid ||
        direction != parameters_.joint.invert_sign || !config.execution.position_limits ||
        !config.brake.valid(config.execution.continuous_current_a) ||
        !std::isfinite(known_position) || now - joint_us_ > 1000U ||
        std::fabs(feedback_.joint().angular_velocity) > config.brake.still_rad_s ||
        known_position < config.execution.minimum_position_rad ||
        known_position > config.execution.maximum_position_rad) return false;
    support_current_a_ = config.brake.hold_a;
    axis_confirmed_ = feedback_.recover_turn(known_position, config.brake.travel_rad);
    if (axis_confirmed_ && unloaded) {
        feedback_.align_unloaded(config.joint.elastic.zero_rad);
        elastic_referenced_ = true;
    }
    return axis_confirmed_;
}

bool MotorControl::begin_handoff(uint32_t now)
{
    if (stop_reason_ || !axis_confirmed_ || brake_.phase() != BrakePhase::Locked || !enable(true)) return false;
    brake_.start(feedback_.joint().multi_turn_angle_filted, now);
    contact_time_s_ = 0;
    network_applied_ = false;
    return true;
}

void MotorControl::controlled_stop(uint32_t now)
{
    network_pending_ = false;
    if (!enabled_ || brake_.phase() == BrakePhase::Locked) { stop(true); return; }
    const auto& joint = feedback_.joint();
    brake_.set_stop_timeout(joint.angular_velocity, interaction_config_.brake);
    brake_.stop(joint.multi_turn_angle_filted, joint.angular_velocity, now);
}

bool MotorControl::enable(bool value)
{
    if (!value) { stop(); return true; }
    if (mode_managed_) return false;
    if (!exclusive_available() || !ready()) return false;
    reset_control();
    stopping_ = false;
    enabled_ = true;
    limits_.touch_command();
    return true;
}

bool MotorControl::select_mode(JointTargetMode mode)
{
    if (!configured_ || mode_managed_ || motor_operation_active() || lut_.active() || lut_.pending_persistence()) return false;
    if (!joint_.select_mode(mode, feedback_.loop_feedback()))
        return false;
    control_mode_ = static_cast<uint8_t>(mode);
    return true;
}

bool MotorControl::submit(const JointTargetOrder& order)
{
    const auto previous = joint_.trace();
    if (!configured_ || mode_managed_ || motor_operation_active() || lut_.active() ||
        lut_.pending_persistence() || !joint_.submit(order))
        return false;
    if (!previous.target_valid || int32_t(order.host_timestamp_us - previous.host_timestamp_us) > 0)
        limits_.touch_command();
    return true;
}

CurrentLoopOutput MotorControl::current_sample(const float (&voltages)[6], uint32_t timestamp_us)
{
    if (!configured_) return {};
    input_us_ = timestamp_us;
    ++fast_count_;
    // ADC scan order: Iw, Iv, Iu, Vbus, rotor TMR1, rotor TMR2.
    if (!feedback_.electrical_sample(voltages[2], voltages[1], voltages[0], voltages[3])) {
        fault(); return {};
    }
    rotor_valid_ = feedback_.rotor_sample(voltages[4], voltages[5]);
    if (!rotor_valid_ && (enabled_ || backlash_.active())) { fault(); return {}; }
    sample_.rotor_tmr1 = voltages[4];
    sample_.rotor_tmr2 = voltages[5];
    sample_.raw_electrical_angle_rad = feedback_.rotor().elec_angle;
    sample_.valid = std::isfinite(voltages[4]) && std::isfinite(voltages[5]);
    const auto& electrical = feedback_.electrical();
    CurrentLoopInput input;
    input.current_a = {electrical.iu_filter, electrical.iv_filter, electrical.iw_filter};
    input.bus_voltage_v = electrical.vbus_filter;
    input.measured_electrical_angle_rad = rotor_valid_ ? feedback_.rotor().elec_angle_calib : 0.0F;
    input.electrical_velocity_rad_s =
        feedback_.velocity().angular_velocity * parameters_.joint.motor.pole_pairs;
    const auto measured = CurrentLoop::measure(input.current_a, input.measured_electrical_angle_rad);
    const auto raw_ab =
        foc::clarke_transform({electrical.iu_calib, electrical.iv_calib, electrical.iw_calib});
    const float power = 1.5F * (current_output_.voltage_alpha_beta_v.alpha * raw_ab.alpha +
                                current_output_.voltage_alpha_beta_v.beta * raw_ab.beta);
    limits_.current_sample(electrical.iu_calib, electrical.iv_calib, electrical.iw_calib, measured.q, power,
                           enabled_ || motor_operation_active());
    if (limits_.state().faults)
    {
        if (!hardware_fault_)
            fault();
        return {};
    }
    if (diagnostics_.active() && current_output_.energize &&
        diagnostics_.rl_stage() != RlIdentificationStage::Idle &&
        diagnostics_.rl_stage() != RlIdentificationStage::Drain)
        (void)electrical_identifier_.sample(raw_ab.alpha, current_output_.voltage_alpha_beta_v.alpha,
                                            feedback_.velocity().angular_velocity);
    if (!hardware_fault_ && !protection_.active() && !stopping_) {
        if (calibrator_.state() == CalibrationState::Collecting) {
            const auto& drive = calibrator_.drive();
            input.mode = drive.energize ? CurrentLoopMode::OpenLoop : CurrentLoopMode::Disabled;
            input.voltage_v.d = drive.d_voltage_v;
            input.target_open_loop_angle_rad = drive.target_electrical_angle_rad;
        } else if (diagnostics_.active()) {
            const auto raw = foc::clarke_transform(
                {electrical.iu_calib, electrical.iv_calib, electrical.iw_calib});
            const auto drive = diagnostics_.sample(raw.alpha, measured.q, electrical.vbus_calib);
            input.mode = drive.energize ? drive.mode : CurrentLoopMode::Disabled;
            input.target_current_a = drive.target_current_a;
            input.direct_voltage_v = drive.voltage_v;
        } else if ((enabled_ || backlash_.active()) && ready()) {
            input.mode = CurrentLoopMode::Current;
            input.target_current_a.q = q_target_a_;
        }
    }
    current_output_ = current_.update(input, measured);
    sample_.applied_open_loop_angle_rad = current_output_.applied_electrical_angle_rad;
    if (!current_output_.valid && input.mode != CurrentLoopMode::Disabled) fault();
    if (!current_output_.energize) confirm_mode(false);
    return current_output_;
}

bool MotorControl::joint_sample(float tmr1, float tmr2, uint32_t timestamp_us)
{
    if (!configured_) return false;
    joint_us_ = timestamp_us;
    ++joint_count_;
    sample_.output_tmr1 = tmr1;
    sample_.output_tmr2 = tmr2;
    joint_valid_ = feedback_.joint_sample(tmr1, tmr2);
    external_age_s_ += 1 / parameters_.joint.frequency_hz;
    temperature_age_s_ += 1 / parameters_.joint.frequency_hz;
    const bool moving = enabled_ || diagnostics_.active() || backlash_.active() ||
                        calibrator_.state() == CalibrationState::Collecting;
    if (moving && interaction_config_.execution.temperature_timeout_s > 0 &&
        temperature_age_s_ > interaction_config_.execution.temperature_timeout_s)
        limits_.trip(ExecutionFault::TemperatureStale);
    const bool executing = brake_.phase() == BrakePhase::Preload || brake_.phase() == BrakePhase::Releasing ||
                           brake_.phase() == BrakePhase::Running;
    if (executing && timestamp_us - network_received_us_ >= kNetworkTimeoutUs) {
        stop_reason_ = uint32_t(JointFault::NetworkTimeout);
        latch_fault(stop_reason_, timestamp_us);
        controlled_stop(timestamp_us);
    }
    if (executing) {
        const auto& j = interaction_config_.joint;
        const bool needs_external = control_mode_ == 6 || control_mode_ == 7 ||
                                    interaction_config_.brake.collision_nm > 0;
        const bool model = elastic_referenced_ && control_mode_ >= 4 && j.use_model_external && j.elastic.stiffness_nm_rad > 0;
        if (needs_external && !model && (external_flags() & 1U) == 0) {
            stop_reason_ = uint32_t(ExecutionFault::ExternalSignal) << 6U;
            latch_fault(stop_reason_, timestamp_us);
            controlled_stop(timestamp_us);
        }
    }
    const bool handoff = brake_.phase() != BrakePhase::Locked && brake_.phase() != BrakePhase::Running;
    if (handoff) limits_.touch_command();
    const auto trace = joint_.trace();
    limits_.supervise(
        feedback_.joint().multi_turn_angle_filted, feedback_.velocity().output_angular_velocity,
        control_mode_ >= 4 ? interaction_.result().reference_rad - feedback_.joint().multi_turn_angle_filted
                           : trace.position_error_rad,
        enabled_ && !handoff && (control_mode_ == 2 || control_mode_ == 3 || control_mode_ == 5 || control_mode_ == 6 ||
                     control_mode_ == 7 || control_mode_ == 10),
        moving, enabled_, std::max(temperature_.board_temperature_c, temperature_.motor_temperature_c),
        temperature_.valid);
    if (limits_.state().faults)
    {
        fault();
        return false;
    }
    if (!joint_valid_) {
        if (enabled_ || backlash_.active()) fault();
        return false;
    }
    if (handoff && enabled_) {
        const auto& joint = feedback_.joint();
        float request = 0;
        if (!brake_.step(interaction_config_.brake, support_current_a_, timestamp_us, 1 / parameters_.joint.frequency_hz,
                         joint.multi_turn_angle_filted, joint.angular_velocity, current_output_.current_a.q,
                         request)) {
            limits_.trip(ExecutionFault::BrakeHandoff);
            fault();
            return false;
        }
        if (brake_.phase() == BrakePhase::Locked) { stop(true); return true; }
        q_target_a_ = limits_.apply(request, joint.multi_turn_angle_filted, joint.angular_velocity,
            interaction_config_.joint.torque.valid() ? interaction_config_.joint.torque.joint_nm_per_iq_a :
            parameters_.joint.invert_sign, true, interaction_config_.joint.torque.bias_nm);
        joint_.track_applied(q_target_a_);
        return true;
    }
    if (backlash_.active()) {
        const float velocity = backlash_.update(
            feedback_.velocity().output_equivalent_multi_turn_angle_filted_rad,
            feedback_.joint().multi_turn_angle_filted, hardware_fault_ || protection_.active());
        JointTargetOrder order;
        order.velocity_rad_s = velocity;
        order.host_timestamp_us = joint_count_ * 250U;
        (void)joint_.submit(order);
    }
    const bool run = (enabled_ || backlash_.active()) && ready();
    bool valid = false, energize = false, complete = false;
    float request = 0;
    if (control_mode_ >= 4 && run)
        request = interaction_update(valid, energize, complete);
    else
    {
        const auto output = joint_.update(feedback_.loop_feedback(), run);
        valid = output.valid;
        energize = output.energize;
        complete = output.transition_complete;
        request = output.q_current_a;
    }
    q_target_a_ = limits_.apply(
        request, feedback_.joint().multi_turn_angle_filted,
        control_mode_ >= 4 ? feedback_.joint().angular_velocity
                           : feedback_.velocity().output_angular_velocity,
        interaction_config_.joint.torque.valid() ? interaction_config_.joint.torque.joint_nm_per_iq_a
                                                 : parameters_.joint.invert_sign,
        run && valid && energize,
        interaction_config_.joint.torque.valid() ? interaction_config_.joint.torque.bias_nm : 0.0F);
    if (control_mode_ >= 4)
    {
        q_target_a_ = interaction_.limit_applied(q_target_a_, feedback_.joint().angular_velocity);
        if (!std::isfinite(q_target_a_) ||
            std::fabs(q_target_a_) > limits_.state().current_limit_a + 1e-6F)
        {
            limits_.trip(ExecutionFault::PowerBudget);
            fault();
            return false;
        }
        limits_.track_applied(q_target_a_);
        interaction_.track_applied(q_target_a_);
    }
    else
        joint_.track_applied(q_target_a_);
    if (run && !valid)
    {
        limits_.trip(interaction_snapshot_.grip == algo::interaction::GripState::Fault
                         ? ExecutionFault::InteractionController
                         : ExecutionFault::ExternalSignal);
        fault();
    }
    if (run && valid && !energize)
        stop(true);
    if (run && valid && energize && network_pending_ && enabled_) {
        network_sequence_ = pending_network_sequence_;
        network_applied_us_ = timestamp_us;
        applied_delay_us_ = timestamp_us - network_received_us_;
        network_pending_ = false;
        network_applied_ = true;
    }
    const auto& b = interaction_config_.brake;
    if (brake_.phase() == BrakePhase::Running && b.collision_nm > 0) {
        const auto f = interaction_feedback();
        const bool model = elastic_referenced_ && control_mode_ >= 4 && interaction_snapshot_.valid &&
                           interaction_config_.joint.use_model_external &&
                           interaction_config_.joint.elastic.stiffness_nm_rad > 0;
        const float residual = f.external_valid ? f.external_nm : interaction_snapshot_.external_estimate_nm;
        if (!f.external_valid && !model) {
            limits_.trip(ExecutionFault::ExternalSignal);
            fault();
            return false;
        }
        contact_time_s_ = std::fabs(residual) >= b.collision_nm ?
            contact_time_s_ + 1 / parameters_.joint.frequency_hz : 0;
        if (contact_time_s_ * 1000 >= b.collision_ms) {
            stop_reason_ = uint32_t(JointFault::Contact);
            latch_fault(stop_reason_, timestamp_us);
            controlled_stop(timestamp_us);
        }
    }
    confirm_mode(complete);
    capture_pair(timestamp_us);
    return true;
}

void MotorControl::tick_1ms(uint32_t now_ms, const TemperatureSample& temperature)
{
    if (!configured_) return;
    now_ms_ = now_ms;
    temperature_ = temperature;
    temperature_age_s_ = 0;
    protection_.update_temperature(temperature.board_temperature_c,
                                   temperature.motor_temperature_c, temperature.valid);
    protection_.update_bus(feedback_.electrical().vbus_calib, fast_count_ != 0U);
    if (fast_count_ != checked_fast_count_) last_fast_ms_ = now_ms;
    if (joint_count_ != checked_joint_count_) last_joint_ms_ = now_ms;
    checked_fast_count_ = fast_count_;
    checked_joint_count_ = joint_count_;
    const bool moving = enabled_ || diagnostics_.active() || backlash_.active() ||
        calibrator_.state() == CalibrationState::Collecting;
    if (moving && (uint32_t(now_ms-last_fast_ms_) > 2U ||
                   uint32_t(now_ms-last_joint_ms_) > 2U)) fault();
    diagnostics_.tick_1ms(!hardware_fault_ && !protection_.active(), hardware_fault_ || protection_.active());
    if (protection_.active()) {
        latch_fault(protection_.fault_mask(), int32_t(input_us_ - joint_us_) >= 0 ? input_us_ : joint_us_);
        brake_.lock();
        if (mode_managed_) local_disable_ = true;
        enabled_ = false;
        backlash_.cancel(true);
        reset_control();
    }
    sample_.valid = sample_.valid && fast_count_ != 0U && joint_count_ != 0U;
    (void)calibrator_.update(now_ms, sample_, hardware_fault_ || protection_.active());
    if (lut_.expire(now_ms)) output_view_.updating = false;
    const bool successful = calibrator_.state() == CalibrationState::Complete ||
                            calibrator_.state() == CalibrationState::Idle;
    const auto next = auto_calibration_.poll(sensors(), successful, exclusive_available(),
                                            !enabled_, sample_.valid);
    if (next != CalibrationKind::None) (void)start_calibration(next);
}

bool MotorControl::start_calibration(CalibrationKind kind)
{
    if (!exclusive_available() || hardware_fault_ || protection_.active() || !sample_.valid)
        return false;
    if (kind == CalibrationKind::ElectricalAngle && !rotor_valid_) return false;
    stopping_ = false;
    reset_control();
    return calibrator_.start(kind, now_ms_, sensors(), true, true);
}

bool MotorControl::start_auto_calibration()
{
    if (!exclusive_available() || !sample_.valid) return false;
    auto missing = sensors();
    missing.input_sensor_calib.valid = false;
    auto_calibration_.reset(true);
    const auto first = auto_calibration_.poll(missing, true, true, true, true);
    return start_calibration(first);
}

bool MotorControl::set_plan(PositionTargetPlanMode mode)
{
    if (!exclusive_available() || static_cast<unsigned>(mode) > 1U) return false;
    auto candidate = parameters_.joint;
    candidate.loop.position.target_plan_mode = mode;
    if (!joint_.configure(candidate)) return false;
    parameters_.joint = candidate;
    reset_control();
    return true;
}

bool MotorControl::start_rl(bool stream_ready)
{
    if (!exclusive_available() || !sample_.valid) return false;
    stopping_ = false;
    reset_control();
    electrical_identifier_.reset(1 / parameters_.current_frequency_hz);
    return diagnostics_.start_rl({true, true, stream_ready, !hardware_fault_ && !protection_.active()});
}

bool MotorControl::start_sweep(const CurrentSweepConfig& config, bool stream_ready)
{
    if (!exclusive_available() || !status().ready) return false;
    stopping_ = false;
    reset_control();
    return diagnostics_.start_sweep(config, parameters_.current_frequency_hz,
        {true, true, stream_ready, !hardware_fault_ && !protection_.active()});
}

bool MotorControl::start_backlash()
{
    if (!exclusive_available() || !status().ready) return false;
    stopping_ = false;
    reset_control();
    return joint_.select_mode(JointTargetMode::Velocity, feedback_.loop_feedback(), false) &&
           backlash_.start(true);
}

void MotorControl::stop(bool emergency)
{
    brake_.lock();
    network_pending_ = false;
    if (mode_managed_) local_disable_ = true;
    enabled_ = false;
    calibrator_.cancel();
    diagnostics_.stop();
    backlash_.cancel(emergency);
    stopping_ = !backlash_.active();
    if (!backlash_.active()) reset_control();
}

void MotorControl::latch_fault(uint32_t reason, uint32_t now)
{
    if (first_fault_) return;
    first_fault_ = reason;
    first_fault_phase_ = brake_.phase();
    first_fault_us_ = now;
}

void MotorControl::fault()
{
    hardware_fault_ = true;
    const uint32_t sample_us = int32_t(input_us_ - joint_us_) >= 0 ? input_us_ : joint_us_;
    latch_fault(mode_status().faults, sample_us);
    stop(true);
    current_output_ = {};
}

bool MotorControl::clear_fault()
{
    if ((limits_.state().faults & uint32_t(ExecutionFault::TemperatureStale)) &&
        (!temperature_.valid || temperature_age_s_ > interaction_config_.execution.temperature_timeout_s))
        return false;
    if (!exclusive_available() || protection_.active() || !limits_.clear())
        return false;
    hardware_fault_ = stopping_ = false;
    stop_reason_ = first_fault_ = 0;
    contact_time_s_ = 0;
    reset_control();
    return true;
}

bool MotorControl::pending_calibration(RecordView& record)
{
    if (calibrator_.state() != CalibrationState::PendingPersistence) return false;
    const auto& candidate = calibrator_.result();
    switch (calibrator_.kind()) {
    case CalibrationKind::InputTmr:
        record = {CalibrationRecordId::InputTmr, &candidate.input_sensor_calib,
                  sizeof(candidate.input_sensor_calib)};
        return true;
    case CalibrationKind::ElectricalAngle:
        record = {CalibrationRecordId::ElectricalAngle, &candidate.elec_angle_calib,
                  sizeof(candidate.elec_angle_calib)};
        return true;
    case CalibrationKind::OutputTmr:
        pending_output_ = {candidate.output_tmr1_calib, candidate.output_tmr2_calib};
        record = {CalibrationRecordId::OutputTmr, &pending_output_, sizeof(pending_output_)};
        return true;
    default: return false;
    }
}

bool MotorControl::finish_calibration(bool verified)
{
    if (!calibrator_.complete_persistence(verified)) return false;
    const auto& result = calibrator_.result();
    calibration_.input = result.input_sensor_calib;
    calibration_.output = {result.output_tmr1_calib, result.output_tmr2_calib};
    calibration_.electrical = result.elec_angle_calib;
    reset_control();
    return configure_feedback();
}

bool MotorControl::joint_reference(bool zero, RecordView& record)
{
    if (!exclusive_available() || !joint_valid_ ||
        !feedback_.joint_reference_candidate(zero, pending_reference_)) return false;
    const auto* payload = zero ? &pending_reference_.joint_zero_angle : &pending_reference_.joint_offset_angle;
    record = {zero ? CalibrationRecordId::JointZero : CalibrationRecordId::JointOffset,
              payload, sizeof(*payload)};
    return true;
}

bool MotorControl::commit_reference(bool zero, bool verified)
{
    if (!verified || !exclusive_available()) return false;
    if (zero) calibration_.joint.joint_zero_angle = pending_reference_.joint_zero_angle;
    else calibration_.joint.joint_offset_angle = pending_reference_.joint_offset_angle;
    return configure_feedback();
}

bool MotorControl::linear_candidate(CalibrationRecordId id, const void* data, size_t size,
                                    RecordView& record)
{
    if (!exclusive_available() || !data) return false;
    if (id == CalibrationRecordId::CurrentLinear && size == sizeof(pending_current_)) {
        std::memcpy(&pending_current_, data, size);
        if (!valid_linear_calibration(pending_current_.iu_calib) ||
            !valid_linear_calibration(pending_current_.iv_calib) ||
            !valid_linear_calibration(pending_current_.iw_calib)) return false;
        record = {id, &pending_current_, size};
        return true;
    }
    if (id == CalibrationRecordId::BusVoltage && size == sizeof(pending_bus_)) {
        std::memcpy(&pending_bus_, data, size);
        if (!valid_linear_calibration(pending_bus_)) return false;
        record = {id, &pending_bus_, size};
        return true;
    }
    return false;
}

bool MotorControl::commit_linear(CalibrationRecordId id, bool verified)
{
    if (!verified || !exclusive_available()) return false;
    if (id == CalibrationRecordId::CurrentLinear) calibration_.current = pending_current_;
    else if (id == CalibrationRecordId::BusVoltage) calibration_.bus = pending_bus_;
    else return false;
    return configure_feedback();
}

bool MotorControl::lut_begin(const AngleLutRequest& request, uint16_t owner)
{
    if (!configured_ || enabled_ || motor_operation_active()) return false;
    if (!lut_.begin(request, owner, now_ms_, true)) return false;
    if (request.table == AngleLutTable::Output && lut_.active()) output_view_.updating = true;
    return true;
}

bool MotorControl::lut_begin_read(const AngleLutRequest& request, uint16_t owner,
                                  const MotorAngleLutData& stored)
{
    if (!configured_ || enabled_ || motor_operation_active()) return false;
    return lut_.begin_read(request, owner, now_ms_, stored, true);
}

bool MotorControl::lut_abort(const AngleLutRequest& request, uint16_t owner)
{
    if (!lut_.abort(request, owner)) return false;
    output_view_.updating = false;
    return true;
}

bool MotorControl::lut_finish(bool verified)
{
    if (!lut_.complete_persistence(verified)) { output_view_.updating = false; return false; }
    if (lut_.apply_output()) { output_lut_ = lut_.data(); invalidate_axis(); }
    output_view_.updating = false;
    return true;
}

bool MotorControl::publish_loaded_lut()
{
    if (!idle()) return false;
    for (float value : output_lut_.angle_offset_rad) if (!std::isfinite(value)) return false;
    return configure_feedback();
}

CalibrationRecordType MotorControl::record_type(CalibrationRecordId id)
{
    using namespace calibration_records;
    constexpr CalibrationRecordType types[] {current_linear, bus_voltage, input_tmr, output_tmr,
        electrical_angle, input_angle_lut, output_angle_lut, joint_zero, joint_offset};
    const auto index = static_cast<unsigned>(id);
    return index < 9U ? types[index] : CalibrationRecordType {};
}

} // namespace app
