#include <algo/motor/feedback.h>
#include <algo/filter/filter_sys.h>

namespace algo::motor {
namespace {
constexpr float kOutputAngularAccelerationFilterAlpha = 0.01F;
constexpr float kJointAngularVelocityFilterAlpha = 0.005F;
bool apply_affine2d(const algo::motor::MotorAffine2DCalibData& calib,
                    float sensor1,
                    float sensor2,
                    float* calibrated1,
                    float* calibrated2)
{
    if (!calib.valid || (calibrated1 == nullptr) || (calibrated2 == nullptr)) {
        return false;
    }
    const float x = sensor1 - calib.center[0];
    const float y = sensor2 - calib.center[1];
    *calibrated1 = (calib.transform[0] * x) + (calib.transform[1] * y);
    *calibrated2 = (calib.transform[2] * x) + (calib.transform[3] * y);
    return algo::motor::basic_math::is_finite_float(*calibrated1) &&
           algo::motor::basic_math::is_finite_float(*calibrated2);
}

// 输出 Hall 沿用旧标定：分别做零偏/幅值归一化，并限制到 [-1, 1]。
float apply_tmr_calib(float input, const algo::motor::MotorTmrCalibData& calib)
{
    if (!calib.valid) {
        return input;
    }
    const float amp = (calib.amp > algo::motor::basic_math::kMinTmrAmp) ? calib.amp : 1.0F;
    return algo::motor::basic_math::clamp_float((input - calib.offset) / amp, -1.0F, 1.0F);
}

// 用硬件默认参数把 ADC 端口电压换算成未标定的物理量 Iraw/Vraw。
float apply_raw_convert(float input, const algo::motor::MotorLinearCalibData& calib)
{
    if (!calib.valid) {
        return input;
    }
    return (input - calib.offset) * calib.gain;
}

// 对未标定物理量做普通直线校正。
float apply_linear_calib(float input, const algo::motor::MotorLinearCalibData& calib)
{
    if (!calib.valid) {
        return input;
    }
    return (input * calib.gain) + calib.offset;
}


}  // namespace

bool current_ctrl_feedback_init(const MotorElecCalibData& elec_raw_convert_data,
                                const MotorElecCalibData& elec_calib_data,
                                MotorElecInfoFeedback* elec_info_feedback,
                                MotorElecAngleFeedback* elec_angle_feedback,
                                float current_filter_alpha)
{
    if ((elec_info_feedback == nullptr) &&
        (elec_angle_feedback == nullptr)) {
        return false;
    }

    if (elec_info_feedback != nullptr) {
        elec_info_feedback->iu_raw = 0.0F;
        elec_info_feedback->iv_raw = 0.0F;
        elec_info_feedback->iw_raw = 0.0F;
        elec_info_feedback->vbus_raw = 0.0F;
        elec_info_feedback->iu_calib = 0.0F;
        elec_info_feedback->iv_calib = 0.0F;
        elec_info_feedback->iw_calib = 0.0F;
        elec_info_feedback->vbus_calib = 0.0F;
        elec_info_feedback->iu_filter = 0.0F;
        elec_info_feedback->iv_filter = 0.0F;
        elec_info_feedback->iw_filter = 0.0F;
        elec_info_feedback->vbus_filter = 0.0F;

        auto& internal = elec_info_feedback->internal;
        internal.iu_raw_convert_data = elec_raw_convert_data.iu_calib;
        internal.iv_raw_convert_data = elec_raw_convert_data.iv_calib;
        internal.iw_raw_convert_data = elec_raw_convert_data.iw_calib;
        internal.vbus_raw_convert_data = elec_raw_convert_data.vbus_calib;
        internal.iu_calib_data = elec_calib_data.iu_calib;
        internal.iv_calib_data = elec_calib_data.iv_calib;
        internal.iw_calib_data = elec_calib_data.iw_calib;
        internal.vbus_calib_data = elec_calib_data.vbus_calib;
        internal.current_filter_alpha =
            algo::motor::basic_math::is_finite_float(current_filter_alpha) ?
                algo::motor::basic_math::clamp_float(current_filter_alpha, 0.0F, 1.0F) :
                0.9F;
        internal.vbus_filter_alpha = 0.08F;
        internal.has_filter_output = false;
    }

    if (elec_angle_feedback != nullptr) {
        elec_angle_feedback->input_tmr1 = 0.0F;
        elec_angle_feedback->input_tmr2 = 0.0F;
        elec_angle_feedback->mech_angle = 0.0F;
        elec_angle_feedback->elec_angle = 0.0F;
        elec_angle_feedback->elec_angle_calib = 0.0F;
        elec_angle_feedback->mech_angle_calib = 0.0F;

        auto& internal = elec_angle_feedback->internal;
        internal.calibrated_tmr1 = 0.0F;
        internal.calibrated_tmr2 = 0.0F;
        internal.angle_range = 0.0F;
        internal.zeroed_angle = 0.0F;
        internal.angle_offset = 0.0F;
        internal.interp_config.order = algo::motor::basic_math::LagrangeInterpOrder::Second;
        internal.interp_config.x_min = 0.0F;
        internal.interp_config.x_max = algo::motor::basic_math::kTwoPi;
        internal.interp_config.point_count = kElecAngleCalibPointCount;
    }

    return true;
}

// 初始化 joint_ctrl 使用的速度反馈和输出端关节角反馈状态。
bool joint_ctrl_feedback_init(const MotorBasicConfig& motor_basic_config,
                              MotorVelocityFeedback* velocity_feedback,
                              MotorJointAngleFeedback* joint_angle_feedback)
{
    if ((velocity_feedback == nullptr) &&
        (joint_angle_feedback == nullptr)) {
        return false;
    }
    if ((velocity_feedback != nullptr) &&
        (!motor_basic_config.valid ||
         !algo::motor::basic_math::is_finite_float(motor_basic_config.gear_ratio) ||
         (motor_basic_config.gear_ratio <= 0.0F))) {
        return false;
    }

    if (velocity_feedback != nullptr) {
        velocity_feedback->angular_velocity = 0.0F;
        velocity_feedback->angular_velocity_raw = 0.0F;
        velocity_feedback->angular_acceleration = 0.0F;
        velocity_feedback->output_angular_velocity = 0.0F;
        velocity_feedback->output_equivalent_multi_turn_angle_rad = 0.0F;
        velocity_feedback->output_equivalent_multi_turn_angle_filted_rad = 0.0F;
        velocity_feedback->output_acc = 0.0F;
        velocity_feedback->output_acc_filted = 0.0F;

        auto& internal = velocity_feedback->internal;
        internal.gear_ratio =
            (motor_basic_config.gear_ratio > 0.0F) ? motor_basic_config.gear_ratio : 1.0F;
        internal.last_angle = 0.0F;
        internal.delta_angle = 0.0F;
        internal.last_angular_velocity = 0.0F;
        internal.filtered_angular_velocity = 0.0F;
        internal.last_output_angular_velocity = 0.0F;
        internal.output_acc_filted = 0.0F;
        internal.has_last_angle = false;
    }

    if (joint_angle_feedback != nullptr) {
        joint_angle_feedback->output_tmr1 = 0.0F;
        joint_angle_feedback->output_tmr2 = 0.0F;
        joint_angle_feedback->single_turn_angle_raw = 0.0F;
        joint_angle_feedback->single_turn_angle_calib = 0.0F;
        joint_angle_feedback->multi_turn_angle = 0.0F;
        joint_angle_feedback->multi_turn_angle_filted = 0.0F;
        joint_angle_feedback->angular_velocity = 0.0F;
        joint_angle_feedback->turn_count = 0;

        auto& internal = joint_angle_feedback->internal;
        internal.calibrated_tmr1 = 0.0F;
        internal.calibrated_tmr2 = 0.0F;
        internal.angle_offset = 0.0F;
        internal.single_turn_angle_zero_calib = 0.0F;
        internal.last_single_turn_angle_zero_calib = 0.0F;
        internal.multi_turn_angle_raw = 0.0F;
        internal.jump_threshold = algo::motor::basic_math::kTwoPi * 0.95F;
        internal.has_last_single_turn_angle = false;
        internal.interp_config.order = algo::motor::basic_math::LagrangeInterpOrder::Second;
        internal.interp_config.x_min = 0.0F;
        internal.interp_config.x_max = algo::motor::basic_math::kTwoPi;
        internal.interp_config.point_count = kOutputAngleCalibPointCount;
    }

    return true;
}

// 根据输入端两路 TMR 原始采样计算电机机械角，并保存 TMR 标定后的中间值。
bool update_sensor_angle(float input_tmr_v1,
                         float input_tmr_v2,
                         const MotorSensorCalibData& sensor_calib_data,
                         const MotorInputTmrAngleConfig& angle_config,
                         MotorElecAngleFeedback* feedback)
{
    auto& internal = feedback->internal;
    feedback->input_tmr1 = input_tmr_v1;
    feedback->input_tmr2 = input_tmr_v2;

    if (!apply_affine2d(sensor_calib_data.input_sensor_calib,
                        input_tmr_v1, input_tmr_v2,
                        &internal.calibrated_tmr1,
                        &internal.calibrated_tmr2)) {
        return false;
    }

    const float angle_y =
        angle_config.swap_channels ? internal.calibrated_tmr2 : internal.calibrated_tmr1;
    const float angle_x =
        angle_config.swap_channels ? internal.calibrated_tmr1 : internal.calibrated_tmr2;
    feedback->mech_angle = algo::motor::basic_math::normalize_angle_0_to_2pi(
        atan2f(angle_y, angle_x));
    return true;
}

// 对输出端单圈角做非线性标定，输出结果写入关节角反馈结构体。
bool joint_angle_calibrate(float raw_angle_rad,
                           const MotorSensorCalibData& sensor_calib_data,
                           MotorJointAngleFeedback* feedback)
{
    if (feedback == nullptr) {
        return false;
    }
    if (!algo::motor::basic_math::is_finite_float(raw_angle_rad)) {
        return false;
    }
    auto& internal = feedback->internal;
    const auto* view = sensor_calib_data.output_angle_calib;
    const auto* table = view ? view->data : nullptr;
    if (!table || !table->valid) {
        feedback->single_turn_angle_calib = raw_angle_rad;
        return true;
    }

    const auto& angle_calib = *table;
    const float normalized_angle =
        algo::motor::basic_math::normalize_angle_0_to_2pi(raw_angle_rad);
    internal.angle_offset = 0.0F;
    if (algo::motor::basic_math::uniform_lagrange_interpolate(internal.interp_config,
                                                       angle_calib.angle_offset_rad,
                                                       normalized_angle,
                                                       &internal.angle_offset)) {
        feedback->single_turn_angle_calib = normalized_angle + internal.angle_offset;
    } else {
        feedback->single_turn_angle_calib = raw_angle_rad;
    }
    return true;
}

// 将机械角转换为电角度，并按电角度标定表进行误差补偿。
bool elec_angle_calibrate(const MotorSensorCalibData& sensor_calib_data,
                          const MotorBasicConfig& motor_basic_config,
                          MotorElecAngleFeedback* feedback)
{
    auto& internal = feedback->internal;
    const uint16_t pole_pairs = motor_basic_config.pole_pairs;

    feedback->elec_angle =
        algo::motor::basic_math::normalize_angle_0_to_2pi(feedback->mech_angle) *
        static_cast<float>(pole_pairs);
    float elec_angle_calib = feedback->elec_angle;

    const auto& angle_calib = sensor_calib_data.elec_angle_calib;
    if (angle_calib.valid) {
        internal.angle_range =
            algo::motor::basic_math::kTwoPi * static_cast<float>(pole_pairs);
        internal.zeroed_angle = feedback->elec_angle - angle_calib.zero_offset_rad;
        if (internal.zeroed_angle < 0.0F) {
            internal.zeroed_angle += internal.angle_range;
        }
        if (internal.zeroed_angle > internal.angle_range) {
            internal.zeroed_angle = fmodf(internal.zeroed_angle, internal.angle_range);
        }

        internal.interp_config.x_min = 0.0F;
        internal.interp_config.x_max = internal.angle_range;
        internal.interp_config.point_count = kElecAngleCalibPointCount;
        internal.angle_offset = 0.0F;
        if (algo::motor::basic_math::uniform_lagrange_interpolate(internal.interp_config,
                                                           angle_calib.angle_offset_rad,
                                                           internal.zeroed_angle,
                                                           &internal.angle_offset)) {
            elec_angle_calib = internal.zeroed_angle + internal.angle_offset;
        }
    }

    feedback->elec_angle_calib = elec_angle_calib;
    feedback->mech_angle_calib = elec_angle_calib / static_cast<float>(pole_pairs);
    return true;
}

// 根据连续机械角更新角速度、角加速度，以及减速后的输出端等效多圈角原始值和滤波值。
bool update_velocity(float raw_angle_rad,
                     float refresh_freq,
                     float gear_ratio,
                     float invert_sign,
                     float multi_turn_angle_filter_alpha,
                     MotorVelocityFeedback* feedback)
{
    if (feedback == nullptr) {
        return false;
    }
    auto& internal = feedback->internal;
    if (!algo::motor::basic_math::is_finite_float(raw_angle_rad) ||
        !algo::motor::basic_math::is_finite_float(refresh_freq) ||
        !algo::motor::basic_math::is_finite_float(gear_ratio) ||
        !algo::motor::basic_math::is_finite_float(invert_sign) ||
        !algo::motor::basic_math::is_finite_float(multi_turn_angle_filter_alpha) ||
        !algo::motor::basic_math::is_finite_float(internal.gear_ratio) ||
        !algo::motor::basic_math::is_finite_float(feedback->filter_alpha) ||
        (refresh_freq <= 0.0F) ||
        (gear_ratio <= 0.0F) ||
        (invert_sign == 0.0F)) {
        return false;
    }

    if (!internal.has_last_angle) {
        internal.last_angle = raw_angle_rad;
        feedback->angular_velocity = 0.0F;
        feedback->angular_velocity_raw = 0.0F;
        feedback->angular_acceleration = 0.0F;
        feedback->output_angular_velocity = 0.0F;
        const float output_equivalent_angle =
            invert_sign * raw_angle_rad / gear_ratio;
        feedback->output_equivalent_multi_turn_angle_rad = output_equivalent_angle;
        feedback->output_equivalent_multi_turn_angle_filted_rad =
            output_equivalent_angle;
        feedback->output_acc = 0.0F;
        feedback->output_acc_filted = 0.0F;
        internal.delta_angle = 0.0F;
        internal.last_angular_velocity = 0.0F;
        internal.filtered_angular_velocity = 0.0F;
        internal.last_output_angular_velocity = 0.0F;
        internal.output_acc_filted = 0.0F;
        internal.has_last_angle = true;
        return true;
    }
    if (!algo::motor::basic_math::is_finite_float(internal.last_angle) ||
        !algo::motor::basic_math::is_finite_float(feedback->angular_velocity)) {
        return false;
    }

    internal.delta_angle =
        algo::motor::basic_math::wrap_angle_minus_pi_to_pi(raw_angle_rad - internal.last_angle);
    const float output_equivalent_angle =
        feedback->output_equivalent_multi_turn_angle_rad +
        (invert_sign * internal.delta_angle / gear_ratio);
    feedback->angular_velocity_raw = internal.delta_angle * refresh_freq;
    const float velocity_alpha =
        algo::motor::basic_math::clamp_float(feedback->filter_alpha, 0.0F, 1.0F);
    internal.last_angular_velocity = feedback->angular_velocity;
    internal.filtered_angular_velocity =
        algo::motor::filter::low_pass_filter(feedback->angular_velocity_raw,
                                      internal.last_angular_velocity,
                                      velocity_alpha);
    const float multi_turn_angle_alpha = algo::motor::basic_math::clamp_float(
        multi_turn_angle_filter_alpha, 0.0F, 1.0F);
    const float output_equivalent_angle_filted =
        algo::motor::filter::low_pass_filter(
            output_equivalent_angle,
            feedback->output_equivalent_multi_turn_angle_filted_rad,
            multi_turn_angle_alpha);
    if (!algo::motor::basic_math::is_finite_float(output_equivalent_angle) ||
        !algo::motor::basic_math::is_finite_float(output_equivalent_angle_filted) ||
        !algo::motor::basic_math::is_finite_float(feedback->angular_velocity_raw) ||
        !algo::motor::basic_math::is_finite_float(internal.filtered_angular_velocity)) {
        return false;
    }

    feedback->output_equivalent_multi_turn_angle_rad = output_equivalent_angle;
    feedback->output_equivalent_multi_turn_angle_filted_rad =
        output_equivalent_angle_filted;
    feedback->angular_acceleration =
        (internal.filtered_angular_velocity - internal.last_angular_velocity) * refresh_freq;
    feedback->angular_velocity = internal.filtered_angular_velocity;
    const float output_angular_velocity =
        invert_sign * internal.filtered_angular_velocity / gear_ratio;
    feedback->output_acc =
        (output_angular_velocity - internal.last_output_angular_velocity) * refresh_freq;
    internal.output_acc_filted =
        algo::motor::filter::low_pass_filter(feedback->output_acc,
                                      internal.output_acc_filted,
                                      kOutputAngularAccelerationFilterAlpha);
    if (!algo::motor::basic_math::is_finite_float(output_angular_velocity) ||
        !algo::motor::basic_math::is_finite_float(feedback->output_acc) ||
        !algo::motor::basic_math::is_finite_float(internal.output_acc_filted)) {
        return false;
    }

    feedback->output_angular_velocity = output_angular_velocity;
    feedback->output_acc_filted = internal.output_acc_filted;
    internal.last_output_angular_velocity = output_angular_velocity;
    internal.last_angle = raw_angle_rad;
    return true;
}

// 根据输出端两路 TMR 更新单圈角、多圈角、零位/offset 标定和滤波角度。
bool update_joint_angle(float output_tmr1,
                        float output_tmr2,
                        float refresh_freq,
                        const MotorSensorCalibData& sensor_calib_data,
                        const MotorCalibData& motor_calib_data,
                        MotorJointAngleFeedback* feedback)
{
    if (feedback == nullptr) {
        return false;
    }
    if (!algo::motor::basic_math::is_finite_float(output_tmr1) ||
        !algo::motor::basic_math::is_finite_float(output_tmr2) ||
        !algo::motor::basic_math::is_finite_float(refresh_freq) ||
        (refresh_freq <= 0.0F) ||
        !algo::motor::basic_math::is_finite_float(feedback->filter_alpha)) {
        return false;
    }

    auto& internal = feedback->internal;
    feedback->output_tmr1 = output_tmr1;
    feedback->output_tmr2 = output_tmr2;
    internal.calibrated_tmr1 =
        apply_tmr_calib(output_tmr1, sensor_calib_data.output_tmr1_calib);
    internal.calibrated_tmr2 =
        apply_tmr_calib(output_tmr2, sensor_calib_data.output_tmr2_calib);
    feedback->single_turn_angle_raw =
        algo::motor::basic_math::normalize_angle_0_to_2pi(
            atan2f(internal.calibrated_tmr1, internal.calibrated_tmr2));
    if (!joint_angle_calibrate(feedback->single_turn_angle_raw,
                               sensor_calib_data,
                               feedback)) {
        return false;
    }

    const bool joint_zero_angle_valid = motor_calib_data.joint_zero_angle.valid;
    internal.single_turn_angle_zero_calib = feedback->single_turn_angle_calib;
    if (joint_zero_angle_valid) {
        internal.single_turn_angle_zero_calib =
            algo::motor::basic_math::normalize_angle_0_to_2pi(
                feedback->single_turn_angle_calib -
                motor_calib_data.joint_zero_angle.offset);
    }

    if (!internal.has_last_single_turn_angle) {
        feedback->turn_count = 0;
        // 开机落在零点负侧时，归一化单圈角接近 2pi，用 -1 圈还原成小负多圈角。
        if (joint_zero_angle_valid &&
            (internal.single_turn_angle_zero_calib > internal.jump_threshold)) {
            feedback->turn_count = -1;
        }
        internal.multi_turn_angle_raw =
            (static_cast<float>(feedback->turn_count) * algo::motor::basic_math::kTwoPi) +
            internal.single_turn_angle_zero_calib;
        feedback->multi_turn_angle = internal.multi_turn_angle_raw;
        if (motor_calib_data.joint_offset_angle.valid) {
            feedback->multi_turn_angle -= motor_calib_data.joint_offset_angle.offset;
        }
        feedback->multi_turn_angle_filted = feedback->multi_turn_angle;
        feedback->angular_velocity = 0.0F;
        internal.last_single_turn_angle_zero_calib = internal.single_turn_angle_zero_calib;
        internal.has_last_single_turn_angle = true;
        return true;
    }

    const float lower_jump_threshold =
        algo::motor::basic_math::kTwoPi - internal.jump_threshold;
    if ((internal.last_single_turn_angle_zero_calib > internal.jump_threshold) &&
        (internal.single_turn_angle_zero_calib < lower_jump_threshold)) {
        feedback->turn_count = feedback->turn_count + 1;
    } else if ((internal.last_single_turn_angle_zero_calib < lower_jump_threshold) &&
               (internal.single_turn_angle_zero_calib > internal.jump_threshold)) {
        feedback->turn_count = feedback->turn_count - 1;
    }

    const float last_multi_turn_angle = feedback->multi_turn_angle;
    internal.multi_turn_angle_raw =
        (static_cast<float>(feedback->turn_count) * algo::motor::basic_math::kTwoPi) +
        internal.single_turn_angle_zero_calib;
    feedback->multi_turn_angle = internal.multi_turn_angle_raw;
    if (motor_calib_data.joint_offset_angle.valid) {
        feedback->multi_turn_angle -= motor_calib_data.joint_offset_angle.offset;
    }
    const float angular_velocity =
        (feedback->multi_turn_angle - last_multi_turn_angle) * refresh_freq;
    feedback->angular_velocity += kJointAngularVelocityFilterAlpha *
                                  (angular_velocity - feedback->angular_velocity);

    const float alpha = algo::motor::basic_math::clamp_float(feedback->filter_alpha, 0.0F, 1.0F);
    feedback->multi_turn_angle_filted =
        algo::motor::filter::low_pass_filter(feedback->multi_turn_angle,
                                      feedback->multi_turn_angle_filted,
                                      alpha);
    internal.last_single_turn_angle_zero_calib = internal.single_turn_angle_zero_calib;
    return algo::motor::basic_math::is_finite_float(feedback->multi_turn_angle_filted) &&
           algo::motor::basic_math::is_finite_float(feedback->angular_velocity);
}

// 更新电流和母线电压反馈，依次完成原始值保存、线性标定和低通滤波。
bool update_elec_info(float iu_raw,
                      float iv_raw,
                      float iw_raw,
                      float vbus_raw,
                      MotorElecInfoFeedback* feedback)
{
    auto& internal = feedback->internal;

    feedback->iu_raw = apply_raw_convert(iu_raw, internal.iu_raw_convert_data);
    feedback->iv_raw = apply_raw_convert(iv_raw, internal.iv_raw_convert_data);
    feedback->iw_raw = apply_raw_convert(iw_raw, internal.iw_raw_convert_data);
    feedback->vbus_raw = apply_raw_convert(vbus_raw, internal.vbus_raw_convert_data);
    feedback->iu_calib = apply_linear_calib(feedback->iu_raw, internal.iu_calib_data);
    feedback->iv_calib = apply_linear_calib(feedback->iv_raw, internal.iv_calib_data);
    feedback->iw_calib = apply_linear_calib(feedback->iw_raw, internal.iw_calib_data);
    feedback->vbus_calib = apply_linear_calib(feedback->vbus_raw, internal.vbus_calib_data);

    const float current_alpha =
        algo::motor::basic_math::clamp_float(internal.current_filter_alpha, 0.0F, 1.0F);
    const float vbus_alpha =
        algo::motor::basic_math::clamp_float(internal.vbus_filter_alpha, 0.0F, 1.0F);
    if (!internal.has_filter_output) {
        feedback->iu_filter = feedback->iu_calib;
        feedback->iv_filter = feedback->iv_calib;
        feedback->iw_filter = feedback->iw_calib;
        feedback->vbus_filter = feedback->vbus_calib;
        internal.has_filter_output = true;
        return true;
    }

    feedback->iu_filter =
        algo::motor::filter::low_pass_filter(feedback->iu_calib, feedback->iu_filter, current_alpha);
    feedback->iv_filter =
        algo::motor::filter::low_pass_filter(feedback->iv_calib, feedback->iv_filter, current_alpha);
    feedback->iw_filter =
        algo::motor::filter::low_pass_filter(feedback->iw_calib, feedback->iw_filter, current_alpha);
    feedback->vbus_filter =
        algo::motor::filter::low_pass_filter(feedback->vbus_calib, feedback->vbus_filter, vbus_alpha);
    return true;
}


}  // namespace algo::motor
