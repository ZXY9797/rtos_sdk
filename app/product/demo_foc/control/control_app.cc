#include "control/control_app.h"
#include "control/calibration_store.h"
#include "board/board_devices.h"
#include "comm/joint_canopen_fd.h"
#include "comm/motor_protocol.h"

#include <devicetree.h>
#include <init.h>
#include <irq.h>
#include <link/router.h>
#include <log.h>
#include <osal.h>
#include <system/output_safety.h>
#include <system/watchdog.h>
#include <system/clock.h>

#include <atomic>
#include <algorithm>
#include <cstdio>
#include <cstring>

namespace app {
namespace {
using namespace algo::motor;
MotorControl motor;
CalibrationStore calibration_store;
AdcScale current_scale, joint_scale;
osal::MessageQueue commands;
alignas(std::max_align_t) uint8_t queue_storage[osal::MessageQueue::storage_size<MotorRequest, 4U>()] {};
osal::PeriodicThread slow_thread;
alignas(std::max_align_t) uint8_t slow_stack[8192] {};
std::atomic<bool> running {false}, acquiring {false};
uint8_t stream_owner = 0U;
uint8_t telemetry_owner = 0U, capture_owner = 0U, telemetry_kind = 0U;
uint16_t telemetry_period_ms = 20U;
uint32_t telemetry_sent_ms = 0U;
int safety_output = -1;
bool supported_service = false;
MotorAngleLutData read_lut {};
uint8_t interaction_record[CalibrationRecord::maximum_payload_size]{};
InteractionConfig interaction_candidate{};

static_assert(DT_PROP(DT_ALIAS(can0), fd_enabled), "MD007 requires an FD CAN controller");
#if defined(CONFIG_DEMO_FOC_MD007_MOTION)
bool command_joint_drive(JointDriveAction action, const JointDriveTarget& target, uint32_t now_us)
{
    hal::IrqGuard guard;
    if (action == JointDriveAction::Disable) {
        motor.controlled_stop(now_us);
        return true;
    }
    if (action == JointDriveAction::ResetFault)
        return !system_safety::latched() && motor.clear_fault();

    return !supported_service && !system_safety::latched() && acquiring.load(std::memory_order_relaxed) &&
           motor.apply_network_target(target, system_clock::now_us());
}

JointCanopenFd joint_network {CONFIG_DEMO_FOC_CANOPEN_NODE_ID, command_joint_drive};
#else
JointCanopenFd joint_network {CONFIG_DEMO_FOC_CANOPEN_NODE_ID};
#endif
struct FeedbackMailbox {
    uint32_t id;
    uint32_t sent_us{};
    bool pending{};
};
FeedbackMailbox feedback_mailboxes[] {
    {0x180U + CONFIG_DEMO_FOC_CANOPEN_NODE_ID}, {0x280U + CONFIG_DEMO_FOC_CANOPEN_NODE_ID}
};

bool cancel_feedback(hal::Can& can, FeedbackMailbox& box)
{
    if (!box.pending) return true;
    bool requested = false;
    if (can.abort_pending_fd(box.id, &requested) != hal::Status::Ok) return false;
    if (requested) joint_network.record_drop();
    box.pending = false;
    return true;
}

bool send_joint_frame(void* context, const hal::CanFrame& frame)
{
    auto& can = *static_cast<hal::Can*>(context);
    for (auto& box : feedback_mailboxes) {
        if (frame.id != box.id) continue;
        if (!cancel_feedback(can, box) || can.send_fd(frame) != hal::Status::Ok) return false;
        box.pending = true;
        box.sent_us = system_clock::now_us();
        return true;
    }
    return can.send_fd(frame) == hal::Status::Ok;
}

void poll_joint_network()
{
    auto& can = board::main_can().native();
    const uint32_t service_time = system_clock::now_us();
    for (auto& box : feedback_mailboxes) {
        if (box.pending && service_time - box.sent_us >= 1000U) (void)cancel_feedback(can, box);
    }
    JointFeedbackSample health;
    {
        hal::IrqGuard guard;
        health = motor.joint_feedback_sample();
    }
    joint_network.observe(health, service_time, send_joint_frame, &can);
    joint_network.service(service_time, send_joint_frame, &can);
    for (unsigned i = 0; i < 8U; ++i) {
        hal::CanFrame frame;
        const auto status = can.receive(frame);
        if (status == hal::Status::HardwareError) continue;
        if (status != hal::Status::Ok) break;
        JointFeedbackSample sample;
        uint32_t now;
        {
            hal::IrqGuard guard;
            sample = motor.joint_feedback_sample();
            sample.valid = sample.valid && acquiring.load(std::memory_order_relaxed);
            sample.output_enabled = board::output_enabled() && motor.brake_phase() == BrakePhase::Running;
            now = system_clock::now_us();
        }
        joint_network.process(frame, sample, now, send_joint_frame, &can);
    }
}

struct TimingScope {
    unsigned loop;
    uint32_t start_us {system_clock::now_us()};
    ~TimingScope()
    {
        const uint32_t end_us = system_clock::now_us();
        hal::IrqGuard guard;
        motor.record_timing(loop, start_us, end_us);
    }
};

#if defined(CONFIG_APP_WATCHDOG)
system_watchdog::ClientId control_watchdog = system_watchdog::kInvalidClient;
int register_watchdog()
{
    control_watchdog = system_watchdog::register_client("foc", CONFIG_APP_WATCHDOG_TIMEOUT_MS);
    return control_watchdog != system_watchdog::kInvalidClient ? 0 : -1;
}
SYS_INIT(register_watchdog, INITCALL_LEVEL_APPLICATION, 90);
#endif

void current_irq(void*, const uint16_t* raw, size_t count, hal::Status status)
{
    if (!acquiring.load(std::memory_order_acquire)) return;
    TimingScope timing {0U};
    if (status != hal::Status::Ok || count != 6U || !raw || system_safety::latched()) {
        motor.fault();
        board::shutdown();
        return;
    }
    float voltage[6];
    for (size_t i = 0; i < 6U; ++i) voltage[i] = current_scale.voltage(raw[i]);
    const auto& output = motor.current_sample(voltage, timing.start_us);
    if (!board::apply(output, motor.brake_phase(), supported_service)) motor.fault();
}

void joint_irq(void*, const uint16_t* raw, size_t count, hal::Status status)
{
    if (!acquiring.load(std::memory_order_acquire)) return;
    TimingScope timing {1U};
    // Both IRQs share feedback and fault/reset state. Keep one complete joint
    // update atomic; DMA detects a missed current-frame deadline independently.
    hal::IrqGuard guard;
    if (status != hal::Status::Ok || count != 2U || !raw) {
        motor.fault();
        board::shutdown();
        return;
    }
    (void)motor.joint_sample(joint_scale.voltage(raw[0]), joint_scale.voltage(raw[1]), timing.start_us);
}

void pause_acquisition()
{
    hal::IrqGuard guard;
    acquiring.store(false, std::memory_order_release);
    motor.invalidate_axis();
    board::stop_acquisition();
}

bool resume_acquisition()
{
    hal::IrqGuard guard;
    const bool ok = board::start_acquisition(current_irq, joint_irq, nullptr);
    acquiring.store(ok, std::memory_order_release);
    if (!ok) motor.fault();
    return ok;
}

bool persist(const RecordView& record)
{
    pause_acquisition();
    return calibration_store.write(record);
}

bool persist_interaction()
{
    if (!motor.idle() || !motor.validate_interaction(interaction_candidate))
        return false;
    const size_t count =
        pack_interaction_config(interaction_candidate, interaction_record, sizeof(interaction_record));
    if (!count)
        return false;
    pause_acquisition();
    const bool verified = calibration_store.write_interaction(interaction_record, count);
    const bool applied = verified && motor.configure_interaction(interaction_candidate);
    const bool resumed = resume_acquisition();
    return applied && resumed;
}

void persist_calibration()
{
    RecordView record;
    {
        hal::IrqGuard guard;
        if (!motor.pending_calibration(record)) return;
    }
    const bool verified = persist(record);
    const bool activated = motor.finish_calibration(verified);
    const bool resumed = resume_acquisition();
    LOGI("calibration", "record=%u verified=%u active=%u acquisition=%u",
         static_cast<unsigned>(record.id), verified, activated, resumed);
}

bool send_to(uint8_t owner, uint8_t command, const MotorResponse& response)
{
    if (!owner) return false;
    link::SendRequest request;
    request.receiver = owner;
    request.cmd_set = kMotorCommandSet;
    request.cmd_id = command;
    request.data = response.data;
    request.data_len = response.size;
    return link::Router::instance().send(request) >= 0;
}

void drain_diagnostics()
{
    RlIdentificationBurstPacket rl;
    CurrentSweepBurstPacket sweep;
    bool have_rl, have_sweep;
    {
        hal::IrqGuard guard;
        have_rl = motor.diagnostics().peek_rl_identification_burst(stream_owner, &rl);
        have_sweep = motor.diagnostics().peek_current_sweep_burst(stream_owner, &sweep);
    }
    MotorResponse response;
    if (have_rl) {
        encode_rl(rl, response);
        if (send_to(stream_owner, 0x81U, response)) {
            hal::IrqGuard guard;
            motor.diagnostics().consume_rl_identification_burst(rl.packet_seq);
        }
    }
    if (have_sweep) {
        encode_sweep(sweep, response);
        if (send_to(stream_owner, 0x82U, response)) {
            hal::IrqGuard guard;
            motor.diagnostics().consume_current_sweep_burst(sweep.packet_seq);
        }
    }
}

void publish_observations()
{
    MotorResponse response;
    for (unsigned i = 0U; capture_owner && i < 4U; ++i) {
        CalibrationPair pair;
        {
            hal::IrqGuard guard;
            if (!motor.peek_capture(pair)) break;
        }
        encode_capture(pair, response);
        if (!send_to(capture_owner, 0x85U, response)) break;
        hal::IrqGuard guard;
        motor.consume_capture(pair.sequence);
    }
    const uint32_t now = osal::Kernel::uptime_ms();
    if (telemetry_owner && uint32_t(now-telemetry_sent_ms) >= telemetry_period_ms) {
        MotorObservation sample;
        {
            hal::IrqGuard guard;
            sample = motor.observation();
        }
        if (encode_observation(telemetry_kind, sample, response) &&
            send_to(telemetry_owner, 0x84U, response)) telemetry_sent_ms = now;
    }
}

bool execute_calibration_data(const MotorRequest& command, MotorResponse& response)
{
    if (command.size != 6U || command.data[0] > 1U) return false;
    const unsigned id_value = command.data[1];
    const uint16_t offset = read_u16(command.data+2), count = read_u16(command.data+4);
    if (command.data[0] == 1U) {
        if ((id_value > 8U && id_value != 0xFFU) || offset || count) return false;
        {
            hal::IrqGuard guard;
            if (!motor.idle()) return false;
        }
        pause_acquisition();
        bool ok = true;
        const unsigned first = id_value == 0xFFU ? 0U : id_value;
        const unsigned last = id_value == 0xFFU ? 8U : id_value;
        for (unsigned i = first; i <= last; ++i) {
            const auto id = static_cast<CalibrationRecordId>(i);
            if (!motor.erase_record(id, calibration_store.erase(id))) { ok = false; break; }
        }
        const bool resumed = resume_acquisition();
        return ok && resumed;
    }
    if (id_value > 8U) return false;
    const auto id = static_cast<CalibrationRecordId>(id_value);
    RecordView record;
    if (id == CalibrationRecordId::InputLut || id == CalibrationRecordId::OutputLut) {
        read_lut = {};
        // A missing table is returned as invalid, including its default fields.
        (void)calibration_store.read(id, &read_lut, sizeof(read_lut));
        record = {id, &read_lut, sizeof(read_lut)};
    } else if (!motor.calibration_record(id, record)) return false;
    // Calibration records are immutable to IRQ readers; this slow task is their writer.
    return encode_calibration(record, offset, count, response);
}

bool execute_lut(const MotorRequest& command, MotorResponse& response)
{
    AngleLutRequest request;
    if (!decode_angle_lut_request(command.data, command.size, &request)) return false;
    {
        hal::IrqGuard guard;
        const auto state = motor.status();
        if (state.enabled || state.diagnostic_active || state.backlash.active ||
            state.calibration == CalibrationState::Collecting ||
            state.calibration == CalibrationState::PendingPersistence) return false;
    }
    auto& session = motor.lut();
    const auto record_id = request.table == AngleLutTable::Output ?
        CalibrationRecordId::OutputLut : CalibrationRecordId::InputLut;
    const float* read_values = nullptr;
    switch (request.action) {
    case AngleLutAction::Start: {
        hal::IrqGuard guard;
        if (!motor.lut_begin(request, command.owner)) return false;
        break;
    }
    case AngleLutAction::Data:
        if (!session.accept(request, command.owner, osal::Kernel::uptime_ms())) return false;
        break;
    case AngleLutAction::Finish:
        // CRC and bulk copies run outside interrupt masking. The staging table
        // is owned by this task and the active output table is a separate view.
        if (!session.finish(request, command.owner)) return false;
        if (session.pending_persistence()) {
            const bool verified = persist({record_id, &session.data(), sizeof(session.data())});
            const bool published = motor.lut_finish(verified);
            const bool resumed = resume_acquisition();
            if (!published || !resumed) return false;
        }
        break;
    case AngleLutAction::Abort: {
        hal::IrqGuard guard;
        if (!motor.lut_abort(request, command.owner)) return false;
        break;
    }
    case AngleLutAction::ReadBegin:
        if (!calibration_store.read(record_id, &read_lut, sizeof(read_lut))) return false;
        if (!motor.lut_begin_read(request, command.owner, read_lut)) return false;
        break;
    case AngleLutAction::Read:
        read_values = session.read(request, command.owner, osal::Kernel::uptime_ms());
        if (!read_values) return false;
        break;
    }
    response = {};
    (void)response.byte(kAngleLutWireVersion);
    (void)response.byte(static_cast<uint8_t>(request.table));
    (void)response.byte(static_cast<uint8_t>(request.action));
    (void)response.byte(0U);
    (void)response.u32(request.session);
    (void)response.u16(request.index);
    (void)response.u16(read_values ? request.count : 0U);
    (void)response.u32(session.checksum());
    if (read_values) for (size_t i = 0; i < request.count; ++i) (void)response.real(read_values[i]);
    return true;
}

bool execute(const MotorRequest& command, MotorResponse& response)
{
    const auto* data = command.data;
#if !defined(CONFIG_DEMO_FOC_MD007_MOTION)
    switch (command.command) {
    case MotorCommand::Calibrate:
    case MotorCommand::Backlash:
    case MotorCommand::Rl:
    case MotorCommand::Sweep:
        return false;
    default: break;
    }
#endif
    if (!supported_service && (command.command == MotorCommand::Calibrate ||
        command.command == MotorCommand::Backlash || command.command == MotorCommand::Rl ||
        command.command == MotorCommand::Sweep)) return false;
    switch (command.command) {
    case MotorCommand::ConfirmAxis: {
        if ((command.size != 5U && command.size != 6U) || supported_service ||
            (command.size == 6U && data[5] > 1U)) return false;
        hal::IrqGuard guard;
        return motor.confirm_axis(read_float(data), int8_t(data[4]), system_clock::now_us(),
                                  command.size == 6U && data[5] == 1U);
    }
    case MotorCommand::SupportedService: {
        if (command.size != 1U || data[0] > 1U) return false;
        hal::IrqGuard guard;
        if (!motor.idle()) return false;
        supported_service = data[0] != 0;
        motor.invalidate_axis();
        return true;
    }
    case MotorCommand::Status: return command.size == 0U;
    case MotorCommand::Enable: {
        if (command.size != 1U || data[0] != 0U) return false;
#if defined(CONFIG_DEMO_FOC_MD007_MOTION)
        joint_network.inhibit(system_clock::now_us());
#endif
        hal::IrqGuard guard;
        motor.controlled_stop(system_clock::now_us());
        return true;
    }
    case MotorCommand::Mode:
    case MotorCommand::Target:
    case MotorCommand::ModeTransaction:
        return false;
    case MotorCommand::Calibrate: {
        if (command.size != 1U) return false;
        hal::IrqGuard guard;
        if (data[0] == 0U) return motor.start_auto_calibration();
        return motor.start_calibration(static_cast<CalibrationKind>(data[0]));
    }
    case MotorCommand::Stop: {
        if (command.size != 1U || data[0] > 1U) return false;
#if defined(CONFIG_DEMO_FOC_MD007_MOTION)
        joint_network.inhibit(system_clock::now_us());
#endif
        hal::IrqGuard guard;
        if (data[0]) { motor.stop(true); board::shutdown(); }
        else motor.controlled_stop(system_clock::now_us());
        return true;
    }
    case MotorCommand::ClearFault: {
        if (command.size != 0U || system_safety::latched()) return false;
        {
            hal::IrqGuard guard;
            if (!motor.idle()) return false;
        }
        pause_acquisition();
        const bool cleared = motor.clear_fault();
        const bool resumed = resume_acquisition();
        return cleared && resumed;
    }
    case MotorCommand::Backlash: {
        if (command.size != 0U) return false;
        hal::IrqGuard guard;
        return motor.start_backlash();
    }
    case MotorCommand::Stream:
        if (command.size != 1U || data[0] > 1U || !command.owner) return false;
        if (stream_owner && stream_owner != command.owner) return false;
        if (motor.diagnostics().active()) return false;
        if (data[0] && !motor_stream_ready(static_cast<uint8_t>(command.owner))) return false;
        stream_owner = data[0] ? static_cast<uint8_t>(command.owner) : 0U;
        return true;
    case MotorCommand::Rl: {
        if (command.size != 0U || command.owner != stream_owner) return false;
        hal::IrqGuard guard;
        return motor.start_rl(motor_stream_ready(stream_owner));
    }
    case MotorCommand::Sweep: {
        if (command.owner != stream_owner) return false;
        auto config = motor.parameters().sweep;
        if (command.size != 0U && command.size != 25U) return false;
        if (command.size) {
            config.q_bias_current_a = read_float(data);
            config.amplitude_a = read_float(data+4);
            config.start_frequency_hz = read_float(data+8);
            config.end_frequency_hz = read_float(data+12);
            config.step_frequency_hz = read_float(data+16);
            config.repeat_cycles = data[20];
            config.preload_duration_ms = read_u32(data+21);
        }
        hal::IrqGuard guard;
        return motor.start_sweep(config, motor_stream_ready(stream_owner));
    }
    case MotorCommand::Lut: return execute_lut(command, response);
    case MotorCommand::JointZero:
    case MotorCommand::JointOffset: {
        if (command.size != 0U) return false;
        const bool zero = command.command == MotorCommand::JointZero;
        RecordView record;
        {
            hal::IrqGuard guard;
            if (!motor.joint_reference(zero, record)) return false;
        }
        const bool verified = persist(record);
        const bool published = motor.commit_reference(zero, verified);
        const bool resumed = resume_acquisition();
        return published && resumed;
    }
    case MotorCommand::CurrentLinear:
    case MotorCommand::BusLinear: {
        const bool current = command.command == MotorCommand::CurrentLinear;
        if (command.size != (current ? 24U : 8U)) return false;
        MotorCurrentLinearCalibData current_data;
        MotorLinearCalibData bus_data {true, read_float(data), read_float(data+4)};
        if (current) {
            current_data.iu_calib = {true, read_float(data), read_float(data+4)};
            current_data.iv_calib = {true, read_float(data+8), read_float(data+12)};
            current_data.iw_calib = {true, read_float(data+16), read_float(data+20)};
        }
        const auto id = current ? CalibrationRecordId::CurrentLinear : CalibrationRecordId::BusVoltage;
        RecordView record;
        {
            hal::IrqGuard guard;
            if (!motor.linear_candidate(id, current ? static_cast<const void*>(&current_data) : &bus_data,
                current ? sizeof(current_data) : sizeof(bus_data), record)) return false;
        }
        const bool verified = persist(record);
        const bool published = motor.commit_linear(id, verified);
        const bool resumed = resume_acquisition();
        return published && resumed;
    }
    case MotorCommand::Plan: {
        if (command.size != 1U || data[0] > 1U) return false;
        hal::IrqGuard guard;
        return motor.set_plan(static_cast<PositionTargetPlanMode>(data[0]));
    }
    case MotorCommand::CalibrationData: return execute_calibration_data(command, response);
    case MotorCommand::Observe: {
        if (command.size != 1U || data[0] > 3U)
            return false;
        MotorObservation sample;
        {
            hal::IrqGuard guard;
            sample = motor.observation();
        }
        return encode_observation(data[0], sample, response);
    }
    case MotorCommand::ModeStatus: {
        if (command.size) return false;
        hal::IrqGuard guard;
        encode_mode_status(motor.mode_status(), response);
        return true;
    }
    case MotorCommand::Telemetry: {
        if (command.size != 4U || data[0] > 1U || data[1] > 3U ||
            (telemetry_owner && telemetry_owner != command.owner))
            return false;
        const uint16_t period = read_u16(data+2);
        if (data[0] && (!motor_stream_ready(uint8_t(command.owner)) || period < 5U)) return false;
        telemetry_owner = data[0] ? uint8_t(command.owner) : 0U;
        telemetry_kind = data[1]; telemetry_period_ms = period;
        telemetry_sent_ms = osal::Kernel::uptime_ms();
        return true;
    }
    case MotorCommand::Capture: {
        if (command.size != 3U || data[0] > 1U ||
            (capture_owner && capture_owner != command.owner) ||
            (data[0] && !motor_stream_ready(uint8_t(command.owner)))) return false;
        hal::IrqGuard guard;
        if (!motor.configure_capture(data[0] != 0U, read_u16(data+1))) return false;
        capture_owner = data[0] ? uint8_t(command.owner) : 0U;
        return true;
    }
    case MotorCommand::InteractionConfig:
    {
        // Read [0,section]; persistent replacement [1,section-packet...].
        if (command.size < 2 || data[0] > 1)
            return false;
        {
            hal::IrqGuard guard;
            interaction_candidate = motor.interaction_config();
            if (data[0] && !motor.idle())
                return false;
        }
        if (!data[0])
            return command.size == 2 && encode_interaction_section(interaction_candidate, data[1], response);
        return decode_interaction_section(data + 1, command.size - 1, interaction_candidate) &&
               persist_interaction();
    }
    case MotorCommand::ExternalFeedback:
    {
        if (command.size != 48 || data[0] != 1 || data[1] > 15 || data[2] || data[3])
            return false;
        float values[9];
        for (unsigned i = 0; i < 9; ++i)
            values[i] = read_float(data + 12 + i * 4);
        hal::IrqGuard guard;
        return motor.external_feedback(read_u32(data + 4), data[1], values);
    }
    case MotorCommand::InteractionStatus:
    {
        if (command.size)
            return false;
        hal::IrqGuard guard;
        const auto &r = motor.interaction_result();
        const auto &l = motor.execution_state();
        response.byte(1);
        response.byte(uint8_t(r.grip));
        response.byte(uint8_t(r.valid) | uint8_t(r.energize << 1) | uint8_t(l.limited << 2));
        response.byte(0);
        response.u32(l.faults);
        for (float x :
             {r.torque_nm, r.iq_a, r.reference_rad, r.reference_velocity, r.external_estimate_nm,
              r.elastic_effort_nm, r.energy_j, r.stiffness, r.damping, l.applied_current_a, l.current_limit_a,
              l.command_age_s, l.invalid_temperature_s, l.excess_i2t_a2_s, l.electrical_power_w})
            response.real(x);
        // Reserved fields in the diagnostic wire format.
        for (unsigned i = 0; i < 13; ++i) response.u32(0);
        response.u32(motor.external_sequence());
        response.real(motor.external_age_s());
        response.byte(motor.external_flags());
        response.byte(0);
        response.u16(0);
        return true;
    }
    case MotorCommand::ElectricalModel:
    {
        if (command.size != 1 || data[0] > 1)
            return false;
        ElectricalFit fit;
        {
            hal::IrqGuard guard;
            if (motor.diagnostics().active())
                return false;
            fit = motor.electrical_fit();
            const auto session = motor.diagnostics().rl_result();
            fit.valid = fit.valid && session.finished && session.ret == 0;
            interaction_candidate = motor.interaction_config();
        }
        response.byte(1);
        response.byte(fit.valid);
        response.u16(0);
        response.u32(fit.samples);
        for (float x : {fit.resistance_ohm, fit.inductance_h, fit.offset_v, fit.rms_v})
            response.real(x);
        if (!data[0])
            return true;
        if (!fit.valid || !motor.calibration().current.iu_calib.valid ||
            !motor.calibration().current.iv_calib.valid || !motor.calibration().current.iw_calib.valid ||
            !motor.calibration().bus.valid || !motor.status().ready)
            return false;
        interaction_candidate.electrical.resistance = fit.resistance_ohm;
        interaction_candidate.electrical.ld = interaction_candidate.electrical.lq = fit.inductance_h;
        // Scalar locked-rotor regression cannot identify saliency or magnet flux.
        interaction_candidate.electrical.decoupling = false;
        return persist_interaction();
    }
    case MotorCommand::TorqueCalibration:
    {
        if ((command.size != 1 && command.size != 5) || data[0] > 2 || (data[0] == 1) != (command.size == 5))
            return false;
        float rms = 0;
        {
            hal::IrqGuard guard;
            interaction_candidate = motor.interaction_config();
            if (data[0] == 2 && !motor.idle())
                return false;
            if (data[0] != 2 &&
                !motor.torque_calibration(data[0], command.size == 5 ? read_float(data + 1) : 0,
                                          interaction_candidate, rms))
                return false;
        }
        // The fit is read-only and worker-owned; double-precision regression
        // must not extend the IRQ-masked sampling section.
        if (data[0] == 2 && !motor.torque_calibration(2, 0, interaction_candidate, rms))
            return false;
        response.real(interaction_candidate.joint.torque.joint_nm_per_iq_a);
        response.real(interaction_candidate.joint.torque.bias_nm);
        response.real(rms);
        if (data[0] != 2)
            return true;
        if (rms > std::max(0.01F, 0.05F * interaction_candidate.joint.torque_limit_nm))
            return false;
        return persist_interaction();
    }
    }
    return false;
}

void slow_entry(void*, const osal::PeriodicStats& stats)
{
    if (!running.load(std::memory_order_acquire)) return;
    TimingScope timing {2U};
    poll_joint_network();
    static uint32_t previous_missed = 0U;
    const auto temperature = board::temperature(motor.parameters());
    {
        hal::IrqGuard guard;
        const auto state = motor.status();
        if (stats.missed != previous_missed && (state.enabled || state.backlash.active ||
            motor.diagnostics().active() || state.calibration == CalibrationState::Collecting)) motor.fault();
        previous_missed = stats.missed;
        motor.tick_1ms(osal::Kernel::uptime_ms(), temperature);
        if (motor.status().hardware_fault || motor.status().protection_mask) board::shutdown();
    }
    persist_calibration();
    MotorRequest request;
    if (commands.receive(&request, 0U)) {
        MotorResponse response;
        const bool ok = execute(request, response);
        if (request.completion.is_valid()) {
            if (response.size == 0U) {
                MotorStatus state;
                {
                    hal::IrqGuard guard;
                    state = motor.status();
                }
                encode_status(state, response);
            }
            (void)link::Router::instance().complete_request(request.completion, ok ? 0U : 2U,
                                                           response.data, response.size);
        } else {
            LOGI("foc", "command=%u ok=%u", static_cast<unsigned>(request.command), ok);
        }
    }
    drain_diagnostics();
    publish_observations();
#if defined(CONFIG_APP_WATCHDOG)
    if (!system_watchdog::heartbeat(control_watchdog)) {
        hal::IrqGuard guard;
        motor.fault();
        board::shutdown();
    }
#endif
}

} // namespace

bool enqueue_motor_request(const MotorRequest& request)
{
    return running.load(std::memory_order_acquire) && request.size <= sizeof(request.data) &&
           commands.send(&request, 0U);
}

int start_control()
{
    if (running.load(std::memory_order_acquire)) return 0;
    MotorParameters parameters;
    CalibrationValues calibration;
    constexpr auto motor_variant = MotorVariant::ProfileF; // kmotor/k6216
    if (!motor_parameters(motor_variant, parameters) || !calibration_store.mount()) return -1;
    calibration_store.load(calibration);
    if (!parameters.brake_enabled) return -1;
    if (!motor.configure(parameters, calibration)) return -1;
    const int interaction_size =
        calibration_store.read_interaction(interaction_record, sizeof(interaction_record));
    if (interaction_size < 0 ||
        (interaction_size > 0 &&
         (!unpack_interaction_config(interaction_record, size_t(interaction_size), interaction_candidate) ||
          !motor.configure_interaction(interaction_candidate))))
        return -1;
    (void)calibration_store.read(CalibrationRecordId::OutputLut,
                                &motor.output_lut_storage(), sizeof(MotorAngleLutData));
    if (!motor.publish_loaded_lut()) return -1;
    const AdcFormat current_format {DT_PROP(DT_ALIAS(adc_current), resolution), DT_PROP(DT_ALIAS(adc_current), oversampling_ratio),
        DT_PROP(DT_ALIAS(adc_current), oversampling_shift), 3300U};
    const AdcFormat joint_format {DT_PROP(DT_ALIAS(adc_joint), resolution), DT_PROP(DT_ALIAS(adc_joint), oversampling_ratio),
        DT_PROP(DT_ALIAS(adc_joint), oversampling_shift), 3300U};
    if (!current_scale.configure(current_format) || !joint_scale.configure(joint_format) ||
        !commands.create(4U, sizeof(MotorRequest), queue_storage, sizeof(queue_storage))) return -1;
    safety_output = system_safety::register_output(&motor,
                                                   [](void *context)
                                                   {
                                                       hal::IrqGuard guard;
                                                       static_cast<MotorControl *>(context)->fault();
                                                       board::shutdown();
                                                   });
    if (safety_output < 0 || !start_motor_transport()) { stop_control(); return -1; }
    joint_network.reset();
    for (auto& box : feedback_mailboxes) box.pending = false;
    osal::PeriodicThreadConfig config;
    config.name = "foc_slow";
    config.entry = slow_entry;
    config.frequency_hz = 1000U;
    config.priority = 6U;
    config.stack_size_bytes = sizeof(slow_stack);
    config.stack_buffer = slow_stack;
    if (!slow_thread.start(config) || slow_thread.startup() != 0 || !resume_acquisition()) {
        stop_control(); return -1;
    }
    running.store(true, std::memory_order_release);
    LOGI("foc", "MD007 V1 k6216; joint FD node=%u", CONFIG_DEMO_FOC_CANOPEN_NODE_ID);
    return 0;
}

void stop_control()
{
    running.store(false, std::memory_order_release);
    {
        hal::IrqGuard guard;
        motor.stop(true);
        board::shutdown();
    }
    slow_thread.destroy();
    for (auto& box : feedback_mailboxes) (void)cancel_feedback(board::main_can().native(), box);
    pause_acquisition();
    commands.destroy();
    if (safety_output >= 0) { system_safety::unregister_output(safety_output); safety_output = -1; }
    stream_owner = 0U;
    telemetry_owner = capture_owner = 0U;
}

} // namespace app
