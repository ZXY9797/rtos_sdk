#include <gimbal/hall_sensor.h>

#include <gimbal/math.h>

#include <algorithm>
#include <cmath>
#include <limits>

namespace gimbal {
namespace {

constexpr uint32_t kMinimumCalibrationSamples = 64U;
constexpr float kMinimumSignalSpan = 16.0F;
constexpr float kMaximumRawSample = 65535.0F;
constexpr double kMinimumCovariance = 1.0e-9;

[[nodiscard]] bool finite(float value)
{
    return std::isfinite(value);
}

[[nodiscard]] Vector2 correct_sample(const HallRawSample &sample,
                                     const HallCalibration &calibration)
{
    const float channel_a = sample.channel_a - calibration.offset.x;
    const float channel_b = sample.channel_b - calibration.offset.y;
    return {
        calibration.correction[0][0] * channel_a
            + calibration.correction[0][1] * channel_b,
        calibration.correction[1][0] * channel_a
            + calibration.correction[1][1] * channel_b,
    };
}

[[nodiscard]] bool covariance_whitening(double covariance_aa,
                                        double covariance_ab,
                                        double covariance_bb,
                                        float output[2][2])
{
    const double trace = covariance_aa + covariance_bb;
    const double difference = covariance_aa - covariance_bb;
    const double discriminant = std::sqrt(
        std::max(0.0, difference * difference
                       + 4.0 * covariance_ab * covariance_ab));
    const double eigenvalue_1 = 0.5 * (trace + discriminant);
    const double eigenvalue_2 = 0.5 * (trace - discriminant);
    if (eigenvalue_2 <= kMinimumCovariance) {
        return false;
    }

    double eigenvector_x = covariance_ab;
    double eigenvector_y = eigenvalue_1 - covariance_aa;
    if (std::abs(eigenvector_x) + std::abs(eigenvector_y)
        <= kMinimumCovariance) {
        eigenvector_x = 1.0;
        eigenvector_y = 0.0;
    }
    const double length = std::hypot(eigenvector_x, eigenvector_y);
    eigenvector_x /= length;
    eigenvector_y /= length;
    const double inverse_1 = 1.0 / std::sqrt(eigenvalue_1);
    const double inverse_2 = 1.0 / std::sqrt(eigenvalue_2);
    output[0][0] = static_cast<float>(
        inverse_1 * eigenvector_x * eigenvector_x
        + inverse_2 * eigenvector_y * eigenvector_y);
    output[0][1] = static_cast<float>(
        (inverse_1 - inverse_2) * eigenvector_x * eigenvector_y);
    output[1][0] = output[0][1];
    output[1][1] = static_cast<float>(
        inverse_1 * eigenvector_y * eigenvector_y
        + inverse_2 * eigenvector_x * eigenvector_x);
    return finite(output[0][0]) && finite(output[0][1])
        && finite(output[1][0]) && finite(output[1][1]);
}

} // namespace

bool valid_hall_calibration(const HallCalibration &calibration)
{
    const float determinant =
        calibration.correction[0][0] * calibration.correction[1][1]
        - calibration.correction[0][1] * calibration.correction[1][0];
    return finite(calibration.offset.x) && finite(calibration.offset.y)
        && finite(calibration.correction[0][0])
        && finite(calibration.correction[0][1])
        && finite(calibration.correction[1][0])
        && finite(calibration.correction[1][1])
        && finite(determinant) && std::abs(determinant) > 1.0e-8F
        && finite(calibration.mechanical_zero_rad)
        && finite(calibration.electrical_zero_rad)
        && finite(calibration.mechanical_min_rad)
        && finite(calibration.mechanical_max_rad)
        && calibration.mechanical_min_rad < calibration.mechanical_max_rad
        && finite(calibration.minimum_signal_norm)
        && finite(calibration.maximum_signal_norm)
        && calibration.minimum_signal_norm > 0.0F
        && calibration.maximum_signal_norm
               > calibration.minimum_signal_norm
        && finite(calibration.maximum_speed_rad_s)
        && calibration.maximum_speed_rad_s > 0.0F
        && finite(calibration.speed_filter_hz)
        && calibration.speed_filter_hz > 0.0F
        && (calibration.mechanical_direction == 1
            || calibration.mechanical_direction == -1)
        && calibration.pole_pairs > 0U;
}

bool HallAngleSensor::configure(const HallCalibration &calibration)
{
    if (!valid_hall_calibration(calibration)) {
        is_configured_ = false;
        return false;
    }
    calibration_ = calibration;
    is_configured_ = true;
    reset();
    return true;
}

void HallAngleSensor::reset()
{
    previous_angle_rad_ = 0.0F;
    unwrapped_angle_rad_ = 0.0F;
    filtered_speed_rad_s_ = 0.0F;
    has_previous_sample_ = false;
}

HallStatus HallAngleSensor::update(const HallRawSample &sample,
                                   float sample_period_s,
                                   HallState &state)
{
    state = {};
    state.capture_time_us = sample.capture_time_us;
    if (!is_configured_ || sample_period_s <= 0.0F
        || !finite(sample_period_s)) {
        state.status = HallStatus::InvalidCalibration;
        return state.status;
    }
    if (!finite(sample.channel_a) || !finite(sample.channel_b)) {
        state.status = HallStatus::NonFiniteSample;
        return state.status;
    }
    const Vector2 corrected = correct_sample(sample, calibration_);
    state.signal_norm = std::hypot(corrected.x, corrected.y);
    if (!finite(state.signal_norm)
        || state.signal_norm < calibration_.minimum_signal_norm
        || state.signal_norm > calibration_.maximum_signal_norm) {
        state.status = HallStatus::SignalOutOfRange;
        return state.status;
    }

    state.hall_angle_rad = std::atan2(corrected.y, corrected.x);
    const float direction =
        static_cast<float>(calibration_.mechanical_direction);
    const float angle = math::wrap_pi(
        direction * state.hall_angle_rad
        + calibration_.mechanical_zero_rad);
    if (!has_previous_sample_) {
        previous_angle_rad_ = angle;
        unwrapped_angle_rad_ = angle;
        has_previous_sample_ = true;
    }
    const float delta = math::wrap_pi(angle - previous_angle_rad_);
    const float speed = delta / sample_period_s;
    if (std::abs(speed) > calibration_.maximum_speed_rad_s) {
        state.status = HallStatus::RateOutOfRange;
        return state.status;
    }
    previous_angle_rad_ = angle;
    unwrapped_angle_rad_ += delta;
    const float time_constant = 1.0F
        / (math::kTwoPi * calibration_.speed_filter_hz);
    const float filter_gain = sample_period_s
        / (time_constant + sample_period_s);
    filtered_speed_rad_s_ += filter_gain
        * (speed - filtered_speed_rad_s_);

    state.mechanical_angle_rad = unwrapped_angle_rad_;
    state.speed_rad_s = filtered_speed_rad_s_;
    state.electrical_angle_rad = math::wrap_pi(
        static_cast<float>(calibration_.pole_pairs)
            * state.mechanical_angle_rad
        + calibration_.electrical_zero_rad);
    if (state.mechanical_angle_rad < calibration_.mechanical_min_rad
        || state.mechanical_angle_rad > calibration_.mechanical_max_rad) {
        state.status = HallStatus::MechanicalLimit;
        return state.status;
    }
    state.status = HallStatus::Ok;
    return state.status;
}

void HallSignalCalibrator::reset()
{
    sum_a_ = 0.0;
    sum_b_ = 0.0;
    sum_aa_ = 0.0;
    sum_ab_ = 0.0;
    sum_bb_ = 0.0;
    minimum_a_ = std::numeric_limits<float>::max();
    maximum_a_ = std::numeric_limits<float>::lowest();
    minimum_b_ = std::numeric_limits<float>::max();
    maximum_b_ = std::numeric_limits<float>::lowest();
    sample_count_ = 0U;
}

bool HallSignalCalibrator::add_sample(const HallRawSample &sample)
{
    if (!finite(sample.channel_a) || !finite(sample.channel_b)
        || sample.channel_a < 0.0F || sample.channel_a > kMaximumRawSample
        || sample.channel_b < 0.0F || sample.channel_b > kMaximumRawSample) {
        return false;
    }
    sum_a_ += sample.channel_a;
    sum_b_ += sample.channel_b;
    sum_aa_ += static_cast<double>(sample.channel_a) * sample.channel_a;
    sum_ab_ += static_cast<double>(sample.channel_a) * sample.channel_b;
    sum_bb_ += static_cast<double>(sample.channel_b) * sample.channel_b;
    minimum_a_ = std::min(minimum_a_, sample.channel_a);
    maximum_a_ = std::max(maximum_a_, sample.channel_a);
    minimum_b_ = std::min(minimum_b_, sample.channel_b);
    maximum_b_ = std::max(maximum_b_, sample.channel_b);
    if (sample_count_ == UINT32_MAX) {
        return false;
    }
    ++sample_count_;
    return true;
}

bool HallSignalCalibrator::finish(HallCalibration &calibration) const
{
    if (sample_count_ < kMinimumCalibrationSamples
        || maximum_a_ - minimum_a_ < kMinimumSignalSpan
        || maximum_b_ - minimum_b_ < kMinimumSignalSpan) {
        return false;
    }
    const double inverse_count = 1.0 / sample_count_;
    const double mean_a = sum_a_ * inverse_count;
    const double mean_b = sum_b_ * inverse_count;
    const double covariance_aa = sum_aa_ * inverse_count - mean_a * mean_a;
    const double covariance_ab = sum_ab_ * inverse_count - mean_a * mean_b;
    const double covariance_bb = sum_bb_ * inverse_count - mean_b * mean_b;
    float correction[2][2] {};
    if (!covariance_whitening(covariance_aa, covariance_ab,
                              covariance_bb, correction)) {
        return false;
    }
    calibration.offset = {
        static_cast<float>(mean_a),
        static_cast<float>(mean_b),
    };
    calibration.correction[0][0] = correction[0][0];
    calibration.correction[0][1] = correction[0][1];
    calibration.correction[1][0] = correction[1][0];
    calibration.correction[1][1] = correction[1][1];
    calibration.minimum_signal_norm = 0.25F;
    calibration.maximum_signal_norm = 4.0F;
    return valid_hall_calibration(calibration);
}

} // namespace gimbal
