#include <algo/motor/joint_target.h>

#include <algo/math/basic_math.h>

#include <math.h>

namespace {

constexpr uint8_t kTorqueProtectReasonAbs = 0x01U;
constexpr float kMinDurationS = 1.0e-6F;
constexpr float kMaxDurationS = 1.0F;

/*
 * 限制插值时长和指令超时配置，避免非有限值或非正值。
 */
float clamp_duration_s(float duration_s)
{
    const float safe_duration = algo::motor::basic_math::is_finite_float(duration_s)
                                    ? duration_s
                                    : kMinDurationS;
    return algo::motor::basic_math::clamp_float(safe_duration,
                                         kMinDurationS,
                                         kMaxDurationS);
}

/*
 * 计算外部指令超时时间。
 * timeout 不能短于期望指令周期，否则会在正常收包间隔内误触发超时保持。
 */
float external_order_timeout_s(const algo::motor::JointTargetConfig& config)
{
    const float period_s = clamp_duration_s(config.external_order_period_s);
    const float timeout_s = clamp_duration_s(config.external_order_timeout_s);
    return (timeout_s < period_s) ? period_s : timeout_s;
}

/*
 * 清除当前目标中的力矩命令。
 * 力矩保护触发后调用，避免后续 MIT 或力矩模式继续使用异常前馈。
 */
void clear_torque_command(algo::motor::JointTargetContext* target)
{
    target->external_order.current_order.torque_a = 0.0F;
    target->external_order.last_order.torque_a = 0.0F;
    target->final_command.torque_cmd_a = 0.0F;
    target->torque_order_a = 0.0F;
}

/*
 * 因力矩保护停止接收外部目标。
 * 该函数记录保护原因并清空当前/上一帧有效标志。
 */
void stop_external_order_by_torque_protect(const algo::motor::JointTargetOrder& order,
                                           uint8_t reason,
                                           algo::motor::JointTargetContext* target)
{
    auto& external_order = target->external_order;
    external_order.torque_order_protect.stop_active = true;
    external_order.torque_order_protect.stop_reason = reason;
    external_order.torque_order_protect.host_timestamp_us = order.host_timestamp_us;
    external_order.torque_order_protect.torque_a = order.torque_a;
    external_order.current_order_valid = false;
    external_order.last_order_valid = false;
    external_order.refresh_flag = false;
    external_order.duplicate_order_flag = false;
    clear_torque_command(target);
}

/*
 * 判断新收到的目标是否是同一帧重复刷新。
 * host_timestamp_us 允许缺省为 0；缺省字段不参与重复判断。
 */
bool duplicate_order(const algo::motor::JointTargetOrder& order,
                     const algo::motor::JointTargetOrder& current_order)
{
    const bool duplicate_timestamp =
        (order.host_timestamp_us != 0U) &&
        (order.host_timestamp_us == current_order.host_timestamp_us);
    return duplicate_timestamp;
}

/*
 * 根据当前控制周期更新外部命令年龄。
 * 只有 MIT 断包处理依赖该年龄，调用方直接传入 run_count 和控制频率。
 */
void update_external_order_age(uint32_t run_count,
                               float control_frequency_hz,
                               algo::motor::JointTargetContext* target)
{
    auto& external_order = target->external_order;
    if (!external_order.current_order_valid) {
        external_order.current_order_age_s = 0.0F;
        return;
    }

    if (!algo::motor::basic_math::is_finite_float(control_frequency_hz) ||
        (control_frequency_hz <= 0.0F)) {
        return;
    }

    const uint32_t elapsed_count = run_count - external_order.current_rx_run_count;
    external_order.current_order_age_s =
        static_cast<float>(elapsed_count) / control_frequency_hz;
}

// MIT 的速度、电流前馈及 kp/kd 每周期直接使用最新有效命令。
void set_direct_mit_fields(const algo::motor::JointTargetOrder& order,
                           algo::motor::JointTargetContext* target)
{
    target->final_command.velocity_cmd_rad_s = order.velocity_rad_s;
    target->final_command.torque_cmd_a = order.torque_a;
    target->final_command.mit_kp_cmd = static_cast<float>(order.mit_kp);
    target->final_command.mit_kd_cmd = static_cast<float>(order.mit_kd);
}

/*
 * 先将旧段推进到当前控制时刻，便于新指令从同一位置和斜率接续。
 * 到达终点后保持位置，不按速度指令外推；下一周期的接续斜率归零。
 */
void advance_mit_position(uint32_t run_count,
                          float control_frequency_hz,
                          algo::motor::JointTargetContext* target)
{
    auto& interp = target->hermite_interp;
    if (!interp.segment_valid) {
        interp.last_sample.velocity = 0.0F;
        return;
    }

    const uint32_t elapsed_count = run_count - interp.segment_start_run_count;
    const float elapsed_s = static_cast<float>(elapsed_count) / control_frequency_hz;
    algo::motor::trajectory::CubicHermiteSample sample {};
    if (!algo::motor::trajectory::cubic_hermite_interpolate(interp.segment, elapsed_s, &sample)) {
        interp.segment_valid = false;
        interp.last_sample.velocity = 0.0F;
        return;
    }

    // 若本次刷新已经晚于终点，之前的目标实际处于静止保持状态。
    if (elapsed_s > interp.segment.duration_s) {
        sample.velocity = 0.0F;
    }
    interp.last_sample = sample;
    interp.segment_valid = sample.ratio < 1.0F;
    target->final_command.position_cmd_rad = sample.position;
}

/*
 * 名义周期固定来自配置，不随主站时间戳或收包抖动拉伸。
 * 外部速度作为 Hermite 终点斜率，但插值导数不写入 Kd 速度目标。
 */
void start_mit_position_segment(uint32_t run_count,
                                float position_feedback_rad,
                                algo::motor::JointTargetContext* target)
{
    auto& interp = target->hermite_interp;
    const auto& order = target->external_order.current_order;
    if (!interp.init_finish) {
        interp.last_sample = {};
        interp.last_sample.position = position_feedback_rad;
        interp.init_finish = true;
    }

    interp.segment = {
        interp.last_sample.position,
        interp.last_sample.velocity,
        order.position_rad,
        order.velocity_rad_s,
        clamp_duration_s(target->config.external_order_period_s),
    };
    interp.segment_start_run_count = run_count;
    interp.segment_valid = true;
    interp.last_sample.ratio = 0.0F;
    target->final_command.position_cmd_rad = interp.segment.position_start;
}

/*
 * 从外部命令刷新位置模式目标缓存。
 * 位置模式取外部目标角度，并保留速度字段作为位置规划最大速度。
 */
void refresh_position_target_from_external(algo::motor::JointTargetContext* target)
{
    target->position_rad = target->external_order.current_order.position_rad;
    target->speed_order_rad_s = target->external_order.current_order.velocity_rad_s;
}

/*
 * 应用位置模式目标。
 * 最终速度字段表示本次位置规划的最大速度，不是瞬时速度闭环目标。
 */
void apply_position_target(algo::motor::JointTargetContext* target)
{
    target->final_command.position_cmd_rad = target->position_rad;
    target->final_command.velocity_cmd_rad_s = target->speed_order_rad_s;
    target->final_command.torque_cmd_a =
        target->external_order.current_order.torque_a;
}

}  // namespace

namespace algo::motor {

/*
 * 初始化 joint_target 上下文并保存配置。
 * 配置合法性由上层配置阶段保证，本函数只清空运行态并建立反馈保持目标。
 */
bool joint_target_configure(const JointTargetConfig& config, JointTargetContext* target)
{
    if (target == nullptr) {
        return false;
    }

    *target = JointTargetContext {};
    target->config = config;
    target->configured = true;
    joint_target_hold_feedback_command(0.0F, target);
    return true;
}

/*
 * 重置 joint_target 运行态。
 * 保留已保存的配置和 configured 标志，清空命令、最终目标和位置目标缓存。
 */
void joint_target_reset(JointTargetContext* target)
{
    if (target == nullptr) {
        return;
    }

    const JointTargetConfig config = target->config;
    const bool configured = target->configured;
    *target = JointTargetContext {};
    target->config = config;
    target->configured = configured;
    joint_target_hold_feedback_command(0.0F, target);
}

/*
 * 写入最新外部目标命令。
 * 该接口只接收 joint_ctrl 整理后的快照，不解析 CANopen 字节流。
 */
bool joint_target_update_external_order(const JointTargetOrder& order,
                                        uint32_t run_count,
                                        JointTargetContext* target)
{
    if ((target == nullptr) ||
        !algo::motor::basic_math::is_finite_float(order.position_rad) ||
        !algo::motor::basic_math::is_finite_float(order.velocity_rad_s) ||
        !algo::motor::basic_math::is_finite_float(order.torque_a)) {
        return false;
    }

    auto& external_order = target->external_order;
    if (external_order.torque_order_protect.stop_active) {
        clear_torque_command(target);
        return false;
    }

    if (fabsf(order.torque_a) > target->config.torque_abs_limit_a) {
        stop_external_order_by_torque_protect(order,
                                              kTorqueProtectReasonAbs,
                                              target);
        return false;
    }

    if (external_order.current_order_valid) {
        if (duplicate_order(order, external_order.current_order)) {
            external_order.duplicate_order_flag = true;
            // 同时间戳的载荷修订仍接续位置段，但不续期超时计数。
            external_order.refresh_flag = external_order.refresh_flag ||
                (order.position_rad != external_order.current_order.position_rad) ||
                (order.velocity_rad_s != external_order.current_order.velocity_rad_s);
            external_order.current_order = order;
            return true;
        }

        external_order.duplicate_order_flag = false;
        external_order.last_order = external_order.current_order;
        external_order.last_rx_run_count = external_order.current_rx_run_count;
        external_order.last_order_valid = true;
        external_order.delta_host_timestamp_us =
            algo::motor::basic_math::timestamp_delta_us(
                order.host_timestamp_us,
                external_order.last_order.host_timestamp_us);
    } else {
        external_order.last_order = JointTargetOrder {};
        external_order.last_order_valid = false;
        external_order.last_rx_run_count = 0U;
        external_order.duplicate_order_flag = false;
        external_order.delta_host_timestamp_us = 0;
    }

    external_order.current_order = order;
    external_order.current_order_valid = true;
    external_order.current_rx_run_count = run_count;
    external_order.current_order_age_s = 0.0F;
    external_order.refresh_flag = true;
    return true;
}

/*
 * 将最终目标强制贴住当前反馈。
 * 常用于没有有效外部目标、disable 或需要避免再次 enable 跳变的场景。
 */
void joint_target_hold_feedback_command(float position_feedback_rad,
                                        JointTargetContext* target)
{
    if ((target == nullptr) ||
        !algo::motor::basic_math::is_finite_float(position_feedback_rad)) {
        return;
    }

    target->final_command.position_cmd_rad = position_feedback_rad;
    target->final_command.velocity_cmd_rad_s = 0.0F;
    target->final_command.torque_cmd_a = 0.0F;
    target->final_command.mit_kp_cmd = 0.0F;
    target->final_command.mit_kd_cmd = 0.0F;
    target->speed_order_rad_s = 0.0F;
    target->torque_order_a = 0.0F;
    target->position_rad = position_feedback_rad;
    target->hermite_interp = JointHermiteInterp {};
}

/*
 * 模式切换时清空外部命令缓存，并把最终目标和位置缓存同步到当前反馈点。
 */
void joint_target_reset_mode_switch_hold(float position_feedback_rad,
                                         JointTargetContext* target)
{
    if ((target == nullptr) ||
        !algo::motor::basic_math::is_finite_float(position_feedback_rad)) {
        return;
    }

    target->external_order = JointExternalOrder {};
    joint_target_hold_feedback_command(position_feedback_rad, target);
}

/*
 * STOP/Disabled 状态下将最终目标同步到当前反馈，清零速度、电流前馈和增益。
 */
void joint_target_sync_stop_target(float position_feedback_rad,
                                   JointTargetContext* target)
{
    joint_target_hold_feedback_command(position_feedback_rad, target);
}

/*
 * MIT 位置按名义周期插值，其余字段直接取最新有效外部命令。
 * 超时后直接清零速度和电流前馈，位置完成当前段后保持，kp/kd 保持最新外部值。
 */
void joint_target_refresh_mit(uint32_t run_count,
                              float control_frequency_hz,
                              float position_feedback_rad,
                              JointTargetContext* target)
{
    if ((target == nullptr) ||
        !algo::motor::basic_math::is_finite_float(control_frequency_hz) ||
        (control_frequency_hz <= 0.0F) ||
        !algo::motor::basic_math::is_finite_float(position_feedback_rad)) {
        return;
    }

    update_external_order_age(run_count, control_frequency_hz, target);
    if (!target->external_order.current_order_valid) {
        joint_target_hold_feedback_command(position_feedback_rad, target);
        return;
    }

    auto& external_order = target->external_order;
    const bool timed_out =
        external_order.current_order_age_s >= external_order_timeout_s(target->config);
    advance_mit_position(run_count, control_frequency_hz, target);
    if (!timed_out && (external_order.refresh_flag || !target->hermite_interp.init_finish)) {
        start_mit_position_segment(run_count, position_feedback_rad, target);
    }
    external_order.refresh_flag = false;
    set_direct_mit_fields(external_order.current_order, target);
    if (timed_out) {
        target->final_command.velocity_cmd_rad_s = 0.0F;
        target->final_command.torque_cmd_a = 0.0F;
    }
}

/*
 * 刷新位置模式目标。
 * 取外部位置目标，后续轨迹规划由 joint_ctrl 的位置模式负责。
 */
void joint_target_refresh_position(float position_feedback_rad,
                                   JointTargetContext* target)
{
    if ((target == nullptr) ||
        !algo::motor::basic_math::is_finite_float(position_feedback_rad)) {
        return;
    }
    if (!target->external_order.current_order_valid) {
        joint_target_hold_feedback_command(position_feedback_rad, target);
        return;
    }

    refresh_position_target_from_external(target);
    apply_position_target(target);
}

/*
 * 刷新速度模式目标。
 * 速度模式直接使用最新外部速度命令。
 */
void joint_target_refresh_velocity(float position_feedback_rad,
                                   JointTargetContext* target)
{
    if ((target == nullptr) ||
        !algo::motor::basic_math::is_finite_float(position_feedback_rad)) {
        return;
    }
    if (!target->external_order.current_order_valid) {
        joint_target_hold_feedback_command(position_feedback_rad, target);
        return;
    }

    target->speed_order_rad_s = target->external_order.current_order.velocity_rad_s;
    target->final_command.velocity_cmd_rad_s = target->speed_order_rad_s;
}

/*
 * 刷新力矩模式目标。
 * 力矩模式直接使用最新外部力矩目标。
 */
void joint_target_refresh_torque(float position_feedback_rad,
                                 JointTargetContext* target)
{
    if ((target == nullptr) ||
        !algo::motor::basic_math::is_finite_float(position_feedback_rad)) {
        return;
    }
    if (!target->external_order.current_order_valid) {
        joint_target_hold_feedback_command(position_feedback_rad, target);
        return;
    }

    target->torque_order_a = target->external_order.current_order.torque_a;
    target->final_command.torque_cmd_a = target->torque_order_a;
}

/*
 * 获取最终目标输出的只读指针。
 * joint_ctrl 在模式控制中读取该结构，不应通过返回指针修改内部状态。
 */
const JointFinalCommand* joint_target_final_command(const JointTargetContext* target)
{
    return (target != nullptr) ? &target->final_command : nullptr;
}

/*
 * 获取外部命令缓存状态的只读指针。
 * 供 joint_ctrl 或调试逻辑判断当前是否有有效外部目标和断包年龄。
 */
const JointExternalOrder* joint_target_external_order(const JointTargetContext* target)
{
    return (target != nullptr) ? &target->external_order : nullptr;
}

}  // namespace algo::motor
