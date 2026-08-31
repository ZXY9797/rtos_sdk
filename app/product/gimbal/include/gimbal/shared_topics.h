#pragma once

#include <gimbal/safety_manager.h>
#include <gimbal/types.h>
#include <ipc/snapshot_topic.h>

namespace gimbal {

struct SharedTopics {
    ipc::SnapshotTopic<ImuSample> imu_state;
    ipc::SnapshotTopic<JointState> joint_state;
    ipc::SnapshotTopic<ImuThermalState> imu_thermal_state;
    ipc::SnapshotTopic<AttitudeState> attitude_state;
    ipc::SnapshotTopic<HandleState> handle_state;
    ipc::SnapshotTopic<MotionReference> motion_reference;
    ipc::SnapshotTopic<MotorCommand> motor_command;
    ipc::SnapshotTopic<MotorFeedback> motor_feedback;
    ipc::SnapshotTopic<CapabilitySet> capabilities;
    ipc::SnapshotTopic<SafetyOutput> safety_output;
};

} // namespace gimbal
