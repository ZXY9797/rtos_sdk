#pragma once

#include <algo/motor/feedback.h>
#include <algo/motor/joint_target.h>
#include <algo/controller/pid_sys.h>
#include <algo/controller/leso.h>
#include <algo/filter/filter_sys.h>
#include <algo/filter/notch_peak.h>
#include <algo/trajectory/s_curve_planner.h>

namespace algo::motor {
struct MitLoopConfig {
    // External MIT gains use the full uint8_t command range (0..255).
    algo::motor::basic_math::LinearMap kp_map {0.0F, 255.0F, 0.0F, 20.0F};
    algo::motor::basic_math::LinearMap kd_map {0.0F, 255.0F, 0.0F, 10.0F};
    float position_output_limit_a {4.0F};
    float velocity_output_limit_a {4.0F};
    float q_current_limit_a {8.0F};
};

struct SpeedLoopConfig {
    algo::motor::controller::LesoConfig leso {};
    float q_current_limit_a {8.0F};
    // Filters only the speed loop's iq output in Velocity and Position modes.
    bool q_current_low_pass_enable {false};
    algo::motor::filter::FilterSysConfig q_current_low_pass {};
    // sample_freq_hz is supplied by JointTaskConfig::frequency_hz.
};

struct PositionDeadzoneConfig {
    bool enable {true};
    float error_deadzone_rad {0.006F};
    float error_hysteresis_rad {0.01F};
    float output_velocity_threshold_rad_s {1.0F};
    float stable_time_s {1.0F};
};

enum class PositionTargetPlanMode : uint8_t {
    SCurve = 0U,
    Linear = 1U,
};

struct PositionLoopConfig {
    algo::motor::controller::PidSysConfig position_pid {};
    float output_velocity_limit_rad_s {3.5F};
    // Add the signed planner velocity to the position PID output before limiting.
    bool velocity_feedforward_enable {false};
    PositionTargetPlanMode target_plan_mode {PositionTargetPlanMode::SCurve};
    algo::motor::trajectory::SCurvePlannerConfig target_plan {};
    PositionDeadzoneConfig deadzone {};
    bool external_torque_q_current_limit_enable {false};
};

struct TorqueLoopConfig {
    float q_current_limit_a {8.0F};
};

struct JointCtrlLoopConfig {
    MitLoopConfig mit {};
    SpeedLoopConfig speed {};
    PositionLoopConfig position {};
    TorqueLoopConfig torque {};
};

struct JointQCurrentFilterConfig {
    bool notch_enable {false};
    algo::motor::filter::NotchPeakConfig notch {};
    bool low_pass_enable {false};
    algo::motor::filter::FilterSysConfig low_pass {};
    // Both sample frequencies are supplied by JointTaskConfig::frequency_hz.
};


struct JointAlgorithmConfig {
    float frequency_hz {4000.0F};
    float invert_sign {1.0F};
    MotorBasicConfig motor {};
    JointCtrlLoopConfig loop {};
    JointQCurrentFilterConfig q_current_filter {};
    JointTargetConfig target {};
    JointTargetMode default_mode {JointTargetMode::Mit};
};

struct JointLoopFeedback {
    float joint_position_rad {0.0F};
    float joint_velocity_rad_s {0.0F};
    float motor_velocity_rad_s {0.0F};
};

struct JointLoopOutput {
    bool valid {false};
    bool energize {false};
    bool transition_complete {false};
    float q_current_a {0.0F};
    float planned_position_rad {0.0F};
    float planned_velocity_rad_s {0.0F};
    float speed_observer_output_a {0.0F};
};

struct JointLoopTrace {
    JointTargetMode mode {};
    JointFinalCommand target {};
    uint32_t host_timestamp_us {0U};
    float target_age_s {0.0F};
    float planned_position_rad {0.0F}, planned_velocity_rad_s {0.0F};
    float position_error_rad {0.0F}, speed_output_a {0.0F};
    float mit_position_a {0.0F}, mit_velocity_a {0.0F};
    bool target_valid {false}, waiting {false};
};

// All angles are joint-side rad except explicitly named motor velocity.
// Torque commands use q-axis amperes.
class JointController {
public:
    bool configure(const JointAlgorithmConfig& config);
    void reset(const JointLoopFeedback& feedback);
    bool submit(const JointTargetOrder& order);
    bool select_mode(JointTargetMode mode, const JointLoopFeedback& feedback,
                                  bool wait_for_target = true, bool force_entry = false);
    JointLoopOutput update(const JointLoopFeedback& feedback, bool enabled,
                           bool power_stage_ready = true);
    const JointExternalOrder& external_order() const { return m_target.external_order; }
    const JointFinalCommand& target() const { return m_target.final_command; }
    JointTargetMode mode() const { return mode_; }
    JointLoopTrace trace() const;
    void track_applied(float current_a);

private:
    struct MitPositionLoop {
        float target_position_rad {0.0F};
        float position_error_rad {0.0F};
        float kp {0.0F};
        float output_a {0.0F};
    };

    struct MitVelocityLoop {
        float target_velocity_rad_s {0.0F};
        float velocity_feedback_rad_s {0.0F};
        float velocity_error_rad_s {0.0F};
        float kd {0.0F};
        float output_a {0.0F};
    };

    struct PositionVelocityLoop {
        algo::motor::controller::PidSys position_pid {};
        algo::motor::controller::Leso speed_leso {};
        algo::motor::trajectory::SCurvePlanner target_planner {};
        float position_set_rad {0.0F};
        float target_position_rad {0.0F};
        float target_velocity_limit_rad_s {0.0F};
        float planned_position_rad {0.0F};
        float planned_velocity_rad_s {0.0F};
        float planned_acceleration_rad_s2 {0.0F};
        float linear_target_position_rad {0.0F};
        float linear_step_rad {0.0F};
        float linear_velocity_rad_s {0.0F};
        float position_error_rad {0.0F};
        float position_pid_input_rad {0.0F};
        float position_output_rad_s {0.0F};
        float deadzone_abs_error_rad {0.0F};
        float deadzone_exit_threshold_rad {0.0F};
        uint32_t deadzone_count {0U};
        uint32_t deadzone_count_max {0U};
        bool deadzone_latched {false};
        float speed_set_rad_s {0.0F};
        float speed_output_a {0.0F};
        bool linear_plan_active {false};
    };

    struct TorqueLoop {
        float target_current_a {0.0F};
        float q_current_output_a {0.0F};
    };

    bool configure_control_loops();
    bool configure_speed_q_current_filter();
    float filter_speed_q_current_target(float q_current_a);
    bool configure_q_current_filters();
    void reset_q_current_filters();
    float filter_q_current_target(float q_current_a, float limit_a);
    void reset_loop_state();
    void reset_position_planner_to_feedback();
    void reset_position_deadzone();
    void update_position_deadzone();
    float position_plan_sample_time_s() const;
    float resolve_position_plan_velocity(float velocity_cmd_rad_s) const;
    bool update_s_curve_position_plan(
    const JointFinalCommand& command,
    algo::motor::trajectory::SCurveSample* sample);
    bool update_linear_position_plan(
    const JointFinalCommand& command,
    algo::motor::trajectory::SCurveSample* sample);
    float run_torque_mode(const JointFinalCommand& command);
    float run_velocity_mode(const JointFinalCommand& command);
    float run_position_mode(const JointFinalCommand& command);
    float run_mit_mode(const JointFinalCommand& command);
    JointAlgorithmConfig config_ {};
    MotorBasicConfig m_motor_basic_config {};
    MotorJointAngleFeedback m_joint_angle_feedback {};
    MotorVelocityFeedback m_velocity_feedback {};
    PositionVelocityLoop m_position_velocity_loop {};
    MitPositionLoop m_mit_position_loop {};
    MitVelocityLoop m_mit_velocity_loop {};
    TorqueLoop m_torque_loop {};
    filter::NotchPeak m_q_current_notch {};
    filter::FilterSys m_q_current_low_pass {};
    filter::FilterSys m_speed_q_current_low_pass {};
    JointTargetContext m_target {};
    JointTargetMode mode_ {JointTargetMode::Mit};
    uint32_t cycle_ {0U};
    uint32_t blend_total_ {1U};
    uint32_t blend_remaining_ {0U};
    float blend_from_a_ {0.0F};
    float output_a_ {0.0F};
    float m_entry_stop_velocity {0.0F};
    bool waiting_ {false};
    bool configured_ {false};
    bool invalid_ {false};
    bool enabled_ {false};
    bool seed_speed_on_entry_ {false};
};

}  // namespace algo::motor
