#pragma once

#include <algo/motor.h>
#include "control/motor_observation.h"
#include "control/interaction_config.h"
#if defined(CONFIG_DEMO_FOC_MD007_V1)
#include "comm/joint_canopen_fd.h"
#endif
#include <cstddef>
#include <cstdint>

namespace app {

enum class CalibrationRecordId : uint8_t {
    CurrentLinear, BusVoltage, InputTmr, OutputTmr, ElectricalAngle,
    InputLut, OutputLut, JointZero, JointOffset,
};

struct CalibrationValues {
    algo::motor::MotorCurrentLinearCalibData current {};
    algo::motor::MotorLinearCalibData bus {};
    algo::motor::MotorAffine2DCalibData input {};
    algo::motor::MotorTmrPairCalibData output {};
    algo::motor::MotorElecAngleCalibData electrical {};
    algo::motor::MotorCalibData joint {};
};

struct RecordView {
    CalibrationRecordId id {};
    const void* payload {nullptr};
    size_t size {0U};
};

struct MotorStatus {
    bool enabled {false};
    bool ready {false};
    bool hardware_fault {false};
    uint32_t protection_mask {0U};
    float position_rad {0.0F};
    float velocity_rad_s {0.0F};
    float current_a {0.0F};
    float bus_voltage_v {0.0F};
    algo::motor::CalibrationState calibration {};
    algo::motor::CalibrationKind calibration_kind {};
    uint32_t calibration_samples {0U}, calibration_motions {0U};
    bool diagnostic_active {false}, auto_calibration_failed {false};
    algo::motor::BacklashTestResultSnapshot backlash {};
    algo::motor::RlIdentificationResultSnapshot rl {};
    algo::motor::CurrentSweepResultSnapshot sweep {};
    uint32_t dropped_rl {0U}, dropped_sweep {0U};
};

// ADC IRQs and command/slow-task calls are serialized by the product adapter.
// All numerical state belongs to algo; this class only arbitrates operations.
class MotorControl {
public:
    bool configure(const algo::motor::MotorParameters& parameters,
                    const CalibrationValues& calibration);
    algo::motor::CurrentLoopOutput current_sample(const float (&voltages)[6], uint32_t timestamp_us = 0U);
    bool joint_sample(float tmr1_voltage, float tmr2_voltage, uint32_t timestamp_us = 0U);
    void tick_1ms(uint32_t now_ms, const algo::motor::TemperatureSample& temperature);
    bool enable(bool value);
    bool select_mode(algo::motor::JointTargetMode mode);
    bool submit(const algo::motor::JointTargetOrder& order);
    bool start_calibration(algo::motor::CalibrationKind kind);
    bool start_auto_calibration();
    bool set_plan(algo::motor::PositionTargetPlanMode mode);
    bool start_rl(bool stream_ready);
    bool start_sweep(const algo::motor::CurrentSweepConfig& config, bool stream_ready);
    bool start_backlash();
    void stop(bool emergency = false);
    void fault();
    bool clear_fault();
    bool idle() const;
    MotorStatus status() const;
    bool mode_command(const ModeCommand& command);
    ModeStatus mode_status() const;
    MotorObservation observation() const;
#if defined(CONFIG_DEMO_FOC_MD007_V1)
    JointFeedbackSample joint_feedback_sample() const;
    bool drive_requested() const { return enabled_; }
#endif
    void record_timing(unsigned loop, uint32_t start_us, uint32_t end_us);
    bool encoder_frame(uint32_t id, const uint8_t* data, size_t size, uint32_t processed_us);
    bool configure_capture(bool enabled, uint16_t divider);
    bool peek_capture(CalibrationPair& pair) const;
    void consume_capture(uint32_t sequence);
    bool erase_record(CalibrationRecordId id, bool verified);
    bool calibration_record(CalibrationRecordId id, RecordView& record) const;

    bool pending_calibration(RecordView& record);
    bool finish_calibration(bool verified);
    bool joint_reference(bool zero, RecordView& record);
    bool commit_reference(bool zero, bool verified);
    bool linear_candidate(CalibrationRecordId id, const void* data, size_t size, RecordView& record);
    bool commit_linear(CalibrationRecordId id, bool verified);

    bool lut_begin(const algo::motor::AngleLutRequest& request, uint16_t owner);
    bool lut_begin_read(const algo::motor::AngleLutRequest& request, uint16_t owner,
                         const algo::motor::MotorAngleLutData& stored);
    bool lut_finish(bool verified);
    bool lut_abort(const algo::motor::AngleLutRequest& request, uint16_t owner);
    algo::motor::LutSession& lut() { return lut_; }
    algo::motor::MotorDiagnostics& diagnostics() { return diagnostics_; }
    const algo::motor::MotorParameters& parameters() const { return parameters_; }
    const CalibrationValues& calibration() const { return calibration_; }
    algo::motor::MotorAngleLutData& output_lut_storage() { return output_lut_; }
    bool publish_loaded_lut();
    bool configure_interaction(const InteractionConfig& config);
    bool validate_interaction(const InteractionConfig& config) const;
    const InteractionConfig& interaction_config() const { return interaction_config_; }
    bool external_feedback(uint32_t sequence, uint8_t flags, const float* values);
    uint32_t external_sequence() const { return external_sequence_; }
    float external_age_s() const { return external_age_s_; }
    uint8_t external_flags() const {
        return external_age_s_>interaction_config_.joint.external_timeout_s?0:
            uint8_t(external_.external_valid)|uint8_t(external_.effort_valid<<1U)|
            uint8_t(external_.force_valid<<2U)|uint8_t(wrench_valid_<<3U);
    }
    const algo::interaction::JointResult& interaction_result() const { return interaction_snapshot_; }
    const algo::motor::ExecutionState& execution_state() const { return limits_.state(); }
    algo::motor::ElectricalFit electrical_fit() const { return electrical_identifier_.result(); }
    bool torque_calibration(uint8_t action,float measured_nm,InteractionConfig& candidate,float& rms);
    const algo::robotics::CartesianController<1>::Result& cartesian_result() const { return cartesian_result_; }
    static algo::motor::CalibrationRecordType record_type(CalibrationRecordId id);

private:
    algo::motor::MotorSensorCalibData sensors() const;
    bool configure_feedback();
    bool exclusive_available() const;
    bool motor_operation_active() const;
    bool ready() const;
    void reset_control();
    void confirm_mode(bool transition_complete);
    void capture_pair(uint32_t timestamp_us);
    algo::interaction::JointFeedback interaction_feedback() const;
    bool interaction_command(const ModeCommand& command);
    float interaction_update(bool& valid, bool& energize, bool& complete);
    algo::motor::MotorParameters parameters_ {};
    CalibrationValues calibration_ {};
    algo::motor::FeedbackProcessor feedback_ {};
    algo::motor::CurrentLoop current_ {};
    algo::motor::JointController joint_ {};
    InteractionConfig interaction_config_ {};
    algo::motor::ExecutionLimits limits_ {};
    algo::motor::ElectricalIdentifier electrical_identifier_ {};
    algo::interaction::TorqueCalibration torque_calibration_ {};
    algo::interaction::JointInteraction interaction_ {};
    algo::interaction::JointResult interaction_snapshot_ {};
    algo::robotics::CartesianController<1> cartesian_ {};
    algo::robotics::CartesianController<1>::Target cartesian_target_ {};
    algo::robotics::CartesianController<1>::Result cartesian_result_ {};
    algo::interaction::JointFeedback external_ {};
    algo::robotics::Wrench external_wrench_ {};
    uint32_t external_sequence_ {0U};
    float external_age_s_ {0.0F};
    float temperature_age_s_ {0.0F};
    bool external_seen_ {false}, wrench_valid_ {false}, cartesian_active_ {false};
    uint8_t control_mode_ {2U};
    algo::motor::Calibrator calibrator_ {};
    algo::motor::AutoCalibration auto_calibration_ {};
    algo::motor::Protection protection_ {};
    algo::motor::MotorDiagnostics diagnostics_ {};
    algo::motor::BacklashIdentifier backlash_ {};
    algo::motor::LutSession lut_ {};
    algo::motor::MotorAngleLutData output_lut_ {};
    algo::motor::MotorAngleLutView output_view_ {&output_lut_, false};
    algo::motor::MotorCalibData pending_reference_ {};
    algo::motor::MotorTmrPairCalibData pending_output_ {};
    algo::motor::MotorCurrentLinearCalibData pending_current_ {};
    algo::motor::MotorLinearCalibData pending_bus_ {};
    algo::motor::CalibrationSample sample_ {};
    algo::motor::CurrentLoopOutput current_output_ {};
    uint32_t now_ms_ {0U};
    uint32_t fast_count_ {0U}, joint_count_ {0U};
    uint32_t checked_fast_count_ {0U}, checked_joint_count_ {0U};
    uint32_t last_fast_ms_ {0U}, last_joint_ms_ {0U};
    float q_target_a_ {0.0F};
    bool enabled_ {false}, configured_ {false}, hardware_fault_ {false};
    bool rotor_valid_ {false}, joint_valid_ {false};
    bool stopping_ {false};
    bool mode_managed_ {false}, mode_pending_ {false}, holding_ {false}, local_disable_ {false};
    uint32_t accepted_sequence_ {0U}, applied_sequence_ {0U};
    ModeAction accepted_action_ {};
    bool accepted_enabled_ {false};
    uint32_t input_us_ {0U}, joint_us_ {0U};
    algo::motor::TemperatureSample temperature_ {};
    CycleTiming timing_[3] {};
    EncoderSample encoder_ {};
    CalibrationPair capture_[16] {};
    uint32_t capture_sequence_ {0U}, capture_dropped_ {0U}, paired_encoder_ {0U};
    uint16_t capture_divider_ {0U}, capture_phase_ {0U};
    uint8_t capture_head_ {0U}, capture_size_ {0U};
};

} // namespace app
