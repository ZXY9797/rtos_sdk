#include <algo/calibration/calibration.h>
#include <cmath>
#include <initializer_list>

namespace algo::motor {
namespace {
constexpr uint32_t kInputPoints = 4U * kElecAngleCalibPointCount;
constexpr uint32_t kOutputPoints = static_cast<uint32_t>(kInputPoints * 1.2F + 0.5F);
constexpr float kInputStep = basic_math::kPi / 16.0F;
constexpr float kOutputStep = basic_math::kPi / 6.0F;
}

bool valid_linear_calibration(const MotorLinearCalibData& data)
{
    return data.valid && std::isfinite(data.offset) && std::isfinite(data.gain) &&
           std::fabs(data.gain) > 1.0e-6F;
}

bool Calibrator::configure(const CalibrationConfig& config)
{
    if (state_ == CalibrationState::Collecting ||
        state_ == CalibrationState::PendingPersistence) return false;
    configured_ = false;
    const float motions = kInputPoints * config.motor.gear_ratio * 1.2F *
                          (kInputStep / kOutputStep) + 0.5F;
    if (!config.motor.valid || config.motor.pole_pairs == 0U ||
        !std::isfinite(config.motor.gear_ratio) || config.motor.gear_ratio <= 0.0F ||
        !std::isfinite(motions) || motions < 1.0F || motions > 10000000.0F ||
        config.input_tmr.step_interval_ms == 0U ||
        config.electrical_angle.step_interval_ms == 0U ||
        config.output_tmr.step_interval_ms == 0U) return false;
    for (float voltage : {config.input_voltage_v, config.electrical_voltage_v,
                          config.output_voltage_v})
        if (!std::isfinite(voltage) || voltage < 0.0F) return false;
    config_ = config;
    output_motion_count_ = static_cast<uint32_t>(motions);
    // Unsigned elapsed comparisons require a bounded duration below half range.
    for (CalibrationKind kind : {CalibrationKind::InputTmr,
                                  CalibrationKind::ElectricalAngle,
                                  CalibrationKind::OutputTmr}) {
        kind_ = kind;
        const auto t = timing();
        const uint64_t points = kind == CalibrationKind::InputTmr ? kInputPoints :
            kind == CalibrationKind::ElectricalAngle ? kElecAngleCalibPointCount :
            output_motion_count_;
        if (uint64_t(t.initial_align_ms) + uint64_t(t.step_interval_ms) *
            (points + 5U) + 5000U >= 0x80000000ULL) return false;
    }
    kind_ = CalibrationKind::None;
    state_ = CalibrationState::Idle;
    configured_ = true;
    return true;
}

CalibrationTiming Calibrator::timing() const
{
    switch (kind_) {
    case CalibrationKind::InputTmr: return config_.input_tmr;
    case CalibrationKind::ElectricalAngle: return config_.electrical_angle;
    case CalibrationKind::OutputTmr: return config_.output_tmr;
    default: return {};
    }
}

uint32_t Calibrator::timeout_ms() const
{
    if (kind_ == CalibrationKind::None) return 0U;
    const auto t = timing();
    const uint32_t points = kind_ == CalibrationKind::InputTmr ? kInputPoints :
        kind_ == CalibrationKind::ElectricalAngle ? kElecAngleCalibPointCount : output_motion_count_;
    return t.initial_align_ms + t.step_interval_ms * (points + 5U) + 5000U;
}

bool Calibrator::start(CalibrationKind kind, uint32_t now_ms,
                      const MotorSensorCalibData& previous, bool output_disabled,
                      bool brake_ready)
{
    if (!configured_ || !output_disabled || !brake_ready ||
        state_ == CalibrationState::Collecting ||
        state_ == CalibrationState::PendingPersistence ||
        (kind != CalibrationKind::InputTmr && kind != CalibrationKind::ElectricalAngle &&
         kind != CalibrationKind::OutputTmr) ||
        (kind == CalibrationKind::ElectricalAngle && !previous.input_sensor_calib.valid))
        return false;
    previous_ = previous;
    result_ = previous;
    kind_ = kind;
    start_ms_ = sample_ms_ = now_ms;
    count_ = motion_count_ = 0U;
    for (float& sum : sums_) sum = 0.0F;
    for (unsigned i = 0; i < 2; ++i) minimum_[i] = maximum_[i] = 0.0F;
    drive_ = {};
    drive_.energize = true;
    drive_.d_voltage_v = kind == CalibrationKind::InputTmr ? config_.input_voltage_v :
        kind == CalibrationKind::ElectricalAngle ? config_.electrical_voltage_v :
        config_.output_voltage_v;
    if (kind == CalibrationKind::InputTmr) result_.input_sensor_calib.valid = false;
    if (kind == CalibrationKind::ElectricalAngle) result_.elec_angle_calib = {};
    if (kind == CalibrationKind::OutputTmr) {
        result_.output_tmr1_calib.valid = result_.output_tmr2_calib.valid = false;
    }
    state_ = CalibrationState::Collecting;
    return true;
}

void Calibrator::fail(CalibrationState state)
{
    drive_ = {};
    result_ = previous_;
    state_ = state;
}

void Calibrator::cancel()
{
    if (state_ == CalibrationState::Collecting ||
        state_ == CalibrationState::PendingPersistence) fail(CalibrationState::Cancelled);
}

bool Calibrator::complete_persistence(bool success)
{
    if (state_ != CalibrationState::PendingPersistence) return false;
    if (success) state_ = CalibrationState::Complete;
    else fail(CalibrationState::PersistenceError);
    return success;
}

bool Calibrator::finish_input_fit()
{
    const float inverse_count = 1.0F / static_cast<float>(count_);
    const float center1 = sums_[0] * inverse_count;
    const float center2 = sums_[1] * inverse_count;
    const float c00 = sums_[2] * inverse_count - center1 * center1;
    const float c01 = sums_[3] * inverse_count - center1 * center2;
    const float c11 = sums_[4] * inverse_count - center2 * center2;
    const float det = c00 * c11 - c01 * c01;
    const float trace = c00 + c11;
    if (!std::isfinite(det) || det <= 1.0e-8F ||
        !std::isfinite(trace) || trace <= 0.0F) return false;
    const float root_det = std::sqrt(det);
    const float denominator = std::sqrt(trace + 2.0F * root_det);
    if (!std::isfinite(root_det) || denominator <= 0.0F) return false;
    const float scale = denominator /
        (1.41421356237F * root_det * (trace + 2.0F * root_det));
    auto& fit = result_.input_sensor_calib;
    fit.center[0] = center1;
    fit.center[1] = center2;
    fit.transform[0] = scale * (c11 + root_det);
    fit.transform[1] = fit.transform[2] = scale * -c01;
    fit.transform[3] = scale * (c00 + root_det);
    for (float value : fit.transform) if (!std::isfinite(value)) return false;
    fit.valid = true;
    return true;
}

CalibrationState Calibrator::update(uint32_t now_ms, const CalibrationSample& sample,
                                    bool protection_active)
{
    if (state_ != CalibrationState::Collecting) {
        if (protection_active && state_ == CalibrationState::PendingPersistence)
            fail(CalibrationState::Protection);
        return state_;
    }
    if (protection_active) { fail(CalibrationState::Protection); return state_; }
    if (uint32_t(now_ms - start_ms_) > timeout_ms()) {
        fail(CalibrationState::Timeout);
        return state_;
    }
    const auto t = timing();
    const bool first = kind_ == CalibrationKind::OutputTmr ? motion_count_ == 0U : count_ == 0U;
    const uint32_t wait = first && t.initial_align_ms ? t.initial_align_ms : t.step_interval_ms;
    if (uint32_t(now_ms - sample_ms_) < wait || !sample.valid) return state_;
    sample_ms_ = now_ms;
    bool finished = false;
    if (kind_ == CalibrationKind::InputTmr) {
        const float x = sample.rotor_tmr1, y = sample.rotor_tmr2;
        if (!std::isfinite(x) || !std::isfinite(y)) {
            fail(CalibrationState::Invalid); return state_;
        }
        sums_[0] += x; sums_[1] += y; sums_[2] += x*x;
        sums_[3] += x*y; sums_[4] += y*y;
        ++count_;
        finished = count_ == kInputPoints;
        if (finished && !finish_input_fit()) { fail(CalibrationState::Invalid); return state_; }
        if (!finished) drive_.target_electrical_angle_rad += kInputStep;
    } else if (kind_ == CalibrationKind::ElectricalAngle) {
        if (!std::isfinite(sample.raw_electrical_angle_rad) ||
            !std::isfinite(sample.applied_open_loop_angle_rad)) {
            fail(CalibrationState::Invalid); return state_;
        }
        auto& fit = result_.elec_angle_calib;
        if (count_ == 0U) fit.zero_offset_rad = sample.raw_electrical_angle_rad;
        const float range = basic_math::kTwoPi * config_.motor.pole_pairs;
        float position = sample.raw_electrical_angle_rad - fit.zero_offset_rad;
        if (position < 0.0F) position += range;
        if (position > range) position -= range;
        fit.angle_offset_rad[count_++] = sample.applied_open_loop_angle_rad - position;
        finished = count_ == kElecAngleCalibPointCount;
        if (finished) fit.valid = true;
        else drive_.target_electrical_angle_rad += basic_math::kPi / 4.0F;
    } else {
        ++motion_count_;
        const uint32_t target_count = static_cast<uint32_t>(
            uint64_t(motion_count_) * kOutputPoints / output_motion_count_);
        if (count_ < target_count) {
            const float values[2] {sample.output_tmr1, sample.output_tmr2};
            for (unsigned i = 0; i < 2; ++i) {
                if (!std::isfinite(values[i])) { fail(CalibrationState::Invalid); return state_; }
                if (count_ == 0U) minimum_[i] = maximum_[i] = values[i];
                if (values[i] < minimum_[i]) minimum_[i] = values[i];
                if (values[i] > maximum_[i]) maximum_[i] = values[i];
            }
            ++count_;
        }
        finished = motion_count_ == output_motion_count_;
        if (finished) {
            result_.output_tmr1_calib = {true, (maximum_[0] + minimum_[0]) * 0.5F,
                                       (maximum_[0] - minimum_[0]) * 0.5F};
            result_.output_tmr2_calib = {true, (maximum_[1] + minimum_[1]) * 0.5F,
                                       (maximum_[1] - minimum_[1]) * 0.5F};
        } else drive_.target_electrical_angle_rad += kOutputStep;
    }
    if (finished) {
        drive_ = {};
        state_ = CalibrationState::PendingPersistence;
    }
    return state_;
}

CalibrationKind Calibrator::next_missing(const MotorSensorCalibData& data)
{
    if (!data.input_sensor_calib.valid) return CalibrationKind::InputTmr;
    if (!data.elec_angle_calib.valid) return CalibrationKind::ElectricalAngle;
    if (!data.output_tmr1_calib.valid || !data.output_tmr2_calib.valid)
        return CalibrationKind::OutputTmr;
    return CalibrationKind::None;
}

}  // namespace algo::motor
