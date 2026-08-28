#include <gimbal/hall_calibration.h>

#include <gimbal/math.h>

#include <algorithm>
#include <cmath>

namespace gimbal {
namespace {

constexpr uint32_t kMinimumZeroSamples = 16U;
constexpr float kMinimumDirectionTravelRad = 0.05F;

} // namespace

void HallZeroCalibrator::reset()
{
    mechanical_ = {};
    electrical_ = {};
    mechanical_direction_ = 0;
    pole_pairs_ = 0U;
}

bool HallZeroCalibrator::add_offset(
    CircularAccumulator &accumulator, float offset_rad)
{
    if (!std::isfinite(offset_rad)
        || accumulator.sample_count == UINT32_MAX) {
        return false;
    }
    accumulator.sine_sum += std::sin(offset_rad);
    accumulator.cosine_sum += std::cos(offset_rad);
    ++accumulator.sample_count;
    return true;
}

bool HallZeroCalibrator::add_mechanical_observation(
    float hall_angle_rad, float mechanical_reference_rad,
    int8_t mechanical_direction)
{
    if (!std::isfinite(hall_angle_rad)
        || !std::isfinite(mechanical_reference_rad)
        || (mechanical_direction != 1 && mechanical_direction != -1)
        || (mechanical_direction_ != 0
            && mechanical_direction_ != mechanical_direction)) {
        return false;
    }
    mechanical_direction_ = mechanical_direction;
    const float offset = mechanical_reference_rad
        - static_cast<float>(mechanical_direction) * hall_angle_rad;
    return add_offset(mechanical_, math::wrap_pi(offset));
}

bool HallZeroCalibrator::add_electrical_observation(
    float mechanical_angle_rad, float electrical_reference_rad,
    uint8_t pole_pairs)
{
    if (!std::isfinite(mechanical_angle_rad)
        || !std::isfinite(electrical_reference_rad)
        || pole_pairs == 0U
        || (pole_pairs_ != 0U && pole_pairs_ != pole_pairs)) {
        return false;
    }
    pole_pairs_ = pole_pairs;
    const float offset = electrical_reference_rad
        - static_cast<float>(pole_pairs) * mechanical_angle_rad;
    return add_offset(electrical_, math::wrap_pi(offset));
}

bool HallZeroCalibrator::estimate(
    const CircularAccumulator &accumulator,
    float minimum_concentration, float &mean_rad,
    HallZeroCalibrationQuality &quality)
{
    quality = {};
    if (accumulator.sample_count < kMinimumZeroSamples
        || !std::isfinite(minimum_concentration)
        || minimum_concentration <= 0.0F
        || minimum_concentration > 1.0F) {
        return false;
    }
    const double magnitude = std::hypot(
        accumulator.sine_sum, accumulator.cosine_sum);
    const double concentration = magnitude / accumulator.sample_count;
    if (!std::isfinite(concentration)
        || concentration < minimum_concentration
        || concentration > 1.000001) {
        return false;
    }
    mean_rad = static_cast<float>(std::atan2(
        accumulator.sine_sum, accumulator.cosine_sum));
    const double bounded = std::clamp(concentration, 1.0e-12, 1.0);
    quality.sample_count = accumulator.sample_count;
    quality.concentration = static_cast<float>(bounded);
    quality.circular_standard_deviation_rad = static_cast<float>(
        std::sqrt(std::max(0.0, -2.0 * std::log(bounded))));
    return std::isfinite(mean_rad)
        && std::isfinite(quality.circular_standard_deviation_rad);
}

bool HallZeroCalibrator::finish_mechanical(
    HallCalibration &calibration, float minimum_concentration,
    HallZeroCalibrationQuality &quality) const
{
    float zero_rad = 0.0F;
    if (mechanical_direction_ == 0
        || !estimate(mechanical_, minimum_concentration,
                     zero_rad, quality)) {
        return false;
    }
    HallCalibration candidate = calibration;
    candidate.mechanical_direction = mechanical_direction_;
    candidate.mechanical_zero_rad = zero_rad;
    if (!valid_hall_calibration(candidate)) {
        return false;
    }
    calibration = candidate;
    return true;
}

bool HallZeroCalibrator::finish_electrical(
    HallCalibration &calibration, float minimum_concentration,
    HallZeroCalibrationQuality &quality) const
{
    float zero_rad = 0.0F;
    if (pole_pairs_ == 0U
        || !estimate(electrical_, minimum_concentration,
                     zero_rad, quality)) {
        return false;
    }
    HallCalibration candidate = calibration;
    candidate.pole_pairs = pole_pairs_;
    candidate.electrical_zero_rad = zero_rad;
    if (!valid_hall_calibration(candidate)) {
        return false;
    }
    calibration = candidate;
    return true;
}

bool HallZeroCalibrator::infer_mechanical_direction(
    float first_hall_angle_rad, float second_hall_angle_rad,
    float first_reference_rad, float second_reference_rad,
    int8_t &direction)
{
    if (!std::isfinite(first_hall_angle_rad)
        || !std::isfinite(second_hall_angle_rad)
        || !std::isfinite(first_reference_rad)
        || !std::isfinite(second_reference_rad)) {
        return false;
    }
    const float hall_travel = math::wrap_pi(
        second_hall_angle_rad - first_hall_angle_rad);
    const float reference_travel = math::wrap_pi(
        second_reference_rad - first_reference_rad);
    if (std::abs(hall_travel) < kMinimumDirectionTravelRad
        || std::abs(reference_travel) < kMinimumDirectionTravelRad) {
        return false;
    }
    direction = hall_travel * reference_travel >= 0.0F ? 1 : -1;
    return true;
}

} // namespace gimbal
