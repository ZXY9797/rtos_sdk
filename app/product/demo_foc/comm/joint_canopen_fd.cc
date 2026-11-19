#include "comm/joint_canopen_fd.h"

#include <cmath>
#include <algorithm>
#include <cstring>

namespace app {
namespace {

constexpr uint32_t kSyncCobId = 0x080U;
constexpr uint32_t kHeartbeatCobId = 0x700U;
constexpr uint32_t kSyncPeriodUs = 2000U;
constexpr uint32_t kMaxSampleAgeUs = 1000U;
constexpr uint32_t kHeartbeatUs = 100000U;
constexpr uint32_t kUsdoRequestBase = 0x600U;
constexpr uint32_t kUsdoResponseBase = 0x580U;
constexpr uint32_t kDriveCobId = 0x200U;

struct Mapping {
    uint16_t index;
    uint8_t sub;
    uint8_t type;
    uint8_t size;
};

// Vendor records are specified in wire order. Each type encodes its exact PDO width.
constexpr uint8_t kCommandTypes[]{6, 2, 5, 7, 7, 4, 4, 4, 7, 7, 4, 7, 6, 6, 5, 5, 3};
constexpr uint8_t kDiagnosticTypes[]{7, 7, 7, 7, 7, 7, 7, 5, 5, 5, 5, 4, 4, 7, 7};
constexpr uint8_t kEventTypes[]{7, 7, 5};
uint8_t scalar_size(uint8_t type)
{
    if (type == 2 || type == 5) return 1;
    if (type == 3 || type == 6) return 2;
    return 4;
}
constexpr uint8_t kI32 = 0x04U;
constexpr uint8_t kI8 = 0x02U;
constexpr uint8_t kU8 = 0x05U;
constexpr uint8_t kU16 = 0x06U;
constexpr uint8_t kU32 = 0x07U;
constexpr Mapping kFeedbackMap[] {
    {0x6041, 0, kU16, 2}, {0x6064, 0, kI32, 4}, {0x606C, 0, kI32, 4},
    {0x2000, 1, kI32, 4}, {0x2000, 2, kI32, 4}, {0x2000, 3, kU32, 4},
    {0x2000, 4, kU32, 4}, {0x2000, 5, kU32, 4}, {0x2000, 6, kU8, 1},
    {0x6061, 0, kI8, 1},
};
constexpr unsigned kFeedbackMapCount = sizeof(kFeedbackMap) / sizeof(kFeedbackMap[0]);
constexpr uint32_t kDriveMap[] {0x60400010U, 0x60600008U, 0x607A0020U,
                               0x60FF0020U, 0x60710010U};

hal::CanFrame frame(uint32_t id, uint8_t size)
{
    hal::CanFrame result;
    result.id = id;
    result.size = size;
    result.fd = true;
    result.brs = true;
    return result;
}

void write32(uint8_t* bytes, uint32_t value)
{
    for (unsigned i = 0; i < 4U; ++i)
        bytes[i] = static_cast<uint8_t>(value >> (i * 8U));
}

uint16_t read16(const uint8_t* bytes)
{
    return uint16_t(bytes[0]) | (uint16_t(bytes[1]) << 8U);
}

uint32_t read32(const uint8_t* bytes)
{
    return uint32_t(read16(bytes)) | (uint32_t(read16(bytes + 2)) << 16U);
}

bool scaled(float value, float scale, uint8_t* bytes)
{
    constexpr float lower = -2147483648.0F;
    constexpr float upper = 2147483648.0F;
    const float result = std::round(value * scale);
    if (!std::isfinite(result) || result < lower || result >= upper) return false;
    write32(bytes, static_cast<uint32_t>(static_cast<int32_t>(result)));
    return true;
}

bool pack_feedback(const JointFeedbackSample& sample, uint32_t now_us, uint16_t status,
                   uint8_t mode, uint8_t counter, uint8_t* data)
{
    if (!sample.valid || uint32_t(now_us - sample.sample_us) > kMaxSampleAgeUs) return false;
    data[0] = static_cast<uint8_t>(status);
    if (!scaled(sample.position_rad, 1000000.0F, &data[2]) ||
        !scaled(sample.velocity_rad_s, 1000000.0F, &data[6]) ||
        !scaled(sample.current_a, 1000.0F, &data[10]) ||
        !scaled(sample.bus_voltage_v, 1000.0F, &data[14])) return false;
    write32(&data[18], sample.sample_us);
    write32(&data[22], sample.sequence);
    write32(&data[26], sample.faults);
    data[30] = counter;
    data[31] = mode;
    return true;
}

} // namespace

void JointCanopenFd::reset()
{
    state_ = Nmt::PreOperational;
    boot_pending_ = true;
    seen_sync_ = false;
    sync_counter_ = 0U;
    drive_state_ = first_fault_ ? DriveState::Fault : DriveState::Disabled;
    target_pending_ = previous_fault_reset_ = false;
    std::memset(command_wire_, 0, sizeof(command_wire_));
    extended_session_ = seen_source_ = seen_command_ = false;
    mode_display_ = 0U;
}

uint16_t JointCanopenFd::statusword(const JointFeedbackSample& sample) const
{
    if (sample.faults != 0U) return static_cast<uint16_t>(DriveState::Fault);
    if (drive_state_ == DriveState::Enabled && !sample.output_enabled)
        return static_cast<uint16_t>(DriveState::SwitchedOn);
    return static_cast<uint16_t>(drive_state_);
}

void JointCanopenFd::disable_drive(uint32_t now_us, DriveState next)
{
    if (drive_ && drive_state_ == DriveState::Enabled)
        (void)drive_(JointDriveAction::Disable, target_, now_us);
    drive_state_ = next;
    mode_display_ = 0U;
}

void JointCanopenFd::record_fault(uint32_t reason, uint32_t now)
{
    if (first_fault_ != 0) return;
    first_fault_ = reason;
    first_fault_us_ = now;
    event_pending_ = true;
}

void JointCanopenFd::observe(const JointFeedbackSample& sample, uint32_t now, Send send, void* context)
{
    if (!first_fault_) fault_brake_ = sample.brake_phase;
    if (sample.faults) {
        if (sample.first_fault && !first_fault_) fault_brake_ = sample.fault_phase;
        record_fault(sample.first_fault ? sample.first_fault : sample.faults, sample.first_fault ? sample.fault_us : now);
        inhibit(now);
    }
    if (event_pending_ && !boot_pending_) {
        auto event = frame(0x380U + node_id_, 12U);
        write32(event.data, first_fault_);
        write32(event.data + 4, first_fault_us_);
        event.data[8] = fault_brake_;
        if (send(context, event)) event_pending_ = false;
    }
}

void JointCanopenFd::inhibit(uint32_t now_us)
{
    record_fault(uint32_t(JointFault::LocalInhibit), now_us);
    disable_drive(now_us, DriveState::Fault);
    target_pending_ = false;
}

void JointCanopenFd::receive_drive_target(const hal::CanFrame& frame, uint32_t now_us)
{
    if (!drive_ || state_ != Nmt::Operational) return;
    if (frame.id == 0x300U + node_id_) {
        if (frame.size != 48 || frame.data[3] == 0 || frame.data[3] > 240 ||
            frame.data[44] > 3 || frame.data[45]) return;
        JointDriveTarget t;
        t.extended = true;
        t.controlword = read16(frame.data);
        t.mode = frame.data[2];
        t.sync = frame.data[3];
        t.sequence = read32(frame.data + 4);
        t.source = read32(frame.data + 8);
        t.position_urad = int32_t(read32(frame.data + 12));
        t.velocity_urad_s = int32_t(read32(frame.data + 16));
        t.torque_mnm = int32_t(read32(frame.data + 20));
        if (t.mode == 10U) t.torque_per_mille = t.torque_mnm;
        t.stiffness_milli = read32(frame.data + 24);
        t.damping_milli = read32(frame.data + 28);
        t.external_mnm = int32_t(read32(frame.data + 32));
        t.external_sequence = read32(frame.data + 36);
        t.source_age_ms = read16(frame.data + 40);
        t.external_age_ms = read16(frame.data + 42);
        t.external_valid = (frame.data[44] & 1U) != 0;
        t.support_valid = (frame.data[44] & 2U) != 0;
        t.support_ma = static_cast<int16_t>(read16(frame.data + 46));
        if (seen_command_ && int32_t(t.sequence - last_command_) <= 0) { ++dropped_; return; }
        last_command_ = t.sequence;
        seen_command_ = true;
        std::memcpy(command_wire_, frame.data, sizeof(command_wire_));
        target_ = t;
        target_pending_ = true;
        target_received_us_ = now_us;
        extended_session_ = true;
        return;
    }
    if (extended_session_ || frame.size != 16U ||
        frame.data[13] != 0U || frame.data[14] != 0U || frame.data[15] != 0U) return;
    target_ = {};
    target_.controlword = read16(frame.data);
    target_.mode = frame.data[2];
    target_.position_urad = static_cast<int32_t>(read32(&frame.data[3]));
    target_.velocity_urad_s = static_cast<int32_t>(read32(&frame.data[7]));
    target_.torque_per_mille = static_cast<int16_t>(read16(&frame.data[11]));
    target_received_us_ = now_us;
    target_pending_ = true;
}

void JointCanopenFd::apply_drive_target(const JointFeedbackSample& sample, uint32_t now_us)
{
    if (!target_pending_) return;
    target_pending_ = false;
    if (now_us - target_received_us_ >= kNetworkTimeoutUs ||
        (target_.extended && target_.sync != sync_counter_)) { ++dropped_; return; }
    target_.sync = sync_counter_;

    const bool reset = (target_.controlword & 0x0080U) != 0U;
    const bool reset_edge = reset && !previous_fault_reset_;
    previous_fault_reset_ = reset;
    if (sample.faults != 0U || drive_state_ == DriveState::Fault) {
        disable_drive(now_us, DriveState::Fault);
        if (reset_edge && !event_pending_ && drive_(JointDriveAction::ResetFault, target_, now_us)) {
            drive_state_ = DriveState::Disabled;
            first_fault_ = 0;
            seen_source_ = false;
        }
        return;
    }

    const uint16_t command = target_.controlword & 0x008FU;
    if ((command & 0x0006U) != 0x0006U || (target_.controlword & 0x0100U) != 0U) {
        disable_drive(now_us, DriveState::Disabled);
    } else if (command == 0x0006U) {
        disable_drive(now_us, DriveState::Ready);
    } else if (command == 0x0007U && drive_state_ != DriveState::Disabled) {
        disable_drive(now_us, DriveState::SwitchedOn);
    } else if (command == 0x000FU &&
               (drive_state_ == DriveState::SwitchedOn || drive_state_ == DriveState::Enabled)) {
        if (target_.extended) {
            if (!seen_source_ || int32_t(target_.source - last_source_) > 0) {
                last_source_ = target_.source;
                source_since_ = target_received_us_ - uint32_t(target_.source_age_ms) * 1000U;
                seen_source_ = true;
            }
            if (target_.source != last_source_ || target_.source_age_ms >= 50U ||
                now_us - source_since_ >= 50000U) {
                record_fault(uint32_t(JointFault::SourceTimeout), now_us);
                inhibit(now_us);
                return;
            }
        }
        if (((target_.mode >= 8U && target_.mode <= 10U) ||
             (target_.extended && target_.mode >= kDragMode)) &&
            drive_(JointDriveAction::Apply, target_, now_us)) {
            drive_state_ = DriveState::Enabled;
            mode_display_ = target_.mode;
            last_drive_us_ = now_us;
        } else {
            record_fault(uint32_t(JointFault::RejectedTarget), now_us);
            disable_drive(now_us, DriveState::Fault);
        }
    }
}

void JointCanopenFd::handle_nmt(const hal::CanFrame& rx)
{
    if (rx.size != 2U || (rx.data[1] != 0U && rx.data[1] != node_id_)) return;
    switch (rx.data[0]) {
    case 0x01: state_ = Nmt::Operational; break;
    case 0x02: state_ = Nmt::Stopped; break;
    case 0x80: state_ = Nmt::PreOperational; break;
    case 0x82: reset(); break;
    default: return;
    }
    seen_sync_ = false;
}

void JointCanopenFd::process(const hal::CanFrame& rx, const JointFeedbackSample& sample,
                             uint32_t now_us, Send send, void* context)
{
    if (!rx.fd || rx.extended || boot_pending_) return;
    if (rx.id == 0U) {
        if (rx.size == 2U && (rx.data[1] == 0U || rx.data[1] == node_id_) &&
            (rx.data[0] == 0x02U || rx.data[0] == 0x80U || rx.data[0] == 0x82U)) {
            const auto next = drive_state_ == DriveState::Fault && rx.data[0] != 0x82U
                ? DriveState::Fault : DriveState::Disabled;
            disable_drive(now_us, next);
            target_pending_ = false;
        }
        handle_nmt(rx);
        return;
    }
    if (rx.id == kDriveCobId + node_id_ || rx.id == 0x300U + node_id_) {
        receive_drive_target(rx, now_us);
        return;
    }
    if (rx.id > kUsdoRequestBase && rx.id <= kUsdoRequestBase + 127U) {
        if (state_ != Nmt::Stopped) handle_usdo(rx, sample, now_us, send, context);
        return;
    }
    if (rx.id != kSyncCobId || state_ != Nmt::Operational || rx.size > 1U) return;
    const uint8_t counter = rx.size == 1U ? rx.data[0] : 0U;
    if (rx.size == 1U && (counter == 0U || counter > 240U)) return;

    if (seen_sync_) {
        const uint32_t elapsed = now_us - last_sync_us_;
        if (elapsed < kSyncPeriodUs / 2U ||
            (counter != 0U && counter == sync_counter_ &&
             elapsed < 2U * kSyncPeriodUs)) return;
    }
    if (seen_sync_ && counter && sync_counter_) {
        const unsigned expected = sync_counter_ % 240U + 1U;
        missed_sync_ += (counter + 240U - expected) % 240U;
    }
    seen_sync_ = true;
    last_sync_us_ = now_us;
    sync_counter_ = counter;
    apply_drive_target(sample, now_us);
    auto feedback = frame(feedback_cob_id(), 32U);
    if (pack_feedback(sample, now_us, statusword(sample), mode_display_, counter, feedback.data)) {
        if (!send(context, feedback)) ++dropped_;
    } else ++stale_samples_;
    if (sample.valid) max_sample_age_ = std::max(max_sample_age_, now_us - sample.sample_us);
    if (extended_session_ && !send(context, diagnostics(sample, now_us))) ++dropped_;
}

hal::CanFrame JointCanopenFd::diagnostics(const JointFeedbackSample& sample, uint32_t now_us) const
{
    auto ack = frame(0x280U + node_id_, 48);
    write32(ack.data, sample.applied_sequence);
    write32(ack.data + 4, sample.applied_us);
    write32(ack.data + 8, sample.apply_delay_us);
    write32(ack.data + 12, dropped_);
    write32(ack.data + 16, stale_samples_);
    write32(ack.data + 20, missed_sync_);
    write32(ack.data + 24, max_sample_age_);
    ack.data[28] = sample.brake_phase;
    const bool fresh = sample.valid && now_us - sample.sample_us <= kMaxSampleAgeUs;
    ack.data[29] = fresh ? sample.effort_flags : 0U;
    ack.data[30] = sample.applied_valid;
    ack.data[31] = sync_counter_;
    if (!scaled(sample.output_nm, 1000, ack.data + 32)) ack.data[29] &= ~1U;
    if (!scaled(sample.external_nm, 1000, ack.data + 36)) ack.data[29] &= ~6U;
    write32(ack.data + 40, sample.external_age_us);
    write32(ack.data + 44, first_fault_);
    return ack;
}

void JointCanopenFd::handle_usdo(const hal::CanFrame& rx, const JointFeedbackSample& sample,
                                 uint32_t now_us, Send send, void* context) const
{
    if (rx.size < 6U || rx.data[0] != node_id_ || rx.data[1] == 0x7FU) return;
    const uint8_t sub = rx.data[3];
    const uint16_t index = uint16_t(rx.data[4]) | (uint16_t(rx.data[5]) << 8U);
    auto response = frame(kUsdoResponseBase + node_id_, 7U);
    response.data[0] = static_cast<uint8_t>(rx.id - kUsdoRequestBase);
    response.data[2] = rx.data[2];
    response.data[3] = sub;
    response.data[4] = rx.data[4];
    response.data[5] = rx.data[5];
    uint8_t error = 0U;
    uint8_t type = kU32;
    uint8_t size = 4U;
    uint32_t bits = 0U;
    if (rx.data[1] == 0x01U) {
        error = 0x32U; // Read-only object dictionary.
    } else if (rx.data[1] != 0x11U) {
        error = 0x13U;
    } else if (rx.size != 6U) {
        error = 0x1EU;
    } else if ((index == 0x1800U || index == 0x1802U) ||
               (drive_ && (index == 0x1400U || index == 0x1401U || index == 0x1801U))) {
        switch (sub) {
        case 0U: bits = 2U; type = kU8; size = 1U; break;
        case 1U:
            switch (index) {
            case 0x1400: bits = 0x200U; break;
            case 0x1401: bits = 0x300U; break;
            case 0x1800: bits = 0x180U; break;
            case 0x1801: bits = 0x280U; break;
            default: bits = 0x380U; break;
            }
            bits += node_id_;
            break;
        case 2U: bits = index == 0x1802U ? 0xFEU : 1U; type = kU8; size = 1U; break;
        default: error = 0x34U; break;
        }
    } else if ((drive_ && (index == 0x1601 || index == 0x2110 || index == 0x1A01 || index == 0x2111)) ||
               index == 0x1A02 || index == 0x2112) {
        const bool command = index == 0x1601 || index == 0x2110;
        const bool event = index == 0x1A02 || index == 0x2112;
        const unsigned count = command ? sizeof(kCommandTypes) : event ? sizeof(kEventTypes) : sizeof(kDiagnosticTypes);
        const uint8_t* types = command ? kCommandTypes : event ? kEventTypes : kDiagnosticTypes;
        if (!sub) { bits = count; type = kU8; size = 1; }
        else if (sub > count) error = 0x34;
        else if (index < 0x2000) {
            const uint32_t object = command ? 0x2110U : event ? 0x2112U : 0x2111U;
            bits = (object << 16U) | (uint32_t(sub) << 8U) | (scalar_size(types[sub - 1]) * 8U);
        } else {
            type = types[sub - 1];
            size = scalar_size(type);
            if (event) bits = sub == 1 ? first_fault_ : sub == 2 ? first_fault_us_ : fault_brake_;
            else {
                unsigned offset = 0;
                for (unsigned i = 1; i < sub; ++i) offset += scalar_size(types[i - 1]);
                hal::CanFrame ack;
                if (!command) ack = diagnostics(sample, now_us);
                const auto* bytes = command ? command_wire_ : ack.data;
                for (unsigned i = 0; i < size; ++i) bits |= uint32_t(bytes[offset+i]) << (8U*i);
            }
        }
    } else if (drive_ && index == 0x1600U) {
        if (sub == 0U) {
            bits = sizeof(kDriveMap) / sizeof(kDriveMap[0]);
            type = kU8;
            size = 1U;
        } else if (sub <= sizeof(kDriveMap) / sizeof(kDriveMap[0])) {
            bits = kDriveMap[sub - 1U];
        } else {
            error = 0x34U;
        }
    } else if (index == 0x1A00U) {
        if (sub == 0U) {
            bits = kFeedbackMapCount;
            type = kU8;
            size = 1U;
        } else if (sub <= kFeedbackMapCount) {
            const auto& entry = kFeedbackMap[sub - 1U];
            bits = uint32_t(entry.index) << 16U | uint32_t(entry.sub) << 8U | entry.size * 8U;
        } else {
            error = 0x34U;
        }
    } else if (sub == 0U && index != 0x2000U &&
               index != 0x6064U && index != 0x606CU) {
        switch (index) {
        case 0x2100: bits = first_fault_; break;
        case 0x2101: bits = first_fault_us_; break;
        case 0x2102: bits = fault_brake_; type = kU8; size = 1; break;
        case 0x1000: break; // No claim of complete drive-profile conformance.
        case 0x1001:
            bits = statusword(sample) == static_cast<uint16_t>(DriveState::Fault);
            type = kU8;
            size = 1U;
            break;
        case 0x1005: bits = kSyncCobId; break;
        case 0x1006: bits = kSyncPeriodUs; break;
        case 0x1017: bits = kHeartbeatUs / 1000U; type = kU16; size = 2U; break;
        case 0x6041: bits = statusword(sample); type = kU16; size = 2U; break;
        case 0x6061: bits = mode_display_; type = kI8; size = 1U; break;
        case 0x6502: bits = drive_ ? 0x0380U : 0U; break;
        default: error = 0x33U; break;
        }
    } else if (index == 0x2000U && sub == 0U) {
        bits = 6U;
        type = kU8;
        size = 1U;
    } else {
        error = 0x34U;
        unsigned offset = 0U;
        for (const auto& entry : kFeedbackMap) {
            if (index == entry.index && sub == entry.sub) {
                uint8_t data[32] {};
                if (pack_feedback(sample, now_us, statusword(sample), mode_display_, sync_counter_, data)) {
                    for (unsigned i = 0; i < entry.size; ++i)
                        bits |= uint32_t(data[offset + i]) << (8U * i);
                    type = entry.type;
                    size = entry.size;
                    error = 0U;
                } else {
                    error = 0x64U;
                }
                break;
            }
            offset += entry.size;
        }
    }
    if (error != 0U) {
        response.data[1] = 0x7FU;
        response.data[6] = error;
    } else {
        response.size = 8U + size;
        response.data[1] = 0x31U;
        response.data[6] = type;
        response.data[7] = size;
        write32(&response.data[8], bits);
    }
    (void)send(context, response);
}

void JointCanopenFd::service(uint32_t now_us, Send send, void* context)
{
    if (drive_state_ == DriveState::Enabled && now_us - last_drive_us_ >= kNetworkTimeoutUs) {
        record_fault(uint32_t(JointFault::NetworkTimeout), now_us);
        inhibit(now_us);
    }
    if (boot_pending_ || uint32_t(now_us - last_heartbeat_us_) >= kHeartbeatUs) {
        auto heartbeat = frame(kHeartbeatCobId + node_id_, 1U);
        heartbeat.data[0] = boot_pending_ ? 0U : static_cast<uint8_t>(state_);
        if (send(context, heartbeat)) {
            boot_pending_ = false;
            last_heartbeat_us_ = now_us;
        }
    }
}

} // namespace app
