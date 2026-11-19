#pragma once

#include <drivers/can_frame.h>

namespace app {

struct JointFeedbackSample {
    float position_rad {};
    float velocity_rad_s {};
    float current_a {};
    float bus_voltage_v {};
    uint32_t sample_us {};
    uint32_t sequence {};
    uint32_t faults {};
    bool valid {};
    bool output_enabled {};
};

struct JointDriveTarget {
    uint16_t controlword {};
    uint8_t mode {};
    int32_t position_urad {};
    int32_t velocity_urad_s {};
    int16_t torque_per_mille {};
};

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
    void service(uint32_t now_us, Send send, void* context);

private:
    enum class Nmt : uint8_t { Stopped = 4U, Operational = 5U, PreOperational = 127U };
    enum class DriveState : uint16_t {
        Disabled = 0x0040U, Ready = 0x0021U, SwitchedOn = 0x0023U,
        Enabled = 0x0027U, Fault = 0x0008U,
    };
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
    uint32_t target_received_us_ {};
    uint32_t last_drive_us_ {};
    uint8_t mode_display_ {};
    bool target_pending_ {};
    bool previous_fault_reset_ {};
};

} // namespace app
