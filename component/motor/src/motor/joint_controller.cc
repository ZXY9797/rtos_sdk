#include <algo/motor/joint_controller.h>
#include <algo/interaction/joint_interaction.h>
#include <cmath>

namespace algo::motor {

JointLoopTrace JointController::trace() const
{
    JointLoopTrace result;
    result.mode = mode_;
    result.target = m_target.final_command;
    result.host_timestamp_us = m_target.external_order.current_order.host_timestamp_us;
    result.target_age_s = m_target.external_order.current_order_age_s;
    result.target_valid = m_target.external_order.current_order_valid;
    result.waiting = waiting_;
    result.planned_position_rad = m_position_velocity_loop.planned_position_rad;
    result.planned_velocity_rad_s = m_position_velocity_loop.planned_velocity_rad_s;
    result.position_error_rad = m_position_velocity_loop.position_error_rad;
    result.speed_output_a = m_position_velocity_loop.speed_output_a;
    result.mit_position_a = m_mit_position_loop.output_a;
    result.mit_velocity_a = m_mit_velocity_loop.output_a;
    return result;
}

bool JointController::configure(const JointAlgorithmConfig& config)
{
    configured_ = false;
    if (!std::isfinite(config.frequency_hz) || config.frequency_hz <= 0.0F ||
        0.020F * config.frequency_hz >= 4294967040.0F ||
        static_cast<unsigned>(config.default_mode) > 3U ||
        !std::isfinite(config.invert_sign) || std::fabs(config.invert_sign) != 1.0F ||
        !config.motor.valid || !std::isfinite(config.motor.gear_ratio) ||
        config.motor.gear_ratio <= 0.0F) return false;
    const auto& deadzone = config.loop.position.deadzone;
    if (deadzone.enable && (!std::isfinite(deadzone.stable_time_s) ||
        deadzone.stable_time_s <= 0.0F || !std::isfinite(deadzone.error_deadzone_rad) ||
        deadzone.error_deadzone_rad < 0.0F || !std::isfinite(deadzone.error_hysteresis_rad) ||
        deadzone.error_hysteresis_rad < 0.0F ||
        !std::isfinite(deadzone.output_velocity_threshold_rad_s) ||
        deadzone.output_velocity_threshold_rad_s < 0.0F ||
        deadzone.stable_time_s * config.frequency_hz >= 4294967040.0F)) return false;
    config_ = config;
    mode_ = config.default_mode;
    m_motor_basic_config = config.motor;
    if (!joint_target_configure(config.target, &m_target) || !configure_control_loops()) return false;
    configured_ = true;
    reset({});
    return true;
}

void JointController::reset(const JointLoopFeedback& feedback)
{
    m_joint_angle_feedback.multi_turn_angle_filted = feedback.joint_position_rad;
    m_velocity_feedback.angular_velocity = feedback.motor_velocity_rad_s;
    m_velocity_feedback.output_angular_velocity = feedback.joint_velocity_rad_s;
    reset_loop_state();
    reset_position_planner_to_feedback();
    joint_target_reset(&m_target);
    joint_target_hold_feedback_command(feedback.joint_position_rad, &m_target);
    blend_remaining_ = 0U;
    output_a_ = blend_from_a_ = m_entry_stop_velocity = 0.0F;
    waiting_ = false;
    invalid_ = false;
    enabled_ = false;
    seed_speed_on_entry_ = false;
}

bool JointController::submit(const JointTargetOrder& order)
{
    if (!configured_ || !joint_target_update_external_order(order, cycle_, &m_target)) return false;
    if (waiting_) {
        blend_from_a_ = output_a_;
        blend_total_ = static_cast<uint32_t>(0.020F * config_.frequency_hz);
        if (blend_total_ == 0U) blend_total_ = 1U;
        blend_remaining_ = blend_total_;
        waiting_ = false;
    }
    return true;
}

bool JointController::select_mode(JointTargetMode mode, const JointLoopFeedback& feedback,
                                  bool wait_for_target, bool force_entry)
{
    if (!configured_ || static_cast<unsigned>(mode) > 3U ||
        !std::isfinite(feedback.joint_position_rad) ||
        !std::isfinite(feedback.joint_velocity_rad_s) ||
        !std::isfinite(feedback.motor_velocity_rad_s)) return false;
    if (mode == mode_ && !force_entry) return true;
    const bool previous_speed = mode_ == JointTargetMode::Velocity || mode_ == JointTargetMode::Position;
    const bool next_speed = mode == JointTargetMode::Velocity || mode == JointTargetMode::Position;
    blend_from_a_ = output_a_;
    blend_total_ = static_cast<uint32_t>(0.020F * config_.frequency_hz);
    if (blend_total_ == 0U) blend_total_ = 1U;
    blend_remaining_ = blend_total_;
    m_joint_angle_feedback.multi_turn_angle_filted = feedback.joint_position_rad;
    joint_target_reset_mode_switch_hold(feedback.joint_position_rad, &m_target);
    if (mode == JointTargetMode::Position) {
        m_position_velocity_loop.position_pid.reset();
        reset_position_deadzone();
        reset_position_planner_to_feedback();
        m_entry_stop_velocity = feedback.joint_velocity_rad_s;
    } else m_entry_stop_velocity = 0.0F;
    if (next_speed && !previous_speed)
        m_position_velocity_loop.speed_leso.seed(feedback.motor_velocity_rad_s, blend_from_a_);
    seed_speed_on_entry_ = !enabled_ && next_speed && !previous_speed;
    mode_ = mode;
    waiting_ = wait_for_target;
    return true;
}

void JointController::track_applied(float current)
{
    if (!std::isfinite(current)) return;
    output_a_ = current;
    if (mode_ == JointTargetMode::Velocity || mode_ == JointTargetMode::Position)
        (void)m_position_velocity_loop.speed_leso.track_output(current);
}

JointLoopOutput JointController::update(const JointLoopFeedback& feedback, bool enabled,
                                        bool power_stage_ready)
{
    JointLoopOutput output {};
    if (!configured_) return output;
    if (!std::isfinite(feedback.joint_position_rad) ||
        !std::isfinite(feedback.joint_velocity_rad_s) ||
        !std::isfinite(feedback.motor_velocity_rad_s)) { reset({}); return output; }
    ++cycle_;
    m_joint_angle_feedback.multi_turn_angle_filted = feedback.joint_position_rad;
    m_velocity_feedback.angular_velocity = feedback.motor_velocity_rad_s;
    m_velocity_feedback.output_angular_velocity = feedback.joint_velocity_rad_s;
    if (!enabled || !power_stage_ready) {
        enabled_ = false;
        reset_loop_state();
        reset_position_planner_to_feedback();
        output_a_ = 0.0F;
        if (!enabled) {
            joint_target_reset(&m_target);
            joint_target_hold_feedback_command(feedback.joint_position_rad, &m_target);
            blend_remaining_ = 0U;
        }
        output.valid = true;
        return output;
    }
    if (!enabled_) {
        reset_loop_state();
        reset_position_planner_to_feedback();
        if (seed_speed_on_entry_)
            m_position_velocity_loop.speed_leso.seed(feedback.motor_velocity_rad_s, blend_from_a_);
        seed_speed_on_entry_ = false;
        output_a_ = 0.0F;
        enabled_ = true;
    }
    switch (mode_) {
    case JointTargetMode::Torque: joint_target_refresh_torque(feedback.joint_position_rad, &m_target); break;
    case JointTargetMode::Velocity: joint_target_refresh_velocity(feedback.joint_position_rad, &m_target); break;
    case JointTargetMode::Position: joint_target_refresh_position(feedback.joint_position_rad, &m_target); break;
    case JointTargetMode::Mit: joint_target_refresh_mit(cycle_, config_.frequency_hz, feedback.joint_position_rad, &m_target); break;
    }
    const auto* command = joint_target_final_command(&m_target);
    if (!command) { reset_loop_state(); output_a_ = 0.0F; return output; }
    invalid_ = false;
    float current = 0.0F;
    float limit = config_.loop.mit.q_current_limit_a;
    switch (mode_) {
    case JointTargetMode::Torque: current = run_torque_mode(*command); limit = config_.loop.torque.q_current_limit_a; break;
    case JointTargetMode::Velocity: current = run_velocity_mode(*command); limit = config_.loop.speed.q_current_limit_a; break;
    case JointTargetMode::Position:
        current = run_position_mode(*command); limit = config_.loop.speed.q_current_limit_a;
        if (config_.loop.position.external_torque_q_current_limit_enable &&
            std::fabs(command->torque_cmd_a) > 0.0F &&
            (limit <= 0.0F || std::fabs(command->torque_cmd_a) < limit)) limit = std::fabs(command->torque_cmd_a);
        break;
    case JointTargetMode::Mit: current = run_mit_mode(*command); break;
    }
    current = filter_q_current_target(current, limit);
    if (waiting_ && (mode_ == JointTargetMode::Torque || mode_ == JointTargetMode::Mit)) current = blend_from_a_;
    if (blend_remaining_) {
        const float progress = float(blend_total_ - blend_remaining_) / float(blend_total_);
        current = blend_from_a_ + trajectory::smoothstep(progress) * (current - blend_from_a_);
        --blend_remaining_;
    }
    output_a_ = basic_math::clamp_symmetric(current, limit);
    if (invalid_ || !std::isfinite(output_a_)) { reset(feedback); return output; }
    output.valid = output.energize = true;
    output.transition_complete = blend_remaining_ == 0U;
    output.q_current_a = output_a_;
    output.planned_position_rad = m_position_velocity_loop.planned_position_rad;
    output.planned_velocity_rad_s = m_position_velocity_loop.planned_velocity_rad_s;
    output.speed_observer_output_a = m_position_velocity_loop.speed_output_a;
    return output;
}

bool JointController::configure_control_loops()
{
    if (!configure_q_current_filters() || !configure_speed_q_current_filter()) {
        return false;
    }
    algo::motor::controller::PidSysConfig position_pid_config =
        config_.loop.position.position_pid;
    if (position_pid_config.sample_freq_hz <= 0.0F) {
        position_pid_config.sample_freq_hz = config_.frequency_hz;
    }
    if (!m_position_velocity_loop.position_pid.configure(position_pid_config)) {
        return false;
    }

    algo::motor::controller::LesoConfig speed_leso_config = config_.loop.speed.leso;
    if (speed_leso_config.sample_time_s <= 0.0F) {
        speed_leso_config.sample_time_s = 1.0F / config_.frequency_hz;
    }
    if (!m_position_velocity_loop.speed_leso.configure(speed_leso_config)) {
        return false;
    }

    if (static_cast<uint8_t>(config_.loop.position.target_plan_mode) >
        static_cast<uint8_t>(PositionTargetPlanMode::Linear)) {
        return false;
    }

    algo::motor::trajectory::SCurvePlannerConfig target_plan_config =
        config_.loop.position.target_plan;
    if (target_plan_config.sample_time_s <= 0.0F) {
        target_plan_config.sample_time_s = 1.0F / config_.frequency_hz;
    }
    if (target_plan_config.max_velocity_limit_rad_s <= 0.0F) {
        target_plan_config.max_velocity_limit_rad_s =
            config_.loop.position.output_velocity_limit_rad_s;
    }
    if (target_plan_config.default_max_velocity_rad_s <= 0.0F) {
        target_plan_config.default_max_velocity_rad_s =
            target_plan_config.max_velocity_limit_rad_s;
    }
    if (!m_position_velocity_loop.target_planner.configure(target_plan_config)) {
        return false;
    }

    const auto& deadzone_config = config_.loop.position.deadzone;
    if (deadzone_config.enable) {
        m_position_velocity_loop.deadzone_count_max =
            static_cast<uint32_t>(deadzone_config.stable_time_s *
                                  config_.frequency_hz);
    } else {
        m_position_velocity_loop.deadzone_count_max = 0U;
    }

    reset_loop_state();
    return true;
}

bool JointController::configure_speed_q_current_filter()
{
    m_speed_q_current_low_pass = {};
    const auto& config = config_.loop.speed;
    if (!config.q_current_low_pass_enable) {
        return true;
    }
    auto low_pass = config.q_current_low_pass;
    low_pass.sample_freq_hz = config_.frequency_hz;
    if (low_pass.pass_type != algo::motor::filter::FilterPassType::LowPass ||
        (low_pass.proto != algo::motor::filter::FilterProto::Butter &&
         low_pass.proto != algo::motor::filter::FilterProto::Cheby2) ||
        low_pass.freq_hz >= 0.5F * low_pass.sample_freq_hz) {
        return false;
    }
    return m_speed_q_current_low_pass.configure(low_pass);
}

float JointController::filter_speed_q_current_target(float q_current_a)
{
    const auto& config = config_.loop.speed;
    if (!algo::motor::basic_math::is_finite_float(q_current_a)) {
        m_speed_q_current_low_pass.reset();
        return 0.0F;
    }
    q_current_a = algo::motor::basic_math::clamp_symmetric(q_current_a, config.q_current_limit_a);
    if (!config.q_current_low_pass_enable) {
        return q_current_a;
    }
    q_current_a = m_speed_q_current_low_pass.update(q_current_a);
    if (!algo::motor::basic_math::is_finite_float(q_current_a)) {
        m_speed_q_current_low_pass.reset();
        return 0.0F;
    }
    return algo::motor::basic_math::clamp_symmetric(q_current_a, config.q_current_limit_a);
}

bool JointController::configure_q_current_filters()
{
    m_q_current_notch = {};
    m_q_current_low_pass = {};
    const auto& config = config_.q_current_filter;
    if (config.notch_enable) {
        auto notch = config.notch;
        notch.sample_freq_hz = config_.frequency_hz;
        // NotchPeak also supports gain peaks (0 < as_param < 1); iq uses a notch.
        if (!(notch.as_param > 1.0F) || !m_q_current_notch.configure(notch)) {
            return false;
        }
    }
    if (config.low_pass_enable) {
        auto low_pass = config.low_pass;
        low_pass.sample_freq_hz = config_.frequency_hz;
        if (low_pass.pass_type != algo::motor::filter::FilterPassType::LowPass ||
            (low_pass.proto != algo::motor::filter::FilterProto::Butter &&
             low_pass.proto != algo::motor::filter::FilterProto::Cheby2) ||
            low_pass.freq_hz >= 0.5F * low_pass.sample_freq_hz ||
            !m_q_current_low_pass.configure(low_pass)) {
            return false;
        }
    }
    return true;
}

void JointController::reset_q_current_filters()
{
    if (m_q_current_notch.configured()) m_q_current_notch.reset();
    if (m_q_current_low_pass.configured()) m_q_current_low_pass.reset();
}

float JointController::filter_q_current_target(float q_current_a, float limit_a)
{
    if (!algo::motor::basic_math::is_finite_float(q_current_a)) {
        reset_q_current_filters();
        return 0.0F;
    }
    const auto& config = config_.q_current_filter;
    if (!config.notch_enable && !config.low_pass_enable) {
        return q_current_a;
    }
    if (config.notch_enable) q_current_a = m_q_current_notch.update(q_current_a);
    if (config.low_pass_enable) q_current_a = m_q_current_low_pass.update(q_current_a);
    if (!algo::motor::basic_math::is_finite_float(q_current_a)) {
        reset_q_current_filters();
        return 0.0F;
    }
    // IIR transients can overshoot even when their input has already been limited.
    return algo::motor::basic_math::clamp_symmetric(q_current_a, limit_a);
}

void JointController::reset_loop_state()
{
    reset_q_current_filters();
    if (m_speed_q_current_low_pass.configured()) m_speed_q_current_low_pass.reset();
    m_position_velocity_loop.position_pid.reset();
    m_position_velocity_loop.speed_leso.reset();
    m_position_velocity_loop.position_set_rad = 0.0F;
    m_position_velocity_loop.target_position_rad = 0.0F;
    m_position_velocity_loop.target_velocity_limit_rad_s = 0.0F;
    m_position_velocity_loop.planned_position_rad = 0.0F;
    m_position_velocity_loop.planned_velocity_rad_s = 0.0F;
    m_position_velocity_loop.planned_acceleration_rad_s2 = 0.0F;
    m_position_velocity_loop.linear_target_position_rad = 0.0F;
    m_position_velocity_loop.linear_step_rad = 0.0F;
    m_position_velocity_loop.linear_velocity_rad_s = 0.0F;
    m_position_velocity_loop.linear_plan_active = false;
    m_position_velocity_loop.position_error_rad = 0.0F;
    m_position_velocity_loop.position_pid_input_rad = 0.0F;
    m_position_velocity_loop.position_output_rad_s = 0.0F;
    reset_position_deadzone();
    m_position_velocity_loop.speed_set_rad_s = 0.0F;
    m_position_velocity_loop.speed_output_a = 0.0F;
    m_mit_position_loop = {};
    m_mit_velocity_loop = {};
    m_torque_loop = {};
}

void JointController::reset_position_planner_to_feedback()
{
    auto& loop = m_position_velocity_loop;
    const float position_feedback = m_joint_angle_feedback.multi_turn_angle_filted;
    if (algo::motor::basic_math::is_finite_float(position_feedback)) {
        loop.target_planner.reset(position_feedback);
        loop.position_set_rad = position_feedback;
        loop.target_position_rad = position_feedback;
        loop.planned_position_rad = position_feedback;
        loop.linear_target_position_rad = position_feedback;
    } else {
        loop.target_planner.reset();
        loop.linear_target_position_rad = 0.0F;
    }
    loop.target_velocity_limit_rad_s = 0.0F;
    loop.planned_velocity_rad_s = 0.0F;
    loop.planned_acceleration_rad_s2 = 0.0F;
    loop.linear_step_rad = 0.0F;
    loop.linear_velocity_rad_s = 0.0F;
    loop.linear_plan_active = false;
}

void JointController::reset_position_deadzone()
{
    m_position_velocity_loop.deadzone_abs_error_rad = 0.0F;
    m_position_velocity_loop.deadzone_exit_threshold_rad = 0.0F;
    m_position_velocity_loop.deadzone_count = 0U;
    m_position_velocity_loop.deadzone_latched = false;
    m_position_velocity_loop.position_pid_input_rad = 0.0F;
}

void JointController::update_position_deadzone()
{
    const auto& config = config_.loop.position.deadzone;
    auto& loop = m_position_velocity_loop;

    if (!config.enable) {
        loop.deadzone_abs_error_rad = 0.0F;
        loop.deadzone_exit_threshold_rad = 0.0F;
        loop.deadzone_count = 0U;
        loop.deadzone_latched = false;
        loop.position_pid_input_rad = loop.position_error_rad;
        return;
    }

    loop.deadzone_abs_error_rad = fabsf(loop.position_error_rad);
    const float enter_threshold = config.error_deadzone_rad;
    loop.deadzone_exit_threshold_rad =
        config.error_deadzone_rad + config.error_hysteresis_rad;

    if (loop.deadzone_latched) {
        if (loop.deadzone_abs_error_rad > loop.deadzone_exit_threshold_rad) {
            loop.deadzone_latched = false;
        }
    } else if (loop.deadzone_abs_error_rad < enter_threshold) {
        loop.deadzone_latched = true;
    }

    if (loop.deadzone_latched) {
        if (fabsf(loop.position_output_rad_s) < config.output_velocity_threshold_rad_s) {
            if (loop.deadzone_count < loop.deadzone_count_max) {
                loop.deadzone_count++;
            }
        } else {
            loop.deadzone_count = 0U;
        }
    } else {
        loop.deadzone_count = 0U;
    }

    if (loop.deadzone_count >= loop.deadzone_count_max) {
        loop.position_pid_input_rad = 0.0F;
    } else {
        loop.position_pid_input_rad = loop.position_error_rad;
    }
}

float JointController::position_plan_sample_time_s() const
{
    const auto& target_plan = config_.loop.position.target_plan;
    if (target_plan.sample_time_s > 0.0F) {
        return target_plan.sample_time_s;
    }
    return 1.0F / config_.frequency_hz;
}

float JointController::resolve_position_plan_velocity(float velocity_cmd_rad_s) const
{
    constexpr float velocity_epsilon_rad_s = 0.001F;
    const auto& position_config = config_.loop.position;
    const auto& target_plan = position_config.target_plan;
    float max_velocity = target_plan.max_velocity_limit_rad_s;
    if (max_velocity <= 0.0F) {
        max_velocity = position_config.output_velocity_limit_rad_s;
    }

    float default_velocity = target_plan.default_max_velocity_rad_s;
    if (default_velocity < velocity_epsilon_rad_s) {
        default_velocity = max_velocity;
    }

    float velocity = fabsf(velocity_cmd_rad_s);
    if (velocity < velocity_epsilon_rad_s) {
        velocity = default_velocity;
    }
    if (velocity < velocity_epsilon_rad_s) {
        return 0.0F;
    }
    if (max_velocity > 0.0F) {
        velocity = algo::motor::basic_math::clamp_float(velocity,
                                                 velocity_epsilon_rad_s,
                                                 max_velocity);
    }
    return velocity;
}

bool JointController::update_s_curve_position_plan(
    const JointFinalCommand& command,
    algo::motor::trajectory::SCurveSample* sample)
{
    if (sample == nullptr) {
        return false;
    }

    auto& loop = m_position_velocity_loop;
    const float requested_max_velocity = fabsf(command.velocity_cmd_rad_s);
    const bool need_new_plan =
        (fabsf(command.position_cmd_rad - loop.target_position_rad) >
         config_.loop.position.target_plan.position_epsilon_rad) ||
        (fabsf(requested_max_velocity - loop.target_velocity_limit_rad_s) >
         config_.loop.position.target_plan.velocity_epsilon_rad_s);

    if (need_new_plan) {
        if (!loop.target_planner.plan(command.position_cmd_rad,
                                      requested_max_velocity)) {
            return false;
        }
        loop.target_position_rad = command.position_cmd_rad;
        loop.target_velocity_limit_rad_s = requested_max_velocity;
    }

    *sample = loop.target_planner.update();
    return true;
}

bool JointController::update_linear_position_plan(
    const JointFinalCommand& command,
    algo::motor::trajectory::SCurveSample* sample)
{
    constexpr float position_epsilon_rad = 0.001F;
    if (sample == nullptr) {
        return false;
    }

    auto& loop = m_position_velocity_loop;
    const bool need_new_plan =
        fabsf(command.position_cmd_rad - loop.target_position_rad) >
        position_epsilon_rad;

    if (need_new_plan) {
        const float sample_time_s = position_plan_sample_time_s();
        const float velocity_abs =
            resolve_position_plan_velocity(command.velocity_cmd_rad_s);
        const float start_position =
            algo::motor::basic_math::is_finite_float(loop.planned_position_rad)
                ? loop.planned_position_rad
                : m_joint_angle_feedback.multi_turn_angle_filted;
        if ((sample_time_s <= 0.0F) ||
            (velocity_abs <= 0.0F) ||
            !algo::motor::basic_math::is_finite_float(start_position)) {
            return false;
        }

        const float delta = command.position_cmd_rad - start_position;
        loop.planned_position_rad = start_position;
        loop.linear_target_position_rad = command.position_cmd_rad;
        loop.target_position_rad = command.position_cmd_rad;
        loop.target_velocity_limit_rad_s = velocity_abs;
        loop.planned_acceleration_rad_s2 = 0.0F;
        if (fabsf(delta) <= position_epsilon_rad) {
            loop.planned_position_rad = command.position_cmd_rad;
            loop.planned_velocity_rad_s = 0.0F;
            loop.linear_step_rad = 0.0F;
            loop.linear_velocity_rad_s = 0.0F;
            loop.linear_plan_active = false;
        } else {
            const float step_abs = velocity_abs * sample_time_s;
            if (!algo::motor::basic_math::is_finite_float(step_abs) ||
                (step_abs <= 0.0F)) {
                return false;
            }
            const float direction = (delta >= 0.0F) ? 1.0F : -1.0F;
            loop.linear_step_rad = direction * step_abs;
            loop.linear_velocity_rad_s = direction * velocity_abs;
            loop.planned_velocity_rad_s = loop.linear_velocity_rad_s;
            loop.linear_plan_active = true;
        }
    }

    if (loop.linear_plan_active) {
        const float next_position =
            loop.planned_position_rad + loop.linear_step_rad;
        const bool reach_target =
            (loop.linear_step_rad >= 0.0F)
                ? (next_position >= loop.linear_target_position_rad)
                : (next_position <= loop.linear_target_position_rad);
        if (reach_target) {
            loop.planned_position_rad = loop.linear_target_position_rad;
            loop.planned_velocity_rad_s = 0.0F;
            loop.linear_step_rad = 0.0F;
            loop.linear_velocity_rad_s = 0.0F;
            loop.linear_plan_active = false;
        } else {
            loop.planned_position_rad = next_position;
            loop.planned_velocity_rad_s = loop.linear_velocity_rad_s;
        }
    } else {
        loop.planned_velocity_rad_s = 0.0F;
    }
    loop.planned_acceleration_rad_s2 = 0.0F;

    sample->position_rad = loop.planned_position_rad;
    sample->velocity_rad_s = loop.planned_velocity_rad_s;
    sample->acceleration_rad_s2 = loop.planned_acceleration_rad_s2;
    sample->elapsed_s = 0.0F;
    sample->active = loop.linear_plan_active;
    sample->finished = !loop.linear_plan_active;
    return true;
}

float JointController::run_torque_mode(const JointFinalCommand& command)
{
    m_torque_loop.target_current_a = command.torque_cmd_a;
    const float q_current_a = config_.invert_sign * command.torque_cmd_a;
    m_torque_loop.q_current_output_a =
        algo::motor::basic_math::clamp_symmetric(q_current_a, config_.loop.torque.q_current_limit_a);
    return m_torque_loop.q_current_output_a;
}

float JointController::run_velocity_mode(const JointFinalCommand& command)
{
    const float gear_ratio = m_motor_basic_config.gear_ratio;
    m_position_velocity_loop.speed_set_rad_s = config_.invert_sign * command.velocity_cmd_rad_s * gear_ratio;
    m_position_velocity_loop.speed_output_a = m_position_velocity_loop.speed_leso.update(m_position_velocity_loop.speed_set_rad_s,
                                                                                         m_velocity_feedback.angular_velocity);

    // Keep speed_output_a as the raw LESO diagnostic; filter only the iq target.
    return filter_speed_q_current_target(m_position_velocity_loop.speed_output_a);
}

float JointController::run_position_mode(const JointFinalCommand& command)
{
    auto& loop = m_position_velocity_loop;
    algo::motor::trajectory::SCurveSample plan_sample {};
    bool plan_valid = false;
    if (fabsf(m_entry_stop_velocity) > 0.0F) {
        const float dt = 1.0F / config_.frequency_hz;
        const float step = config_.loop.position.target_plan.max_acceleration_rad_s2 * dt;
        const float old = m_entry_stop_velocity;
        m_entry_stop_velocity = (fabsf(old) <= step) ? 0.0F : old - copysignf(step, old);
        plan_sample.position_rad = loop.planned_position_rad + (old + m_entry_stop_velocity) * 0.5F * dt;
        plan_sample.velocity_rad_s = m_entry_stop_velocity;
        plan_sample.acceleration_rad_s2 = (m_entry_stop_velocity - old) / dt;
        if (m_entry_stop_velocity == 0.0F) {
            loop.target_planner.reset(plan_sample.position_rad);
            loop.target_position_rad = plan_sample.position_rad;
        }
        plan_valid = true;
    } else switch (config_.loop.position.target_plan_mode) {
    case PositionTargetPlanMode::Linear:
        plan_valid = update_linear_position_plan(command, &plan_sample);
        break;
    case PositionTargetPlanMode::SCurve:
    default:
        plan_valid = update_s_curve_position_plan(command, &plan_sample);
        break;
    }
    if (!plan_valid) {
        invalid_ = true;
        return 0.0F;
    }

    loop.planned_position_rad = plan_sample.position_rad;
    loop.planned_velocity_rad_s = plan_sample.velocity_rad_s;
    loop.planned_acceleration_rad_s2 = plan_sample.acceleration_rad_s2;
    m_position_velocity_loop.position_set_rad = plan_sample.position_rad;
    m_position_velocity_loop.position_error_rad = m_position_velocity_loop.position_set_rad - m_joint_angle_feedback.multi_turn_angle_filted;
    update_position_deadzone();
    m_position_velocity_loop.position_output_rad_s = m_position_velocity_loop.position_pid.update(m_position_velocity_loop.position_pid_input_rad);
    if (config_.loop.position.velocity_feedforward_enable) {
        // Planner velocity is in joint coordinates; run_velocity_mode handles
        // the motor direction and gear ratio after the combined speed limit.
        loop.position_output_rad_s += loop.planned_velocity_rad_s;
    }
    m_position_velocity_loop.position_output_rad_s = algo::motor::basic_math::clamp_symmetric(m_position_velocity_loop.position_output_rad_s,
                                                                                config_.loop.position.output_velocity_limit_rad_s);

    JointFinalCommand velocity_command {};
    velocity_command.velocity_cmd_rad_s = m_position_velocity_loop.position_output_rad_s;
    float q_current_a = run_velocity_mode(velocity_command);
    if (config_.loop.position.external_torque_q_current_limit_enable) {
        q_current_a = algo::motor::basic_math::clamp_symmetric(q_current_a,
                                                        fabsf(command.torque_cmd_a));
    }
    return q_current_a;
}

float JointController::run_mit_mode(const JointFinalCommand& command)
{
    const auto& mit_config = config_.loop.mit;
    m_mit_position_loop.target_position_rad = command.position_cmd_rad;
    m_mit_position_loop.position_error_rad = command.position_cmd_rad - m_joint_angle_feedback.multi_turn_angle_filted;
    m_mit_position_loop.kp = algo::motor::basic_math::linear_map_update(mit_config.kp_map, command.mit_kp_cmd);

    m_mit_velocity_loop.target_velocity_rad_s = command.velocity_cmd_rad_s;
    m_mit_velocity_loop.velocity_feedback_rad_s = m_velocity_feedback.output_angular_velocity;
    m_mit_velocity_loop.velocity_error_rad_s = command.velocity_cmd_rad_s - m_mit_velocity_loop.velocity_feedback_rad_s;
    m_mit_velocity_loop.kd = algo::motor::basic_math::linear_map_update(mit_config.kd_map, command.mit_kd_cmd);

    const auto output = algo::interaction::spring_damper(m_mit_position_loop.position_error_rad,
        m_mit_velocity_loop.velocity_error_rad_s,m_mit_position_loop.kp,m_mit_velocity_loop.kd,
        command.torque_cmd_a,mit_config.position_output_limit_a,mit_config.velocity_output_limit_a,
        mit_config.q_current_limit_a);
    m_mit_position_loop.output_a=output.position;
    m_mit_velocity_loop.output_a=output.damping;
    return config_.invert_sign*output.total;
}

}  // namespace algo::motor
