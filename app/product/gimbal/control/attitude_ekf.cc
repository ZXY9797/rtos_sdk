#include <gimbal/attitude_ekf.h>

namespace gimbal {
namespace {

algo::attitude::ImuCalibration to_body_calibration(const ImuCalibration &calibration)
{
    return {calibration.gyro_bias_rad_s, calibration.gyro_temperature_slope,
            calibration.accelerometer_bias_mps2, calibration.gyro_sensor_to_camera,
            calibration.accelerometer_sensor_to_camera, calibration.reference_temperature_c};
}

algo::attitude::Sample to_filter_sample(const ImuSample &sample)
{
    return {sample.acceleration_mps2, sample.angular_rate_rad_s, sample.temperature_c,
            sample.header.capture_time_us, sample.header.health == Health::Valid};
}

} // namespace

bool valid_imu_calibration(const ImuCalibration &calibration)
{
    return algo::attitude::valid_imu_calibration(to_body_calibration(calibration));
}

bool AttitudeEkf::configure(const AttitudeEkfConfig &config, const ImuCalibration &calibration)
{
    return filter_.configure(config, to_body_calibration(calibration));
}

bool AttitudeEkf::predict(const ImuSample &sample, float sample_period_s)
{
    return filter_.predict(to_filter_sample(sample), sample_period_s);
}

bool AttitudeEkf::update_accelerometer(const ImuSample &sample)
{
    return filter_.update_accelerometer(to_filter_sample(sample));
}

bool AttitudeEkf::update_stationary_bias(const ImuSample &sample)
{
    return filter_.update_stationary_bias(to_filter_sample(sample));
}

AttitudeState AttitudeEkf::state(uint32_t publish_time_us) const
{
    const auto estimate = filter_.state(publish_time_us);
    AttitudeState output {};
    output.header.sequence = estimate.sequence;
    output.header.capture_time_us = estimate.capture_time_us;
    output.header.publish_time_us = publish_time_us;
    output.header.valid_flags = estimate.valid ? kAllValidFlags : 0U;
    output.header.health = estimate.valid ? Health::Valid : Health::Invalid;
    output.camera_in_world = estimate.body_in_world;
    output.angular_rate_world_rad_s = estimate.angular_rate_world_rad_s;
    output.gyro_bias_rad_s = estimate.gyro_bias_rad_s;
    output.attitude_variance = estimate.attitude_variance;
    return output;
}

} // namespace gimbal
