#include "control/motor_control.h"
#include "comm/motor_protocol.h"
#include <cassert>
#include <cmath>
#include <limits>

using namespace algo::motor;

static TemperatureSample valid_temperature()
{
    TemperatureSample t;
    t.valid = true;
    t.board_temperature_c = t.motor_temperature_c = 25;
    return t;
}

static app::CalibrationValues valid_calibration()
{
    app::CalibrationValues values;
    values.input.valid = true;
    values.input.transform[0] = values.input.transform[3] = 1.0F;
    values.electrical.valid = true;
    values.output.tmr1_calib = values.output.tmr2_calib = {true, 0.0F, 1.0F};
    return values;
}

static void test_control_and_gates()
{
    MotorParameters parameters;
    assert(motor_parameters(MotorVariant::ProfileA, parameters));
    app::MotorControl control;
    assert(control.configure(parameters, valid_calibration()));
    assert(!control.enable(true));
    float voltages[6] {1.65F, 1.65F, 1.65F, 1.2F, 0.0F, 1.0F};
    assert(!control.current_sample(voltages).energize);
    assert(control.joint_sample(0.0F, 1.0F));
    assert(control.status().ready);
    assert(control.select_mode(JointTargetMode::Torque));
    assert(control.enable(true));
    JointTargetOrder target;
    target.torque_a = 1.0F;
    target.host_timestamp_us = 100U;
    assert(control.submit(target));
    for (unsigned i = 0; i < 100; ++i) {
        assert(control.joint_sample(0.0F, 1.0F));
        const auto output = control.current_sample(voltages);
        assert(output.valid && output.energize);
        assert(std::isfinite(output.voltage_v.q));
    }
    assert(!control.start_rl(true));
    assert(!control.start_backlash());
    control.stop();
    assert(!control.current_sample(voltages).energize);
    assert(control.idle());
    assert(!control.start_rl(false));
    assert(control.start_rl(true));
    assert(!control.enable(true));
    control.tick_1ms(1U, valid_temperature());
    assert(control.current_sample(voltages).energize);
    control.stop();
    assert(!control.current_sample(voltages).energize);
}

static void test_calibration_persistence_and_fault()
{
    MotorParameters parameters;
    assert(motor_parameters(MotorVariant::ProfileA, parameters));
    app::MotorControl control;
    assert(control.configure(parameters, {}));
    float sample[6] {1.65F, 1.65F, 1.65F, 1.2F, 1.6F, 1.7F};
    (void)control.current_sample(sample);
    (void)control.joint_sample(1.0F, 0.0F);
    assert(control.start_calibration(CalibrationKind::InputTmr));
    assert(!control.start_backlash());
    for (unsigned i = 0U; i < 448U; ++i) {
        const float angle = float(i) * basic_math::kTwoPi / 448.0F;
        sample[4] = 1.6F + 0.8F * std::sin(angle);
        sample[5] = 1.7F + 0.6F * std::cos(angle);
        (void)control.current_sample(sample);
        (void)control.joint_sample(1.0F, 0.0F);
        TemperatureSample temperature;
        temperature.valid = true;
        temperature.board_temperature_c = temperature.motor_temperature_c = 25.0F;
        control.tick_1ms((i + 1U) * 10U, temperature);
    }
    app::RecordView record;
    assert(control.pending_calibration(record));
    assert(record.id == app::CalibrationRecordId::InputTmr);
    assert(!control.calibration().input.valid);
    assert(!control.finish_calibration(false));
    assert(!control.calibration().input.valid);
    assert(!control.current_sample(sample).energize);
    control.fault();
    assert(!control.start_calibration(CalibrationKind::InputTmr));
    assert(control.clear_fault());
    assert(control.start_calibration(CalibrationKind::InputTmr));
    control.stop();
    assert(control.status().calibration == CalibrationState::Cancelled);
}

static void test_linear_and_reference_commits()
{
    MotorParameters parameters;
    assert(motor_parameters(MotorVariant::ProfileA, parameters));
    app::MotorControl control;
    assert(control.configure(parameters, valid_calibration()));
    float sample[6] {1.65F, 1.65F, 1.65F, 1.2F, 0.0F, 1.0F};
    (void)control.current_sample(sample);
    assert(control.joint_sample(std::sin(1.0F), std::cos(1.0F)));
    app::RecordView record;
    assert(control.joint_reference(true, record));
    assert(!control.commit_reference(true, false));
    assert(!control.calibration().joint.joint_zero_angle.valid);
    assert(control.commit_reference(true, true));
    assert(std::fabs(control.calibration().joint.joint_zero_angle.offset - 1.0F) < 1e-5F);
    const MotorLinearCalibData bus {true, 0.1F, 1.02F};
    assert(control.linear_candidate(app::CalibrationRecordId::BusVoltage, &bus, sizeof(bus), record));
    assert(!control.commit_linear(app::CalibrationRecordId::BusVoltage, false));
    assert(!control.calibration().bus.valid);
    assert(control.commit_linear(app::CalibrationRecordId::BusVoltage, true));
    assert(control.calibration().bus.gain == 1.02F);
    assert(control.set_plan(PositionTargetPlanMode::SCurve));
    assert(control.parameters().joint.loop.position.target_plan_mode == PositionTargetPlanMode::SCurve);
}

static void test_wire_codecs()
{
    app::MotorResponse response;
    assert(response.u32(0x12345678U));
    assert(app::read_u32(response.data) == 0x12345678U);
    assert(response.real(-1.25F));
    assert(app::read_float(response.data+4) == -1.25F);
    RlIdentificationBurstPacket rl;
    rl.sample_count = 20U;
    rl.current_alpha_ma[0] = -123;
    app::encode_rl(rl, response);
    assert(response.size == 60U && response.data[3] == 20U);
    assert(static_cast<int16_t>(app::read_u16(response.data+20)) == -123);
    CurrentSweepBurstPacket sweep;
    sweep.sample_count = 40U;
    sweep.frequency_millihz = 2000U;
    app::encode_sweep(sweep, response);
    assert(response.size == 104U && app::read_u32(response.data+16) == 2000U);
    app::MotorStatus status;
    status.diagnostic_active = true;
    status.calibration_kind = CalibrationKind::ElectricalAngle;
    status.calibration_samples = 112U;
    app::encode_status(status, response);
    assert(response.size == 49U && (response.data[1] & 8U));
    assert(response.data[40] == 4U && app::read_u32(response.data+41) == 112U);
}

static void test_complete_automatic_pipeline()
{
    MotorParameters parameters;
    assert(motor_parameters(MotorVariant::ProfileA, parameters));
    app::MotorControl control;
    assert(control.configure(parameters, {}));
    float phase = 0.0F;
    auto sample_one_ms = [&] {
        for (unsigned i = 0U; i < 20U; ++i) {
            const float rotor = phase / parameters.motor.pole_pairs;
            float voltage[6] {1.65F, 1.65F, 1.65F, 1.2F,
                1.65F + 0.8F * std::sin(rotor),
                1.65F + 0.6F * std::cos(rotor) + 0.2F * std::sin(rotor)};
            const auto output = control.current_sample(voltage);
            phase = output.applied_electrical_angle_rad;
            if (i % 5U == 4U) {
                const float angle = phase / parameters.motor.pole_pairs / parameters.motor.gear_ratio;
                (void)control.joint_sample(1.65F + 0.7F*std::sin(angle),
                                            1.65F + 0.5F*std::cos(angle));
            }
        }
    };
    sample_one_ms();
    assert(control.start_auto_calibration());
    unsigned persisted = 0U;
    const app::CalibrationRecordId expected[] {app::CalibrationRecordId::InputTmr,
        app::CalibrationRecordId::ElectricalAngle, app::CalibrationRecordId::OutputTmr};
    uint32_t ms = 0U;
    for (; ms < 30000U && persisted < 3U; ++ms) {
        sample_one_ms();
        control.tick_1ms(ms + 1U, valid_temperature());
        app::RecordView record;
        if (control.pending_calibration(record)) {
            assert(record.id == expected[persisted]);
            assert(control.finish_calibration(true));
            ++persisted;
        }
    }
    assert(persisted == 3U);
    sample_one_ms();
    control.tick_1ms(ms + 1U, valid_temperature());
    assert(control.status().ready && !control.status().auto_calibration_failed);
    assert(control.enable(true));
    float invalid[6] {1.65F, 1.65F, 1.65F, 1.2F,
                      std::numeric_limits<float>::quiet_NaN(), 1.0F};
    assert(!control.current_sample(invalid).energize);
    assert(control.status().hardware_fault && !control.status().enabled);
}

static void test_observation_and_capture()
{
    MotorParameters parameters;
    assert(motor_parameters(MotorVariant::ProfileA, parameters));
    app::MotorControl control;
    assert(control.configure(parameters, valid_calibration()));
    float voltage[6] {1.67F, 1.65F, 1.65F, 1.2F, 0.0F, 1.0F};
    (void)control.current_sample(voltage, 0xFFFFFFF0U);
    assert(control.joint_sample(0.0F, 1.0F, 1U));
    const auto sample = control.observation();
    assert(std::fabs(sample.current_a.q) > 0.01F);
    assert(control.status().current_a == -sample.current_a.q);
    assert(sample.q_feedback_a == control.status().current_a);
    assert(std::fabs(sample.electrical.iw_raw + 0.4F) < 1e-4F);
    control.record_timing(0U, 0xFFFFFFF0U, 0x10U);
    control.record_timing(0U, 0x40U, 0x80U);
    auto timing = control.observation().timing[0];
    assert(timing.count == 2U && timing.maximum_us == 64U && timing.overruns == 1U);
    assert(timing.maximum_interval_us == 80U);
    assert(!control.configure_capture(true, 3U));
    assert(control.configure_capture(true, 4U));
    for (unsigned i = 0U; i < 4U; ++i) assert(control.joint_sample(0.0F, 1.0F, 10U+i));
    app::CalibrationPair pair;
    assert(control.peek_capture(pair));
    assert(pair.sequence == 1U && pair.joint_us == 13U);
    assert(pair.input_valid && pair.input_us == 0xFFFFFFF0U);
    control.consume_capture(999U);
    assert(control.peek_capture(pair));
    control.consume_capture(1U);
    assert(!control.peek_capture(pair));
    for (unsigned i = 0U; i < 17U*4U; ++i) assert(control.joint_sample(0.0F, 1.0F, 20U+i));
    for (unsigned i = 0U; i < 16U; ++i) {
        assert(control.peek_capture(pair));
        assert(pair.input_valid);
        control.consume_capture(pair.sequence);
    }
    assert(!control.peek_capture(pair));
    for (unsigned i = 0U; i < 4U; ++i) assert(control.joint_sample(0.0F, 1.0F, 100U+i));
    assert(control.peek_capture(pair) && pair.dropped == 1U && pair.sequence == 19U);
    app::MotorResponse response;
    app::encode_capture(pair, response);
    assert(response.size == 40U && response.data[0] == 2U && response.data[1] == 1U);
    assert(app::read_u32(response.data+4) == 1000000U);
    assert(app::read_u32(response.data+24) == 1U);
    assert(app::encode_observation(0U, sample, response) && response.size == 104U);
    assert(app::encode_observation(1U, sample, response) && response.size == 108U);
    assert(app::encode_observation(2U, sample, response) && response.size == 84U);
    assert(app::encode_observation(3U, sample, response));
    assert(response.size == 48U);
    assert(!app::encode_observation(4U, sample, response));
    assert(control.configure_capture(false, 0U) && !control.peek_capture(pair));
}

static void test_mode_transactions()
{
    MotorParameters parameters;
    assert(motor_parameters(MotorVariant::ProfileA, parameters));
    app::MotorControl control;
    assert(control.configure(parameters, valid_calibration()));
    float voltage[6] {1.65F, 1.65F, 1.65F, 1.2F, 0.0F, 1.0F};
    auto step = [&] {
        (void)control.current_sample(voltage);
        assert(control.joint_sample(0.0F, 1.0F));
    };
    step();
    app::ModeCommand command;
    command.mode = JointTargetMode::Torque;
    command.enabled = true;
    command.sequence = 0xFFFFFFF0U;
    command.target.torque_a = 0.2F;
    assert(control.mode_command(command));
    assert(control.mode_status().busy);
    assert(!control.submit(command.target) && !control.select_mode(JointTargetMode::Position));
    for (unsigned i = 0; i < 100U; ++i) step();
    assert(control.mode_status().applied_sequence == command.sequence && !control.mode_status().busy);
    auto delayed_disable = command;
    delayed_disable.action = app::ModeAction::Disable;
    delayed_disable.enabled = false;
    delayed_disable.sequence--;
    assert(control.mode_command(delayed_disable) && control.status().enabled);
    step();
    assert(control.mode_status().enabled && !control.mode_status().busy);
    command.action = app::ModeAction::Hold;
    command.mode = JointTargetMode::Position;
    command.sequence++;
    assert(control.mode_command(command) && control.mode_status().holding);
    const auto held_sequence = command.sequence;
    for (unsigned i = 0; i < 100U; ++i) step();
    assert(control.mode_command(command) && !control.mode_status().busy);
    command.sequence++;
    command.action = app::ModeAction::Target;
    assert(!control.mode_command(command));
    command.action = app::ModeAction::Restore;
    command.mode = JointTargetMode::Torque;
    command.sequence = 5U; // Newer across uint32 wrap.
    assert(control.mode_command(command));
    step();
    assert(control.mode_status().busy && control.mode_status().holding);
    auto old = command;
    old.action = app::ModeAction::Hold;
    old.mode = JointTargetMode::Position;
    old.sequence = held_sequence;
    assert(control.mode_command(old));
    assert(control.mode_status().accepted_sequence == 5U);
    control.stop(true); // A local stop cannot acknowledge the pending Restore.
    for (unsigned i = 0; i < 100U; ++i) step();
    assert(control.mode_status().busy && control.mode_status().locally_disabled);
    command.sequence = 6U;
    assert(!control.mode_command(command));
    command.action = app::ModeAction::Disable;
    command.enabled = false;
    assert(control.mode_command(command));
    step();
    assert(!control.mode_status().busy && !control.mode_status().holding);
    assert(!control.mode_status().locally_disabled && control.mode_status().applied_sequence == 6U);
    command.action = app::ModeAction::Target;
    command.enabled = true;
    command.sequence = 7U;
    assert(control.mode_command(command));
    control.fault();
    step();
    assert(control.mode_status().busy && control.mode_status().faults);
    command.action = app::ModeAction::Disable;
    command.enabled = false;
    command.sequence = 8U;
    assert(control.mode_command(command));
    step();
    assert(!control.mode_status().busy && control.mode_status().faults);
    assert(control.clear_fault());
    app::MotorResponse response;
    app::encode_mode_status(control.mode_status(), response);
    assert(response.size == 16U && app::read_u32(response.data+8) == 8U);
    uint8_t wire[32] {1U, 4U, 0U, 0U};
    app::ModeCommand decoded;
    assert(app::decode_mode_command(wire, sizeof(wire), decoded));
    wire[22] = 1U;
    assert(!app::decode_mode_command(wire, sizeof(wire), decoded));
    wire[22] = 0U; wire[1] = 1U;
    assert(!app::decode_mode_command(wire, sizeof(wire), decoded));

    app::MotorControl invalid_sensor;
    auto bad_reference = valid_calibration();
    bad_reference.joint.joint_offset_angle = {true, std::numeric_limits<float>::quiet_NaN()};
    assert(invalid_sensor.configure(parameters, bad_reference));
    (void)invalid_sensor.current_sample(voltage);
    assert(!invalid_sensor.joint_sample(0.0F, 1.0F));
    command.action = app::ModeAction::Disable;
    command.enabled = false;
    command.sequence = 1U;
    assert(invalid_sensor.mode_command(command));
    (void)invalid_sensor.current_sample(voltage);
    assert(!invalid_sensor.mode_status().busy && !invalid_sensor.mode_status().enabled);
}

static void test_calibration_management()
{
    MotorParameters parameters;
    assert(motor_parameters(MotorVariant::ProfileA, parameters));
    app::MotorControl control;
    assert(control.configure(parameters, valid_calibration()));
    app::RecordView record;
    app::MotorResponse response;
    assert(control.calibration_record(app::CalibrationRecordId::InputTmr, record));
    assert(app::encode_calibration(record, 0U, 0U, response));
    assert(response.size == 34U && response.data[2] == 1U);
    assert(app::read_float(response.data+18) == 1.0F);
    assert(!control.erase_record(record.id, false));
    assert(control.calibration().input.valid);
    assert(control.erase_record(record.id, true));
    assert(!control.calibration().input.valid);
    assert(!app::encode_calibration(record, 0U, 121U, response));
    assert(!app::encode_calibration(record, 7U, 1U, response));
    assert(control.calibration_record(app::CalibrationRecordId::ElectricalAngle, record));
    assert(app::encode_calibration(record, 112U, 1U, response) && response.size == 14U);
    assert(control.erase_record(app::CalibrationRecordId::ElectricalAngle, true));
    MotorAngleLutData lut;
    lut.valid = true;
    lut.angle_offset_rad[359] = 0.125F;
    record = {app::CalibrationRecordId::OutputLut, &lut, sizeof(lut)};
    assert(app::encode_calibration(record, 240U, 120U, response) && response.size == 490U);
    assert(app::read_float(response.data+486) == 0.125F);
    record.size--;
    assert(!app::encode_calibration(record, 0U, 120U, response));
}

static void test_interaction_configuration_and_execution()
{
    MotorParameters parameters;
    assert(motor_parameters(MotorVariant::ProfileA, parameters));
    app::MotorControl control;
    assert(control.configure(parameters, valid_calibration()));
    const float voltage[6]{1.65F, 1.65F, 1.65F, 1.2F, 0, 1};
    auto step = [&]
    {
        control.current_sample(voltage);
        return control.joint_sample(0, 1);
    };
    assert(step());
    control.tick_1ms(0, valid_temperature());
    app::ModeCommand command;
    command.enabled = true;
    command.sequence = 1;
    command.mode = static_cast<JointTargetMode>(5);
    command.physical.position_rad = .1F;
    command.physical.stiffness = 5;
    assert(!control.mode_command(command)); // physical units cannot silently use an unknown Kt
    auto config = control.interaction_config();
    config.joint.torque = {2, 0};
    config.joint.use_energy_tank = false;
    config.joint.servo_stiffness = 10;
    config.joint.servo_damping = 1;
    config.joint.force.lever_arm_m = .02F;
    config.cartesian.length_m = .1F;
    config.cartesian.controller.stiffness.fill(10);
    config.cartesian.controller.damping.fill(1);
    assert(control.configure_interaction(config));
    uint8_t wire[1444]{};
    const auto count = app::pack_interaction_config(config, wire, sizeof(wire));
    assert(count && count <= sizeof(wire));
    app::InteractionConfig loaded;
    assert(app::unpack_interaction_config(wire, count, loaded));
    assert(loaded.joint.torque.joint_nm_per_iq_a == 2);
    assert(control.validate_interaction(loaded));
    assert(!app::unpack_interaction_config(wire, count - 1, loaded));
    wire[5] = 1;
    assert(!app::unpack_interaction_config(wire, count, loaded));
    app::MotorResponse section;
    assert(app::encode_interaction_section(config, 1, section));
    section.data[4 + 11 * 4] = 2;
    section.data[4 + 11 * 4 + 1] = 0;
    section.data[4 + 11 * 4 + 2] = 0;
    section.data[4 + 11 * 4 + 3] = 0;
    assert(!app::decode_interaction_section(section.data, section.size, loaded));
    assert(control.mode_command(command));
    for (unsigned i = 0; i < 100; ++i)
        assert(step());
    assert(control.mode_status().applied_sequence == 1);
    assert(control.observation().q_target_a > 0 && control.observation().q_target_a < .3F);
    assert(!control.configure_interaction(config));
    command.mode = JointTargetMode::Torque;
    command.target.torque_a = .1F;
    command.target.host_timestamp_us = 1;
    command.sequence = 2;
    assert(control.mode_command(command));
    assert(control.mode_status().busy);
    for (unsigned i = 0; i < 100; ++i)
        assert(step());
    assert(control.mode_status().applied_sequence == 2);
    control.stop(true);
    command.action = app::ModeAction::Disable;
    command.enabled = false;
    command.sequence = 3;
    assert(control.mode_command(command));
    assert(step());
    config.execution.command_timeout_s = .01F;
    assert(control.configure_interaction(config));
    command.action = app::ModeAction::Target;
    command.enabled = true;
    command.mode = static_cast<JointTargetMode>(4);
    command.sequence = 4;
    command.physical.torque_nm = .1F;
    assert(control.mode_command(command));
    for (unsigned i = 0; i < 30; ++i)
        assert(step());
    assert(control.mode_command(command)); // duplicate is acknowledged but cannot renew the watchdog
    for (unsigned i = 0; i < 20; ++i)
        (void)step();
    assert(control.execution_state().faults & uint32_t(ExecutionFault::CommandStale));
    assert(!control.status().enabled);
    assert(control.clear_fault());
    command.action = app::ModeAction::Disable;
    command.enabled = false;
    command.sequence = 5;
    assert(control.mode_command(command));
    assert(step());
    config.execution.command_timeout_s = .5F;
    config.joint.external_timeout_s = .002F;
    assert(control.configure_interaction(config));
    const float signal[9]{0, 0, .3F};
    assert(control.external_feedback(1, 4, signal));
    command.action = app::ModeAction::Target;
    command.enabled = true;
    command.mode = static_cast<JointTargetMode>(8);
    command.physical.force_n = .3F;
    command.sequence = 6;
    assert(control.mode_command(command));
    for (unsigned i = 0; i < 5; ++i)
        assert(step());
    assert(control.external_feedback(1, 4, signal));
    for (unsigned i = 0; i < 10; ++i)
        (void)step();
    assert(control.execution_state().faults & uint32_t(ExecutionFault::ExternalSignal));
    assert(!control.status().enabled);
    uint8_t physical[40]{2, 0, 4, 1};
    app::ModeCommand decoded;
    assert(app::decode_mode_command(physical, 40, decoded));
    physical[36] = 1;
    assert(!app::decode_mode_command(physical, 40, decoded));
}

static void test_loaded_brake()
{
    app::BrakeControl brake;
    const app::BrakeConfig c{1, .1F, 2, 3, 4, .05F, .01F, 2, 3, .5F, 0, 0};
    float iq = 0;
    brake.start(1, 0);
    assert(brake.step(c, 1, 10000, .00025F, 1, 0, 0, iq));
    assert(iq == 1 && brake.phase() == app::BrakePhase::Preload);
    assert(brake.step(c, 1, 11000, .00025F, 1, 0, 1, iq));
    assert(brake.step(c, 1, 13000, .00025F, 1, 0, 1, iq));
    assert(brake.phase() == app::BrakePhase::Releasing);
    assert(!brake.step(c, 1, 13250, .00025F, 1.1F, 0, 1, iq)); // excessive drift
    brake.start(1, 0);
    assert(!brake.step(c, 1, 2001000, .00025F, 1, 0, 0, iq)); // preload never established
}

static void test_md007_handoff()
{
    MotorParameters p;
    assert(motor_parameters(MotorVariant::ProfileF, p));
    app::MotorControl motor;
    auto calibration = valid_calibration();
    calibration.joint.joint_zero_angle = {true, 0};
    assert(motor.configure(p, calibration));
    auto config = motor.interaction_config();
    config.brake = {0, .2F, 2, 3, 4, .1F, .1F, 1, 1, .1F, 0, 0};
    config.execution.position_limits = true;
    config.execution.minimum_position_rad = -8;
    config.execution.maximum_position_rad = 8;
    config.joint.torque = {2, 0};
    config.joint.servo_stiffness = 2;
    assert(motor.configure_interaction(config));
    const float voltage[6]{1.65F, 1.65F, 1.65F, 1.2F, 0, 1};
    uint32_t now = UINT32_MAX - 8000U;
    auto step = [&] {
        now += 250;
        motor.current_sample(voltage, now);
        assert(motor.joint_sample(0, 1, now));
    };
    step();
    const float known = motor.status().position_rad + basic_math::kTwoPi;
    app::JointDriveTarget cmd;
    cmd.extended = true;
    cmd.mode = app::kImpedanceMode;
    cmd.position_urad = int32_t(known * 1e6F);
    cmd.sequence = 5;
    assert(!motor.apply_network_target(cmd, now));
    assert(!motor.confirm_axis(known, -1, now));
    assert(!motor.confirm_axis(known + 0.5F, 1, now));
    assert(motor.confirm_axis(known, 1, now));
    assert(std::fabs(motor.status().position_rad - known) < .001F);
    assert(motor.apply_network_target(cmd, now));
    for (unsigned i = 0; i < 32; ++i) {
        if (i % 8 == 0) { ++cmd.sequence; assert(motor.apply_network_target(cmd, now)); }
        step();
        if (i < 8) assert(motor.brake_phase() == app::BrakePhase::Preload);
    }
    auto sample = motor.joint_feedback_sample();
    assert(motor.brake_phase() == app::BrakePhase::Running && sample.applied_valid);
    assert(sample.applied_sequence == cmd.sequence && sample.apply_delay_us <= 250);
    assert(sample.effort_flags == 1);
    for (unsigned i = 0; i < 60; ++i) step();
    assert(motor.brake_phase() == app::BrakePhase::Locked && motor.idle());
    assert(motor.joint_feedback_sample().faults & (1U << 31));
    assert(!motor.apply_network_target(cmd, now));
    assert(motor.clear_fault());
    cmd.mode = app::kAdmittanceMode;
    assert(!motor.apply_network_target(cmd, now)); // no external torque measurement
    assert(motor.idle() && motor.brake_phase() == app::BrakePhase::Locked);
    for (unsigned i = 0; i < 24; ++i) step();
    cmd.external_valid = true;
    cmd.external_sequence = 1;
    assert(motor.apply_network_target(cmd, now));
    assert(motor.joint_feedback_sample().effort_flags & 2U);
    motor.controlled_stop(now);
    for (unsigned i = 0; i < 24; ++i) step();
    assert(motor.idle());
    assert(motor.configure_interaction(config));
    assert(!motor.axis_confirmed());
    config.joint.use_model_external = true;
    config.joint.elastic.stiffness_nm_rad = 1;
    assert(motor.configure_interaction(config));
    assert(motor.confirm_axis(known, 1, now));
    cmd.external_valid = false;
    assert(!motor.apply_network_target(cmd, now));
    assert(motor.confirm_axis(known, 1, now, true));
    assert(motor.apply_network_target(cmd, now));
    motor.stop(true);
    calibration.joint.joint_zero_angle.valid = false;
    assert(motor.configure(p, calibration));
    assert(motor.configure_interaction(config));
    step();
    assert(!motor.confirm_axis(motor.status().position_rad, 1, now));
}

int main()
{
    test_loaded_brake();
    test_md007_handoff();
    test_control_and_gates();
    test_calibration_persistence_and_fault();
    test_linear_and_reference_commits();
    test_wire_codecs();
    test_complete_automatic_pipeline();
    test_observation_and_capture();
    test_mode_transactions();
    test_calibration_management();
    test_interaction_configuration_and_execution();
}
