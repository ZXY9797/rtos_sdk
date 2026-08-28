#include <gimbal/attitude_ekf.h>

#include <gimbal/math.h>

#include <algorithm>
#include <array>
#include <cmath>

namespace gimbal {
namespace {

constexpr size_t kStateCount = 6U;
constexpr size_t kMeasurementCount = 3U;
constexpr size_t kBiasOffset = 3U;
constexpr float kMinimumNoise = 1.0e-9F;
constexpr float kMinimumVariance = 1.0e-10F;
constexpr float kMaximumVariance = 10.0F;
constexpr double kMinimumDeterminant = 1.0e-30;

using StateVector = std::array<float, kStateCount>;
using ObservationMatrix =
    std::array<std::array<float, kStateCount>, kMeasurementCount>;
using StateMatrix =
    std::array<std::array<float, kStateCount>, kStateCount>;

[[nodiscard]] bool finite_vector(const Vector3 &value)
{
    return std::isfinite(value.x) && std::isfinite(value.y)
        && std::isfinite(value.z);
}

[[nodiscard]] Vector3 calibrated_acceleration(
    const ImuSample &sample, const ImuCalibration &calibration)
{
    return math::subtract(
        math::multiply(calibration.accelerometer_sensor_to_camera,
                       sample.acceleration_mps2),
        calibration.accelerometer_bias_mps2);
}

[[nodiscard]] Vector3 calibrated_gyro(
    const ImuSample &sample, const ImuCalibration &calibration)
{
    return math::multiply(
        calibration.gyro_sensor_to_camera,
        sample.angular_rate_rad_s);
}

[[nodiscard]] bool valid_calibration_matrix(const Matrix3 &matrix)
{
    for (const auto &row : matrix.value) {
        const Vector3 row_vector {row[0], row[1], row[2]};
        const float row_norm = math::norm(row_vector);
        if (!std::isfinite(row_norm)
            || row_norm < 0.5F || row_norm > 1.5F) {
            return false;
        }
    }
    const float determinant = math::determinant(matrix);
    return std::isfinite(determinant)
        && determinant > 0.1F && determinant < 10.0F;
}

[[nodiscard]] bool invert_matrix3(
    const float input[3][3], float output[3][3])
{
    const double determinant =
        static_cast<double>(input[0][0])
            * (static_cast<double>(input[1][1]) * input[2][2]
               - static_cast<double>(input[1][2]) * input[2][1])
        - static_cast<double>(input[0][1])
            * (static_cast<double>(input[1][0]) * input[2][2]
               - static_cast<double>(input[1][2]) * input[2][0])
        + static_cast<double>(input[0][2])
            * (static_cast<double>(input[1][0]) * input[2][1]
               - static_cast<double>(input[1][1]) * input[2][0]);
    if (!std::isfinite(determinant)
        || std::abs(determinant) <= kMinimumDeterminant) {
        return false;
    }
    const float inverse = static_cast<float>(1.0 / determinant);
    output[0][0] = (input[1][1] * input[2][2]
                    - input[1][2] * input[2][1]) * inverse;
    output[0][1] = (input[0][2] * input[2][1]
                    - input[0][1] * input[2][2]) * inverse;
    output[0][2] = (input[0][1] * input[1][2]
                    - input[0][2] * input[1][1]) * inverse;
    output[1][0] = (input[1][2] * input[2][0]
                    - input[1][0] * input[2][2]) * inverse;
    output[1][1] = (input[0][0] * input[2][2]
                    - input[0][2] * input[2][0]) * inverse;
    output[1][2] = (input[0][2] * input[1][0]
                    - input[0][0] * input[1][2]) * inverse;
    output[2][0] = (input[1][0] * input[2][1]
                    - input[1][1] * input[2][0]) * inverse;
    output[2][1] = (input[0][1] * input[2][0]
                    - input[0][0] * input[2][1]) * inverse;
    output[2][2] = (input[0][0] * input[1][1]
                    - input[0][1] * input[1][0]) * inverse;
    for (size_t row = 0U; row < kMeasurementCount; ++row) {
        for (size_t column = 0U;
             column < kMeasurementCount; ++column) {
            if (!std::isfinite(output[row][column])) return false;
        }
    }
    return true;
}

void form_innovation_covariance(
    const float covariance[6][6], const ObservationMatrix &observation,
    float measurement_variance, float innovation[3][3])
{
    for (size_t row = 0U; row < kMeasurementCount; ++row) {
        for (size_t column = 0U; column < kMeasurementCount; ++column) {
            float value = row == column ? measurement_variance : 0.0F;
            for (size_t left = 0U; left < kStateCount; ++left) {
                for (size_t right = 0U; right < kStateCount; ++right) {
                    value += observation[row][left]
                        * covariance[left][right]
                        * observation[column][right];
                }
            }
            innovation[row][column] = value;
        }
    }
}

void form_kalman_gain(
    const float covariance[6][6], const ObservationMatrix &observation,
    const float inverse_innovation[3][3], float gain[6][3])
{
    float covariance_observation[6][3] {};
    for (size_t row = 0U; row < kStateCount; ++row) {
        for (size_t column = 0U; column < kMeasurementCount; ++column) {
            for (size_t inner = 0U; inner < kStateCount; ++inner) {
                covariance_observation[row][column] +=
                    covariance[row][inner] * observation[column][inner];
            }
        }
    }
    for (size_t row = 0U; row < kStateCount; ++row) {
        for (size_t column = 0U; column < kMeasurementCount; ++column) {
            gain[row][column] = 0.0F;
            for (size_t inner = 0U;
                 inner < kMeasurementCount; ++inner) {
                gain[row][column] += covariance_observation[row][inner]
                    * inverse_innovation[inner][column];
            }
        }
    }
}

[[nodiscard]] bool joseph_update(
    float covariance[6][6], const ObservationMatrix &observation,
    const float gain[6][3], float measurement_variance)
{
    StateMatrix transform {};
    StateMatrix intermediate {};
    StateMatrix updated {};
    for (size_t row = 0U; row < kStateCount; ++row) {
        for (size_t column = 0U; column < kStateCount; ++column) {
            transform[row][column] = row == column ? 1.0F : 0.0F;
            for (size_t inner = 0U;
                 inner < kMeasurementCount; ++inner) {
                transform[row][column] -=
                    gain[row][inner] * observation[inner][column];
            }
        }
    }
    for (size_t row = 0U; row < kStateCount; ++row) {
        for (size_t column = 0U; column < kStateCount; ++column) {
            for (size_t inner = 0U; inner < kStateCount; ++inner) {
                intermediate[row][column] +=
                    transform[row][inner] * covariance[inner][column];
            }
        }
    }
    for (size_t row = 0U; row < kStateCount; ++row) {
        for (size_t column = 0U; column < kStateCount; ++column) {
            for (size_t inner = 0U; inner < kStateCount; ++inner) {
                updated[row][column] += intermediate[row][inner]
                    * transform[column][inner];
            }
            for (size_t inner = 0U;
                 inner < kMeasurementCount; ++inner) {
                updated[row][column] += measurement_variance
                    * gain[row][inner] * gain[column][inner];
            }
        }
    }
    for (size_t row = 0U; row < kStateCount; ++row) {
        for (size_t column = 0U; column < kStateCount; ++column) {
            const float symmetric = 0.5F
                * (updated[row][column] + updated[column][row]);
            if (!std::isfinite(symmetric)) return false;
            covariance[row][column] = symmetric;
        }
        covariance[row][row] = math::clamp(
            covariance[row][row], kMinimumVariance, kMaximumVariance);
    }
    return true;
}

[[nodiscard]] bool measurement_update(
    float covariance[6][6], const ObservationMatrix &observation,
    const Vector3 &residual, float measurement_variance,
    StateVector &correction)
{
    float innovation[3][3] {};
    float inverse_innovation[3][3] {};
    float gain[6][3] {};
    form_innovation_covariance(
        covariance, observation, measurement_variance, innovation);
    if (!invert_matrix3(innovation, inverse_innovation)) return false;
    form_kalman_gain(
        covariance, observation, inverse_innovation, gain);
    for (size_t row = 0U; row < kStateCount; ++row) {
        correction[row] = gain[row][0] * residual.x
            + gain[row][1] * residual.y
            + gain[row][2] * residual.z;
        if (!std::isfinite(correction[row])) return false;
    }
    return joseph_update(
        covariance, observation, gain, measurement_variance);
}

[[nodiscard]] bool propagate_covariance(
    float covariance[6][6], const Vector3 &rate,
    float sample_period_s, float gyro_variance,
    float bias_variance)
{
    StateMatrix transition {};
    StateMatrix intermediate {};
    StateMatrix predicted {};
    StateMatrix stabilized {};
    for (size_t index = 0U; index < kStateCount; ++index) {
        transition[index][index] = 1.0F;
    }
    transition[0][1] = rate.z * sample_period_s;
    transition[0][2] = -rate.y * sample_period_s;
    transition[1][0] = -rate.z * sample_period_s;
    transition[1][2] = rate.x * sample_period_s;
    transition[2][0] = rate.y * sample_period_s;
    transition[2][1] = -rate.x * sample_period_s;
    for (size_t axis = 0U; axis < kMeasurementCount; ++axis) {
        transition[axis][axis + kBiasOffset] = -sample_period_s;
    }
    for (size_t row = 0U; row < kStateCount; ++row) {
        for (size_t column = 0U; column < kStateCount; ++column) {
            for (size_t inner = 0U; inner < kStateCount; ++inner) {
                intermediate[row][column] +=
                    transition[row][inner] * covariance[inner][column];
            }
        }
    }
    for (size_t row = 0U; row < kStateCount; ++row) {
        for (size_t column = 0U; column < kStateCount; ++column) {
            for (size_t inner = 0U; inner < kStateCount; ++inner) {
                predicted[row][column] += intermediate[row][inner]
                    * transition[column][inner];
            }
        }
    }
    for (size_t axis = 0U; axis < kMeasurementCount; ++axis) {
        predicted[axis][axis] += gyro_variance * sample_period_s;
        predicted[axis + kBiasOffset][axis + kBiasOffset] +=
            bias_variance * sample_period_s;
    }
    for (size_t row = 0U; row < kStateCount; ++row) {
        for (size_t column = 0U; column < kStateCount; ++column) {
            const float symmetric = 0.5F
                * (predicted[row][column] + predicted[column][row]);
            if (!std::isfinite(symmetric)) return false;
            stabilized[row][column] = math::clamp(
                symmetric, -kMaximumVariance, kMaximumVariance);
        }
        stabilized[row][row] = math::clamp(
            stabilized[row][row], kMinimumVariance, kMaximumVariance);
    }
    for (size_t row = 0U; row < kStateCount; ++row) {
        for (size_t column = 0U; column < kStateCount; ++column) {
            covariance[row][column] = stabilized[row][column];
        }
    }
    return true;
}

} // namespace

bool valid_attitude_ekf_config(const AttitudeEkfConfig &config)
{
    return std::isfinite(config.gyro_noise_rad_s)
        && config.gyro_noise_rad_s > kMinimumNoise
        && std::isfinite(config.gyro_bias_noise_rad_s2)
        && config.gyro_bias_noise_rad_s2 > kMinimumNoise
        && std::isfinite(config.accelerometer_noise)
        && config.accelerometer_noise > kMinimumNoise
        && std::isfinite(config.stationary_gyro_noise_rad_s)
        && config.stationary_gyro_noise_rad_s > kMinimumNoise
        && std::isfinite(config.stationary_gyro_threshold_rad_s)
        && config.stationary_gyro_threshold_rad_s
            > config.stationary_gyro_noise_rad_s
        && std::isfinite(config.stationary_acceleration_gate_mps2)
        && config.stationary_acceleration_gate_mps2 > 0.0F
        && std::isfinite(config.accelerometer_gate_mps2)
        && config.accelerometer_gate_mps2
            >= config.stationary_acceleration_gate_mps2
        && std::isfinite(config.maximum_gyro_rate_rad_s)
        && config.maximum_gyro_rate_rad_s > 0.0F
        && std::isfinite(config.maximum_sample_period_s)
        && config.maximum_sample_period_s > 0.0F;
}

bool valid_imu_calibration(const ImuCalibration &calibration)
{
    return finite_vector(calibration.gyro_bias_rad_s)
        && finite_vector(calibration.gyro_temperature_slope)
        && finite_vector(calibration.accelerometer_bias_mps2)
        && valid_calibration_matrix(calibration.gyro_sensor_to_camera)
        && valid_calibration_matrix(
            calibration.accelerometer_sensor_to_camera)
        && std::isfinite(calibration.reference_temperature_c)
        && calibration.reference_temperature_c >= -60.0F
        && calibration.reference_temperature_c <= 150.0F;
}

bool AttitudeEkf::configure(const AttitudeEkfConfig &config,
                            const ImuCalibration &calibration)
{
    if (!valid_attitude_ekf_config(config)
        || !valid_imu_calibration(calibration)) {
        is_configured_ = false;
        return false;
    }
    config_ = config;
    calibration_ = calibration;
    is_configured_ = true;
    reset();
    return true;
}

void AttitudeEkf::reset(const Quaternion &camera_in_world)
{
    attitude_ = math::normalized(camera_in_world);
    gyro_bias_ = calibration_.gyro_bias_rad_s;
    angular_rate_body_ = {};
    for (auto &row : covariance_) {
        for (float &value : row) value = 0.0F;
    }
    covariance_[0][0] = 0.01F;
    covariance_[1][1] = 0.01F;
    covariance_[2][2] = 0.1F;
    covariance_[3][3] = 0.001F;
    covariance_[4][4] = 0.001F;
    covariance_[5][5] = 0.001F;
    sequence_ = 0U;
    capture_time_us_ = 0U;
    is_valid_ = is_configured_;
}

Vector3 AttitudeEkf::corrected_gyro(const ImuSample &sample) const
{
    const float temperature_delta = sample.temperature_c
        - calibration_.reference_temperature_c;
    const Vector3 temperature_bias = math::scale(
        calibration_.gyro_temperature_slope, temperature_delta);
    return math::subtract(
        calibrated_gyro(sample, calibration_),
        math::add(gyro_bias_, temperature_bias));
}

bool AttitudeEkf::predict(const ImuSample &sample, float sample_period_s)
{
    if (!is_configured_ || sample.header.health != Health::Valid
        || !finite_vector(sample.angular_rate_rad_s)
        || !std::isfinite(sample.temperature_c)
        || !std::isfinite(sample_period_s) || sample_period_s <= 0.0F
        || sample_period_s > config_.maximum_sample_period_s) {
        is_valid_ = false;
        return false;
    }
    angular_rate_body_ = corrected_gyro(sample);
    if (math::norm(angular_rate_body_)
            > config_.maximum_gyro_rate_rad_s
        || !propagate_covariance(
            covariance_, angular_rate_body_, sample_period_s,
            config_.gyro_noise_rad_s * config_.gyro_noise_rad_s,
            config_.gyro_bias_noise_rad_s2
                * config_.gyro_bias_noise_rad_s2)) {
        is_valid_ = false;
        return false;
    }
    const Vector3 increment = math::scale(
        angular_rate_body_, sample_period_s);
    attitude_ = math::normalized(math::multiply(
        attitude_, math::from_rotation_vector(increment)));
    capture_time_us_ = sample.header.capture_time_us;
    ++sequence_;
    is_valid_ = true;
    return true;
}

bool AttitudeEkf::update_accelerometer(const ImuSample &sample)
{
    if (!is_valid_ || !finite_vector(sample.acceleration_mps2)) {
        return false;
    }
    const Vector3 acceleration = calibrated_acceleration(
        sample, calibration_);
    const float acceleration_norm = math::norm(acceleration);
    if (std::abs(acceleration_norm - math::kGravityMps2)
        > config_.accelerometer_gate_mps2) {
        return false;
    }
    const Vector3 measured = math::normalized(acceleration);
    const Vector3 predicted = math::rotate(
        math::conjugate(attitude_), {0.0F, 0.0F, 1.0F});
    ObservationMatrix observation {};
    observation[0][1] = -predicted.z;
    observation[0][2] = predicted.y;
    observation[1][0] = predicted.z;
    observation[1][2] = -predicted.x;
    observation[2][0] = -predicted.y;
    observation[2][1] = predicted.x;
    const Vector3 residual = math::subtract(measured, predicted);
    StateVector correction {};
    const float variance = config_.accelerometer_noise
        * config_.accelerometer_noise;
    if (!measurement_update(
            covariance_, observation, residual, variance, correction)) {
        is_valid_ = false;
        return false;
    }
    apply_attitude_error(
        {correction[0], correction[1], correction[2]});
    gyro_bias_ = math::add(
        gyro_bias_,
        {correction[3], correction[4], correction[5]});
    return true;
}

bool AttitudeEkf::update_stationary_bias(const ImuSample &sample)
{
    if (!is_valid_ || sample.header.health != Health::Valid
        || !finite_vector(sample.angular_rate_rad_s)
        || !finite_vector(sample.acceleration_mps2)
        || !std::isfinite(sample.temperature_c)) {
        return false;
    }
    const Vector3 acceleration = calibrated_acceleration(
        sample, calibration_);
    if (std::abs(math::norm(acceleration) - math::kGravityMps2)
            > config_.stationary_acceleration_gate_mps2
        || math::norm(corrected_gyro(sample))
            > config_.stationary_gyro_threshold_rad_s) {
        return false;
    }
    const float temperature_delta = sample.temperature_c
        - calibration_.reference_temperature_c;
    const Vector3 temperature_bias = math::scale(
        calibration_.gyro_temperature_slope, temperature_delta);
    const Vector3 observed_bias = math::subtract(
        calibrated_gyro(sample, calibration_), temperature_bias);
    const Vector3 residual = math::subtract(observed_bias, gyro_bias_);
    ObservationMatrix observation {};
    for (size_t axis = 0U; axis < kMeasurementCount; ++axis) {
        observation[axis][axis + kBiasOffset] = 1.0F;
    }
    StateVector correction {};
    const float variance = config_.stationary_gyro_noise_rad_s
        * config_.stationary_gyro_noise_rad_s;
    if (!measurement_update(
            covariance_, observation, residual, variance, correction)) {
        is_valid_ = false;
        return false;
    }
    apply_attitude_error(
        {correction[0], correction[1], correction[2]});
    gyro_bias_ = math::add(
        gyro_bias_,
        {correction[3], correction[4], correction[5]});
    return true;
}

void AttitudeEkf::apply_attitude_error(const Vector3 &error)
{
    attitude_ = math::normalized(math::multiply(
        attitude_, math::from_rotation_vector(error)));
}

AttitudeState AttitudeEkf::state(uint32_t publish_time_us) const
{
    AttitudeState output {};
    output.header.sequence = sequence_;
    output.header.capture_time_us = capture_time_us_;
    output.header.publish_time_us = publish_time_us;
    const float prediction_period_s = static_cast<float>(
        publish_time_us - capture_time_us_) * 1.0e-6F;
    const bool output_valid = is_valid_
        && prediction_period_s <= config_.maximum_sample_period_s;
    output.header.valid_flags = output_valid ? kAllValidFlags : 0U;
    output.header.health = output_valid ? Health::Valid : Health::Invalid;
    output.camera_in_world = output_valid
        ? math::normalized(math::multiply(
            attitude_, math::from_rotation_vector(math::scale(
                angular_rate_body_, prediction_period_s))))
        : attitude_;
    output.angular_rate_world_rad_s = math::rotate(
        output.camera_in_world, angular_rate_body_);
    output.gyro_bias_rad_s = gyro_bias_;
    output.attitude_variance = {
        covariance_[0][0], covariance_[1][1], covariance_[2][2],
    };
    return output;
}

} // namespace gimbal
