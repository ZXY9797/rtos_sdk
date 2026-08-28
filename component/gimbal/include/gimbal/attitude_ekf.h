#pragma once

#include <gimbal/types.h>

namespace gimbal {

struct ImuCalibration {
    Vector3 gyro_bias_rad_s {};
    Vector3 gyro_temperature_slope {};
    Vector3 accelerometer_bias_mps2 {};
    Matrix3 gyro_sensor_to_camera {};
    Matrix3 accelerometer_sensor_to_camera {};
    float reference_temperature_c {45.0F};
};

struct AttitudeEkfConfig {
    float gyro_noise_rad_s {0.003F};
    float gyro_bias_noise_rad_s2 {0.00005F};
    float accelerometer_noise {0.04F};
    float stationary_gyro_noise_rad_s {0.001F};
    float stationary_gyro_threshold_rad_s {0.05F};
    float stationary_acceleration_gate_mps2 {0.3F};
    float accelerometer_gate_mps2 {1.5F};
    float maximum_gyro_rate_rad_s {50.0F};
    float maximum_sample_period_s {0.01F};
};

class AttitudeEkf {
public:
    [[nodiscard]] bool configure(const AttitudeEkfConfig &config,
                                 const ImuCalibration &calibration);
    void reset(const Quaternion &camera_in_world = {});
    [[nodiscard]] bool predict(const ImuSample &sample,
                               float sample_period_s);
    [[nodiscard]] bool update_accelerometer(const ImuSample &sample);
    [[nodiscard]] bool update_stationary_bias(const ImuSample &sample);
    [[nodiscard]] AttitudeState state(uint32_t publish_time_us) const;

private:
    [[nodiscard]] Vector3 corrected_gyro(const ImuSample &sample) const;
    void apply_attitude_error(const Vector3 &error);

    AttitudeEkfConfig config_ {};
    ImuCalibration calibration_ {};
    Quaternion attitude_ {};
    Vector3 gyro_bias_ {};
    Vector3 angular_rate_body_ {};
    float covariance_[6][6] {};
    uint32_t sequence_ {0U};
    uint32_t capture_time_us_ {0U};
    bool is_configured_ {false};
    bool is_valid_ {false};
};

[[nodiscard]] bool valid_attitude_ekf_config(
    const AttitudeEkfConfig &config);
[[nodiscard]] bool valid_imu_calibration(
    const ImuCalibration &calibration);

} // namespace gimbal
