#pragma once

#include "control/joint_drive.h"
#include <drivers/can_frame.h>

namespace app {

enum class JointDriveAction { Apply, Disable, ResetFault };

class JointCanopenFd {
public:
    using Send = bool (*)(void*, const hal::CanFrame&);
    using Drive = bool (*)(JointDriveAction, const JointDriveTarget&, uint32_t);

    explicit JointCanopenFd(uint8_t node_id, Drive drive = nullptr)
        : node_id_(node_id), drive_(drive) {}
    uint32_t feedback_cob_id() const { return 0x180U + node_id_; }
    void reset();
    void inhibit(uint32_t now_us);
    void process(const hal::CanFrame& rx, const JointFeedbackSample& sample,
                 uint32_t now_us, Send send, void* context);
    void record_drop() { ++dropped_; }
    void service(uint32_t now_us, Send send, void* context);
    void observe(const JointFeedbackSample&, uint32_t now, Send, void*);

private:
    enum class Nmt : uint8_t { Stopped = 4U, Operational = 5U, PreOperational = 127U };
    enum class DriveState : uint16_t {
        Disabled = 0x0040U, Ready = 0x0021U, SwitchedOn = 0x0023U,
        Enabled = 0x0027U, Fault = 0x0008U,
    };
    hal::CanFrame diagnostics(const JointFeedbackSample&, uint32_t now) const;
    void record_fault(uint32_t reason, uint32_t now);
    void receive_drive_target(const hal::CanFrame& frame, uint32_t now_us);
    void apply_drive_target(const JointFeedbackSample& sample, uint32_t now_us);
    void disable_drive(uint32_t now_us, DriveState next);
    uint16_t statusword(const JointFeedbackSample& sample) const;
    void handle_nmt(const hal::CanFrame& frame);
    void handle_usdo(const hal::CanFrame& frame, const JointFeedbackSample& sample,
                     uint32_t now_us, Send send, void* context) const;

    uint8_t node_id_;
    Drive drive_;
    Nmt state_ {Nmt::PreOperational};
    bool boot_pending_ {true};
    bool seen_sync_ {};
    uint8_t sync_counter_ {};
    uint32_t last_sync_us_ {};
    uint32_t last_heartbeat_us_ {};
    DriveState drive_state_ {DriveState::Disabled};
    JointDriveTarget target_ {};
    uint8_t command_wire_[48] {};
    uint32_t target_received_us_ {};
    uint32_t last_drive_us_ {};
    uint8_t mode_display_ {};
    bool extended_session_{}, seen_source_{}, seen_command_{}, event_pending_{};
    uint32_t last_source_{}, source_since_{}, last_command_{};
    uint32_t dropped_{}, stale_samples_{}, missed_sync_{}, max_sample_age_{};
    uint32_t first_fault_{}, first_fault_us_{};
    uint8_t fault_brake_{};
    bool target_pending_ {};
    bool previous_fault_reset_ {};
};

} // namespace app
