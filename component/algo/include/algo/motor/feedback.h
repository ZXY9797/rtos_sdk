#pragma once

#include <algo/calibration/angle_lut.h>
#include <algo/math/basic_math.h>

namespace algo::motor {
inline constexpr size_t kElecAngleCalibPointCount = 112U;

struct MotorLinearCalibData {
    bool valid {false};
    float offset {0.0F};
    float gain {1.0F};
};

// Joint two-channel calibration in the stationary sensor plane.  The
// transform maps (raw-center) to an approximately unit circle.
struct MotorAffine2DCalibData {
    bool valid {false};
    uint8_t reserved[3] {};
    float center[2] {};
    float transform[4] {};
};

struct MotorTmrCalibData {
    bool valid {false};
    float offset {0.0F};
    float amp {1.0F};
};

static constexpr size_t kOutputAngleCalibPointCount = kAngleLutPointCount;

struct MotorElecAngleCalibData {
    bool valid {false};
    float zero_offset_rad {0.0F};
    float angle_offset_rad[kElecAngleCalibPointCount] {};
};

using MotorOutputAngleCalibData = MotorAngleLutData;
using MotorInputAngleCalibData = MotorAngleLutData;

struct MotorElecCalibData {
    MotorLinearCalibData iu_calib {};
    MotorLinearCalibData iv_calib {};
    MotorLinearCalibData iw_calib {};
    MotorLinearCalibData vbus_calib {};
};

struct MotorCurrentLinearCalibData {
    MotorLinearCalibData iu_calib {};
    MotorLinearCalibData iv_calib {};
    MotorLinearCalibData iw_calib {};
};

struct MotorTmrPairCalibData {
    MotorTmrCalibData tmr1_calib {};
    MotorTmrCalibData tmr2_calib {};
};

struct MotorSensorCalibData {
    MotorAffine2DCalibData input_sensor_calib {};
    MotorTmrCalibData output_tmr1_calib {};
    MotorTmrCalibData output_tmr2_calib {};
    MotorElecAngleCalibData elec_angle_calib {};
    const MotorAngleLutView* output_angle_calib {nullptr};
};

struct JointAngleCalibData {
    bool valid {false};
    float offset {0.0F};
};

struct MotorBasicConfig {
    bool valid {true};
    uint8_t pole_pairs {1U};
    float gear_ratio {1.0F};
    float kt_coefficient {1.0F};
};

struct MotorCalibData {
    JointAngleCalibData joint_zero_angle {};
    JointAngleCalibData joint_offset_angle {};
};

// These are the float32 calibration payload layouts. The MCAL
// header has a separate explicit LE codec; payload ABI must also match storage.
static_assert(sizeof(float) == 4U);
static_assert(sizeof(MotorLinearCalibData) == 12U);
static_assert(sizeof(MotorCurrentLinearCalibData) == 36U);
static_assert(sizeof(MotorAffine2DCalibData) == 28U);
static_assert(sizeof(MotorTmrPairCalibData) == 24U);
static_assert(sizeof(MotorElecAngleCalibData) == 456U);
static_assert(sizeof(JointAngleCalibData) == 8U);

struct MotorElecInfoFeedback {
    float iu_raw {0.0F};
    float iv_raw {0.0F};
    float iw_raw {0.0F};
    float vbus_raw {0.0F};
    float iu_calib {0.0F};
    float iv_calib {0.0F};
    float iw_calib {0.0F};
    float vbus_calib {0.0F};
    float iu_filter {0.0F};
    float iv_filter {0.0F};
    float iw_filter {0.0F};
    float vbus_filter {0.0F};
    struct {
        MotorLinearCalibData iu_raw_convert_data {};
        MotorLinearCalibData iv_raw_convert_data {};
        MotorLinearCalibData iw_raw_convert_data {};
        MotorLinearCalibData vbus_raw_convert_data {};
        MotorLinearCalibData iu_calib_data {};
        MotorLinearCalibData iv_calib_data {};
        MotorLinearCalibData iw_calib_data {};
        MotorLinearCalibData vbus_calib_data {};
        float current_filter_alpha {0.9F};
        float vbus_filter_alpha {0.08F};
        bool has_filter_output {false};
    } internal;
};

/*
 * 电机传感器数据处理说明：
 * - MotorElecAngleFeedback 处理输入端 TMR 到机械角、电角度和电角度标定结果。
 * - MotorVelocityFeedback 根据连续机械角计算角速度、角加速度，以及输出端等效多圈角
 *   的原始值和滤波值。
 * - MotorJointAngleFeedback 处理输出端 TMR 到单圈角、多圈角、零位/offset 标定和滤波结果。
 * - feedback 结构体外层字段保存控制环直接使用的结果，internal 保存插值配置和跨周期状态。
 */
struct MotorElecAngleFeedback {
    float input_tmr1 {0.0F};
    float input_tmr2 {0.0F};
    float mech_angle {0.0F};
    float elec_angle {0.0F};
    float elec_angle_calib {0.0F};
    float mech_angle_calib {0.0F};
    struct {
        algo::motor::basic_math::UniformLagrangeInterpConfig interp_config {};
        float calibrated_tmr1 {0.0F};
        float calibrated_tmr2 {0.0F};
        float angle_range {0.0F};
        float zeroed_angle {0.0F};
        float angle_offset {0.0F};
    } internal;
};

struct MotorInputTmrAngleConfig {
    bool swap_channels {false};
};

struct MotorVelocityFeedback {
    float angular_velocity {0.0F};
    float angular_velocity_raw {0.0F};
    float angular_acceleration {0.0F};
    float output_angular_velocity {0.0F};
    // 输入端标定机械角按减速比折算到输出端后的未滤波连续多圈角。
    float output_equivalent_multi_turn_angle_rad {0.0F};
    // 与输出轴多圈角采用相同 4 kHz 一阶低通后的结果。
    float output_equivalent_multi_turn_angle_filted_rad {0.0F};
    float output_acc {0.0F};
    float output_acc_filted {0.0F};
    float filter_alpha {0.05F};
    struct {
        float gear_ratio {1.0F};
        float last_angle {0.0F};
        float delta_angle {0.0F};
        float last_angular_velocity {0.0F};
        float filtered_angular_velocity {0.0F};
        float last_output_angular_velocity {0.0F};
        float output_acc_filted {0.0F};
        bool has_last_angle {false};
    } internal;
};

struct MotorJointAngleFeedback {
    float output_tmr1 {0.0F};
    float output_tmr2 {0.0F};
    float single_turn_angle_raw {0.0F};
    float single_turn_angle_calib {0.0F};
    float multi_turn_angle {0.0F};
    float multi_turn_angle_filted {0.0F};
    float angular_velocity {0.0F};  /*输出轴传感器计算的角速度*/
    float filter_alpha {0.2F};
    int32_t turn_count {0};
    struct {
        algo::motor::basic_math::UniformLagrangeInterpConfig interp_config {};
        float calibrated_tmr1 {0.0F};
        float calibrated_tmr2 {0.0F};
        float angle_offset {0.0F};
        float single_turn_angle_zero_calib {0.0F};
        float last_single_turn_angle_zero_calib {0.0F};
        float multi_turn_angle_raw {0.0F};
        float jump_threshold {0.0F};
        bool has_last_single_turn_angle {false};
    } internal;
};

bool current_ctrl_feedback_init(const MotorElecCalibData& elec_raw_convert_data,
                                const MotorElecCalibData& elec_calib_data,
                                MotorElecInfoFeedback* elec_info_feedback,
                                MotorElecAngleFeedback* elec_angle_feedback,
                                float current_filter_alpha = 0.9F);
bool joint_ctrl_feedback_init(const MotorBasicConfig& motor_basic_config,
                              MotorVelocityFeedback* velocity_feedback,
                              MotorJointAngleFeedback* joint_angle_feedback);
bool update_sensor_angle(float input_tmr_v1,
                         float input_tmr_v2,
                         const MotorSensorCalibData& sensor_calib_data,
                         const MotorInputTmrAngleConfig& angle_config,
                         MotorElecAngleFeedback* feedback);
bool elec_angle_calibrate(const MotorSensorCalibData& sensor_calib_data,
                          const MotorBasicConfig& motor_basic_config,
                          MotorElecAngleFeedback* feedback);
bool update_velocity(float raw_angle_rad,
                     float refresh_freq,
                     float gear_ratio,
                     float invert_sign,
                     float multi_turn_angle_filter_alpha,
                     MotorVelocityFeedback* feedback);
bool joint_angle_calibrate(float raw_angle_rad,
                           const MotorSensorCalibData& sensor_calib_data,
                           MotorJointAngleFeedback* feedback);
bool update_joint_angle(float output_tmr1,
                        float output_tmr2,
                        float refresh_freq,
                        const MotorSensorCalibData& sensor_calib_data,
                        const MotorCalibData& motor_calib_data,
                        MotorJointAngleFeedback* feedback);
bool update_elec_info(float iu_raw,
                      float iv_raw,
                      float iw_raw,
                      float vbus_raw,
                      MotorElecInfoFeedback* feedback);

}  // namespace algo::motor
