#pragma once

#include <algo/attitude/attitude_ekf.h>
#include <gimbal/types.h>

namespace gimbal {

// Kept in the persisted factory parameter layout.
struct ImuCalibration {
    Vector3 gyro_bias_rad_s {};
    Vector3 gyro_temperature_slope {};
    Vector3 accelerometer_bias_mps2 {};
    Matrix3 gyro_sensor_to_camera {};
    Matrix3 accelerometer_sensor_to_camera {};
    float reference_temperature_c {45.0F};
};

using algo::attitude::AttitudeEkfConfig;
using algo::attitude::valid_attitude_ekf_config;

[[nodiscard]] bool valid_imu_calibration(const ImuCalibration &calibration);

class AttitudeEkf {
public:
    [[nodiscard]] bool configure(const AttitudeEkfConfig &config,
                                 const ImuCalibration &calibration);
    void reset(const Quaternion &camera_in_world = {}) { filter_.reset(camera_in_world); }
    [[nodiscard]] bool predict(const ImuSample &sample, float sample_period_s);
    [[nodiscard]] bool update_accelerometer(const ImuSample &sample);
    [[nodiscard]] bool update_stationary_bias(const ImuSample &sample);
    [[nodiscard]] AttitudeState state(uint32_t publish_time_us) const;

private:
    algo::attitude::AttitudeEkf filter_ {};
};

} // namespace gimbal
