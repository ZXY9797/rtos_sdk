#pragma once

#include <algo/motor/feedback.h>

namespace algo::motor {

enum class CalibrationKind : uint8_t { None = 0, InputTmr = 3, ElectricalAngle = 4, OutputTmr = 5 };
enum class CalibrationState : uint8_t {
    Idle, Collecting, PendingPersistence, Complete, Cancelled, Invalid,
    Timeout, Protection, PersistenceError,
};

struct CalibrationTiming {
    uint32_t initial_align_ms {0U};
    uint32_t step_interval_ms {0U};
};

struct CalibrationConfig {
    MotorBasicConfig motor {};
    CalibrationTiming input_tmr {};
    CalibrationTiming electrical_angle {};
    CalibrationTiming output_tmr {};
    float input_voltage_v {1.5F};
    float electrical_voltage_v {1.5F};
    float output_voltage_v {1.5F};
};

struct CalibrationSample {
    bool valid {false};
    float rotor_tmr1 {0.0F};
    float rotor_tmr2 {0.0F};
    float output_tmr1 {0.0F};
    float output_tmr2 {0.0F};
    float raw_electrical_angle_rad {0.0F};
    float applied_open_loop_angle_rad {0.0F};
};

struct CalibrationDrive {
    bool energize {false};
    float d_voltage_v {0.0F};
    float target_electrical_angle_rad {0.0F};
};

// Call from one slow-task owner with a coherent ADC/current-loop snapshot.
// Persistence and motor/brake ownership remain with the application. Results
// become active only after complete_persistence(true); failures restore backup.
class Calibrator {
public:
    bool configure(const CalibrationConfig& config);
    bool start(CalibrationKind kind, uint32_t now_ms,
               const MotorSensorCalibData& previous, bool output_disabled,
               bool brake_ready = true);
    CalibrationState update(uint32_t now_ms, const CalibrationSample& sample,
                            bool protection_active = false);
    void cancel();
    bool complete_persistence(bool success);
    CalibrationState state() const { return state_; }
    CalibrationKind kind() const { return kind_; }
    const CalibrationDrive& drive() const { return drive_; }
    const MotorSensorCalibData& result() const { return result_; }
    uint32_t sample_count() const { return count_; }
    uint32_t motion_count() const { return motion_count_; }
    uint32_t timeout_ms() const;
    static CalibrationKind next_missing(const MotorSensorCalibData& data);

private:
    void fail(CalibrationState state);
    bool finish_input_fit();
    CalibrationTiming timing() const;
    CalibrationConfig config_ {};
    MotorSensorCalibData previous_ {};
    MotorSensorCalibData result_ {};
    CalibrationDrive drive_ {};
    CalibrationState state_ {CalibrationState::Idle};
    CalibrationKind kind_ {CalibrationKind::None};
    uint32_t start_ms_ {0U};
    uint32_t sample_ms_ {0U};
    uint32_t count_ {0U};
    uint32_t motion_count_ {0U};
    uint32_t output_motion_count_ {0U};
    float sums_[5] {};
    float minimum_[2] {};
    float maximum_[2] {};
    bool configured_ {false};
};

bool valid_linear_calibration(const MotorLinearCalibData& data);

class AutoCalibration {
public:
    void reset(bool enabled) { enabled_ = enabled; active_ = failed_ = false; next_ = 0U; }
    CalibrationKind poll(const MotorSensorCalibData& sensors, bool previous_success,
                         bool exclusive_pipeline_idle, bool control_disabled,
                         bool adc_sample_available)
    {
        if (!enabled_ || !exclusive_pipeline_idle || !control_disabled || !adc_sample_available)
            return CalibrationKind::None;
        if (active_ && !previous_success) {
            active_ = false;
            failed_ = true;
            next_ = 0U;
            return CalibrationKind::None;
        }
        const bool required = Calibrator::next_missing(sensors) != CalibrationKind::None;
        if (active_) {
            if (next_ < 3U) return sequence(next_++);
            active_ = false;
            failed_ = required;
            next_ = 0U;
            return CalibrationKind::None;
        }
        if (!required) { failed_ = false; next_ = 0U; return CalibrationKind::None; }
        if (failed_) return CalibrationKind::None;
        active_ = true;
        next_ = 1U;
        return sequence(0U);
    }
    bool active() const { return active_; }
    bool failed() const { return failed_; }
private:
    static CalibrationKind sequence(unsigned index) {
        return index == 0U ? CalibrationKind::InputTmr :
               index == 1U ? CalibrationKind::ElectricalAngle : CalibrationKind::OutputTmr;
    }
    unsigned next_ {0U};
    bool enabled_ {false}, active_ {false}, failed_ {false};
};

}  // namespace algo::motor
