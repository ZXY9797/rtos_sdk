#pragma once

#include <cstdint>

#include <algo/trajectory/hermite_interp.h>

namespace algo::motor {

struct JointTargetConfig {
    // 100 Hz 外部目标名义周期；MIT 位置 Hermite 段时长，同时作为超时阈值下限。
    float external_order_period_s {0.010F};
    float external_order_timeout_s {0.500F}; // MIT 超时后速度和电流前馈直接清零，单位 s。
    float torque_abs_limit_a {0.0F};        // 外部 q 轴电流指令绝对值限幅，单位 A。
};

enum class JointTargetMode : uint8_t {
    Torque = 0U,   // 力矩模式。
    Velocity = 1U, // 速度模式。
    Position = 2U, // 位置模式。
    Mit = 3U,      // MIT 模式，也是默认初始化模式。
};

inline constexpr JointTargetMode kJointTargetDefaultMode {JointTargetMode::Mit}; // 默认目标模式。

struct JointTargetOrder {
    float position_rad {0.0F};       // RPDO0 目标位置，单位 rad。
    float velocity_rad_s {0.0F};     // RPDO0 目标速度，单位 rad/s。
    float torque_a {0.0F};           // RPDO0 目标 q 轴电流，单位 A。
    uint8_t mit_kp {0U};             // RPDO0 MIT kp 原始值，范围 0 到 255。
    uint8_t mit_kd {0U};             // RPDO0 MIT kd 原始值，范围 0 到 255。
    uint32_t host_timestamp_us {0U}; // 上位机时间戳，单位 us。
};

struct JointTorqueOrderProtect {
    bool stop_active {false};          // 力矩指令保护是否已触发。
    uint8_t stop_reason {0U};          // 力矩指令保护触发原因编码。
    uint32_t host_timestamp_us {0U};   // 触发保护时的上位机时间戳，单位 us。
    float torque_a {0.0F};             // 当前参与保护检查的 q 轴电流指令，单位 A。
};

struct JointExternalOrder {
    JointTargetOrder last_order {};                    // 上一帧有效外部目标指令。
    JointTargetOrder current_order {};                 // 当前有效外部目标指令。
    JointTorqueOrderProtect torque_order_protect {};   // 外部力矩指令保护状态。
    bool last_order_valid {false};                     // last_order 是否有效。
    bool current_order_valid {false};                  // current_order 是否有效。
    bool refresh_flag {false};                         // 当前周期是否收到新外部目标。
    bool duplicate_order_flag {false};                 // 当前周期是否为重复外部目标。
    uint32_t last_rx_run_count {0U};                   // 上一帧目标写入时的控制周期计数。
    uint32_t current_rx_run_count {0U};                // 当前目标写入时的控制周期计数。
    int32_t delta_host_timestamp_us {0};               // 当前目标和上一目标的主站时间戳差值，单位 us。
    float current_order_age_s {0.0F};                  // 当前目标距离接收时刻的年龄，单位 s。
};

struct JointFinalCommand {
    float position_cmd_rad {0.0F};   // 输出给 joint_ctrl 的位置目标，单位 rad。
    float velocity_cmd_rad_s {0.0F}; // MIT 下直接送入 Kd 的外部速度目标，单位 rad/s。
    float torque_cmd_a {0.0F};       // 输出给 joint_ctrl 的 q 轴电流目标，单位 A。
    float mit_kp_cmd {0.0F};         // 输出给 joint_ctrl 的 MIT kp。
    float mit_kd_cmd {0.0F};         // 输出给 joint_ctrl 的 MIT kd。
};

struct JointHermiteInterp {
    algo::motor::trajectory::CubicHermiteSegment segment {}; // 仅用于 MIT 位置的插值段。
    algo::motor::trajectory::CubicHermiteSample last_sample {}; // 曲线斜率仅用于接续下一段。
    uint32_t segment_start_run_count {0U};            // 插值段起始控制周期计数。
    bool init_finish {false};                        // 是否已从反馈位置建立插值起点。
    bool segment_valid {false};                      // 当前段是否仍需推进。
};

struct JointTargetContext {
    JointTargetConfig config {};          // joint_target 运行配置。
    JointExternalOrder external_order {}; // 外部目标命令缓存和断包相关状态。
    JointHermiteInterp hermite_interp {}; // MIT 位置插值运行状态。
    JointFinalCommand final_command {};   // 各模式刷新后的最终目标输出。
    float position_rad {0.0F};            // 位置模式目标缓存，单位 rad。
    float speed_order_rad_s {0.0F};       // 速度模式目标缓存，单位 rad/s。
    float torque_order_a {0.0F};          // 力矩模式 q 轴电流目标缓存，单位 A。
    bool configured {false};              // 配置是否已成功写入。
};

bool joint_target_configure(const JointTargetConfig& config, JointTargetContext* target);
void joint_target_reset(JointTargetContext* target);

bool joint_target_update_external_order(const JointTargetOrder& order,
                                        uint32_t run_count,
                                        JointTargetContext* target);

void joint_target_hold_feedback_command(float position_feedback_rad,
                                        JointTargetContext* target);
void joint_target_reset_mode_switch_hold(float position_feedback_rad,
                                         JointTargetContext* target);
void joint_target_sync_stop_target(float position_feedback_rad,
                                   JointTargetContext* target);

void joint_target_refresh_mit(uint32_t run_count,
                              float control_frequency_hz,
                              float position_feedback_rad,
                              JointTargetContext* target);
void joint_target_refresh_position(float position_feedback_rad,
                                   JointTargetContext* target);
void joint_target_refresh_velocity(float position_feedback_rad,
                                   JointTargetContext* target);
void joint_target_refresh_torque(float position_feedback_rad,
                                 JointTargetContext* target);

const JointFinalCommand* joint_target_final_command(const JointTargetContext* target);
const JointExternalOrder* joint_target_external_order(const JointTargetContext* target);

}  // namespace algo::motor
