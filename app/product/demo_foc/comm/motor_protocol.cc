#include "comm/motor_protocol.h"
#include <cstring>
#include <cmath>

namespace app {
bool MotorResponse::byte(uint8_t value)
{
    if (size == sizeof(data)) return false;
    data[size++] = value;
    return true;
}
bool MotorResponse::u16(uint16_t value) { return byte(uint8_t(value)) && byte(uint8_t(value >> 8U)); }

bool MotorResponse::u32(uint32_t value) { return u16(uint16_t(value)) && u16(uint16_t(value >> 16U)); }

bool MotorResponse::real(float value)
{
    uint32_t bits;
    std::memcpy(&bits, &value, sizeof(bits));
    return u32(bits);
}
uint16_t read_u16(const uint8_t* bytes) { return algo::motor::lut_read_u16(bytes); }
uint32_t read_u32(const uint8_t* bytes) { return algo::motor::lut_read_u32(bytes); }
float read_float(const uint8_t* bytes)
{
    const uint32_t bits = read_u32(bytes);
    float value;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

void encode_status(const MotorStatus& status, MotorResponse& response)
{
    response = {};
    (void)response.byte(1U);
    (void)response.byte(uint8_t(status.enabled) | uint8_t(status.ready << 1U) |
                        uint8_t(status.hardware_fault << 2U) | uint8_t(status.diagnostic_active << 3U) |
                        uint8_t(status.backlash.active << 4U) | uint8_t(status.backlash.finished << 5U) |
                        uint8_t(status.backlash.result_valid << 6U) | uint8_t(status.auto_calibration_failed << 7U));
    (void)response.byte(static_cast<uint8_t>(status.calibration));
    (void)response.byte(status.backlash.ret);
    (void)response.u32(status.protection_mask);
    (void)response.real(status.position_rad);
    (void)response.real(status.velocity_rad_s);
    (void)response.real(status.current_a);
    (void)response.real(status.bus_voltage_v);
    (void)response.real(status.backlash.result_rad);
    (void)response.byte(status.rl.finished);
    (void)response.byte(status.rl.ret);
    (void)response.byte(status.sweep.finished);
    (void)response.byte(status.sweep.ret);
    (void)response.u32(status.dropped_rl);
    (void)response.u32(status.dropped_sweep);
    (void)response.byte(static_cast<uint8_t>(status.calibration_kind));
    (void)response.u32(status.calibration_samples);
    (void)response.u32(status.calibration_motions);
}

void encode_rl(const algo::motor::RlIdentificationBurstPacket& packet, MotorResponse& response)
{
    response = {};
    (void)response.byte(packet.version);
    (void)response.byte(static_cast<uint8_t>(packet.stage));
    (void)response.byte(packet.flags);
    (void)response.byte(packet.sample_count);
    (void)response.u16(packet.session_id);
    (void)response.u16(packet.packet_seq);
    (void)response.u32(packet.first_sample_seq);
    (void)response.u32(packet.first_timestamp_us);
    (void)response.u16(static_cast<uint16_t>(packet.applied_voltage_mv));
    (void)response.u16(packet.vbus_mv);
    for (size_t i = 0; i < packet.sample_count && i < algo::motor::kRlIdentificationSamplesPerBurst; ++i)
        (void)response.u16(static_cast<uint16_t>(packet.current_alpha_ma[i]));
}

void encode_sweep(const algo::motor::CurrentSweepBurstPacket& packet, MotorResponse& response)
{
    response = {};
    (void)response.byte(packet.version);
    (void)response.byte(static_cast<uint8_t>(packet.stage));
    (void)response.byte(packet.flags);
    (void)response.byte(packet.sample_count);
    (void)response.u16(packet.session_id);
    (void)response.u16(packet.packet_seq);
    (void)response.u32(packet.first_sample_seq);
    (void)response.u32(packet.first_frequency_sample_index);
    (void)response.u32(packet.frequency_millihz);
    (void)response.u16(static_cast<uint16_t>(packet.q_bias_current_ma));
    (void)response.u16(packet.amplitude_ma);
    for (size_t i = 0; i < packet.sample_count && i < algo::motor::kCurrentSweepSamplesPerBurst; ++i)
        (void)response.u16(static_cast<uint16_t>(packet.feedback_current_ma[i]));
}

bool decode_mode_command(const uint8_t* data, size_t size, ModeCommand& command)
{
    if (!data || size < 8 || data[1] > 4 || data[3] > 1)
        return false;
    if (data[0] == 2U)
    {
        const bool task = data[2] >= 11U && data[2] <= 13U;
        if (data[2] < 4U || data[2] > 14U || size != (task ? 88U : 40U) ||
            data[1] == unsigned(ModeAction::Hold) || data[1] == unsigned(ModeAction::SelectMode) ||
            (data[1] == unsigned(ModeAction::Disable) && data[3]) || read_u32(data + size - 4))
            return false;
        ModeCommand c;
        c.action = static_cast<ModeAction>(data[1]);
        c.mode = static_cast<algo::motor::JointTargetMode>(data[2]);
        c.enabled = data[3];
        c.sequence = read_u32(data + 4);
        if (task)
        {
            for (unsigned i = 0; i < 6; ++i)
            {
                c.task_pose[i] = read_float(data + 8 + i * 4);
                c.task_velocity[i] = read_float(data + 32 + i * 4);
                c.task_wrench[i] = read_float(data + 56 + i * 4);
                if (!std::isfinite(c.task_pose[i]) || !std::isfinite(c.task_velocity[i]) ||
                    !std::isfinite(c.task_wrench[i]))
                    return false;
            }
        }
        else
        {
            c.physical = {read_float(data + 8),  read_float(data + 12), read_float(data + 16),
                          read_float(data + 20), read_float(data + 24), read_float(data + 28)};
            const auto &p = c.physical;
            if (!std::isfinite(p.position_rad) || !std::isfinite(p.velocity_rad_s) ||
                !std::isfinite(p.torque_nm) || !std::isfinite(p.stiffness) || p.stiffness < 0 ||
                !std::isfinite(p.damping) || p.damping < 0 || !std::isfinite(p.force_n) || p.force_n < 0)
                return false;
        }
        command = c;
        return true;
    }
    if (size != 32U || data[0] != 1U || data[2] > 3U || data[22] || data[23] || read_u32(data + 28))
        return false;
    ModeCommand candidate;
    candidate.action = static_cast<ModeAction>(data[1]);
    candidate.mode = static_cast<algo::motor::JointTargetMode>(data[2]);
    candidate.enabled = data[3] != 0U;
    candidate.sequence = read_u32(data+4);
    candidate.target = {read_float(data+8), read_float(data+12), read_float(data+16),
                        data[20], data[21], read_u32(data+24)};
    if (!std::isfinite(candidate.target.position_rad) ||
        !std::isfinite(candidate.target.velocity_rad_s) ||
        !std::isfinite(candidate.target.torque_a) ||
        (candidate.action == ModeAction::Hold &&
         (candidate.mode != algo::motor::JointTargetMode::Position || !candidate.enabled)) ||
        (candidate.action == ModeAction::Disable && candidate.enabled)) return false;
    command = candidate;
    return true;
}

void encode_mode_status(const ModeStatus& status, MotorResponse& response)
{
    response = {};
    (void)response.byte(unsigned(status.mode) > 3U ? 2U : 1U);
    (void)response.byte(static_cast<uint8_t>(status.mode));
    (void)response.byte(uint8_t(status.enabled) | uint8_t(status.holding << 1U) |
                        uint8_t(status.busy << 2U) | uint8_t(status.locally_disabled << 3U));
    (void)response.byte(0U);
    (void)response.u32(status.accepted_sequence);
    (void)response.u32(status.applied_sequence);
    (void)response.u32(status.faults);
}

bool encode_calibration(const RecordView& record, uint16_t offset, uint16_t count,
                        MotorResponse& response)
{
    using namespace algo::motor;
    if (!record.payload) return false;
    uint16_t total = 0U;
    uint8_t valid = 0U;
    size_t expected = 0U;
    switch (record.id) {
    case CalibrationRecordId::CurrentLinear: total = 6U; expected = sizeof(MotorCurrentLinearCalibData); break;
    case CalibrationRecordId::BusVoltage: total = 2U; expected = sizeof(MotorLinearCalibData); break;
    case CalibrationRecordId::InputTmr: total = 6U; expected = sizeof(MotorAffine2DCalibData); break;
    case CalibrationRecordId::OutputTmr: total = 4U; expected = sizeof(MotorTmrPairCalibData); break;
    case CalibrationRecordId::ElectricalAngle: total = 113U; expected = sizeof(MotorElecAngleCalibData); break;
    case CalibrationRecordId::InputLut:
    case CalibrationRecordId::OutputLut: total = 360U; expected = sizeof(MotorAngleLutData); break;
    case CalibrationRecordId::JointZero:
    case CalibrationRecordId::JointOffset: total = 1U; expected = sizeof(JointAngleCalibData); break;
    default: return false;
    }
    if (record.size != expected || offset > total || count > 120U) return false;
    if (!count) count = uint16_t(unsigned(total-offset) > 120U ? 120U : unsigned(total-offset));
    if (count > total-offset) return false;
    // Float fields have explicit wire order; native padding never leaves the MCU.
    float small[6] {};
    const float* values = small;
    const MotorElecAngleCalibData* electrical = nullptr;
    switch (record.id) {
    case CalibrationRecordId::CurrentLinear: {
        const auto& data = *static_cast<const MotorCurrentLinearCalibData*>(record.payload);
        const MotorLinearCalibData phases[] {data.iu_calib, data.iv_calib, data.iw_calib};
        for (unsigned i = 0; i < 3U; ++i) {
            valid |= uint8_t(phases[i].valid << i);
            small[i*2U] = phases[i].offset; small[i*2U+1U] = phases[i].gain;
        }
        break;
    }
    case CalibrationRecordId::BusVoltage: {
        const auto& data = *static_cast<const MotorLinearCalibData*>(record.payload);
        valid = data.valid; small[0] = data.offset; small[1] = data.gain;
        break;
    }
    case CalibrationRecordId::InputTmr: {
        const auto& data = *static_cast<const MotorAffine2DCalibData*>(record.payload);
        valid = data.valid;
        small[0] = data.center[0]; small[1] = data.center[1];
        for (unsigned i = 0; i < 4U; ++i) small[i+2U] = data.transform[i];
        break;
    }
    case CalibrationRecordId::OutputTmr: {
        const auto& data = *static_cast<const MotorTmrPairCalibData*>(record.payload);
        valid = uint8_t(data.tmr1_calib.valid) | uint8_t(data.tmr2_calib.valid << 1U);
        small[0] = data.tmr1_calib.offset; small[1] = data.tmr1_calib.amp;
        small[2] = data.tmr2_calib.offset; small[3] = data.tmr2_calib.amp;
        break;
    }
    case CalibrationRecordId::ElectricalAngle:
        electrical = static_cast<const MotorElecAngleCalibData*>(record.payload);
        valid = electrical->valid;
        break;
    case CalibrationRecordId::InputLut:
    case CalibrationRecordId::OutputLut: {
        const auto& data = *static_cast<const MotorAngleLutData*>(record.payload);
        valid = data.valid; values = data.angle_offset_rad;
        break;
    }
    case CalibrationRecordId::JointZero:
    case CalibrationRecordId::JointOffset: {
        const auto& data = *static_cast<const JointAngleCalibData*>(record.payload);
        valid = data.valid; small[0] = data.offset;
        break;
    }
    }
    response = {};
    (void)response.byte(1U); (void)response.byte(static_cast<uint8_t>(record.id));
    (void)response.byte(valid); (void)response.byte(0U);
    (void)response.u16(offset); (void)response.u16(count); (void)response.u16(total);
    for (unsigned i = offset; i < unsigned(offset)+count; ++i) {
        const float value = electrical ?
            (i ? electrical->angle_offset_rad[i-1U] : electrical->zero_offset_rad) : values[i];
        if (!response.real(value)) return false;
    }
    return true;
}

bool encode_observation(uint8_t kind, const MotorObservation& sample, MotorResponse& response)
{
    if (kind > 3U)
        return false;
    response = {};
    (void)response.byte(unsigned(sample.joint.mode) > 3U ? 2U : 1U);
    (void)response.byte(kind);
    (void)response.u16(uint16_t(sample.temperature.valid) |
        uint16_t(sample.joint.target_valid << 1U) | uint16_t(sample.joint.waiting << 2U) |
        uint16_t(static_cast<unsigned>(sample.joint.mode) << 8U));
    (void)response.u32(sample.fast_sequence); (void)response.u32(sample.joint_sequence);
    (void)response.u32(sample.input_us); (void)response.u32(sample.joint_us);
    const auto& e = sample.electrical;
    const auto& j = sample.joint;
    if (kind == 0U) {
        const float values[] {e.iu_raw, e.iv_raw, e.iw_raw, e.iu_calib, e.iv_calib, e.iw_calib,
            e.iu_filter, e.iv_filter, e.iw_filter, e.vbus_raw, e.vbus_calib, e.vbus_filter,
            sample.rotor_tmr[0], sample.rotor_tmr[1], sample.output_tmr[0], sample.output_tmr[1],
            sample.input_angle_rad, sample.electrical_angle_rad, sample.output_raw_rad,
            sample.temperature.board_temperature_c, sample.temperature.motor_temperature_c};
        for (float value : values) (void)response.real(value);
    } else if (kind == 1U) {
        (void)response.u32(j.host_timestamp_us);
        const float values[] {j.target.position_cmd_rad, j.target.velocity_cmd_rad_s,
            j.target.torque_cmd_a, j.target.mit_kp_cmd, j.target.mit_kd_cmd,
            j.planned_position_rad, j.planned_velocity_rad_s, j.position_error_rad,
            j.speed_output_a, j.mit_position_a, j.mit_velocity_a,
            sample.q_target_a, sample.q_feedback_a, sample.current_a.d, sample.current_a.q,
            sample.voltage_v.d, sample.voltage_v.q, sample.position_rad, sample.velocity_rad_s,
            sample.motor_velocity_rad_s, j.target_age_s};
        for (float value : values) (void)response.real(value);
    }
    else if (kind == 2U)
    {
        (void)response.u32(1000000U);
        for (const auto& timing : sample.timing) {
            (void)response.u32(timing.count); (void)response.u32(timing.last_us);
            (void)response.u32(timing.maximum_us); (void)response.u32(timing.overruns);
            (void)response.u32(timing.maximum_interval_us);
        }
    }
    else
    {
        response.byte(sample.temperature.present_mask);
        response.byte(sample.temperature.valid_mask);
        response.u16(0);
        for (float x : sample.temperature.voltage_v)
            response.real(x);
        for (float x : sample.temperature.temperature_c)
            response.real(x);
    }
    return true;
}

void encode_capture(const CalibrationPair& pair, MotorResponse& response)
{
    response = {};
    (void)response.byte(1U); (void)response.byte(pair.flags); (void)response.u16(0U);
    (void)response.u32(1000000U);
    (void)response.u32(pair.sequence); (void)response.u32(pair.joint_sequence);
    (void)response.u32(pair.joint_us); (void)response.u32(pair.input_us);
    (void)response.u32(pair.encoder_sequence); (void)response.u32(pair.encoder_processed_us);
    (void)response.u32(pair.encoder_raw_count); (void)response.u32(pair.dropped);
    (void)response.real(pair.input_angle_rad); (void)response.real(pair.output_raw_rad);
    (void)response.real(pair.output_calibrated_rad);
}

} // namespace app
