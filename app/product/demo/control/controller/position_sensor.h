#pragma once

#include <cstdint>
#include <cmath>

struct TmrSensorConfig {
    uint8_t adc_port {0};
    uint8_t ch_v1 {5};
    uint8_t ch_v2 {6};
    float v_ref {3.3f};
    float offset_v1 {1.65f};
    float offset_v2 {1.65f};
    float scale_v1 {1.0f};
    float scale_v2 {1.0f};
    bool calib_done {false};
};

class PositionSensor {
public:
    void init(const TmrSensorConfig &cfg);
    bool update();

    float angle_rad() const { return angle_rad_; }
    float v1_normalized() const { return v1_norm_; }
    float v2_normalized() const { return v2_norm_; }

    void set_zero_offset(float offset) { zero_offset_ = offset; has_zero_ = true; }
    void set_calib_params(float off_v1, float off_v2, float amp_v1, float amp_v2);

    float zero_offset() const { return zero_offset_; }
    bool is_calibrated() const { return has_zero_; }

    // 原始电压（用于校准采集）
    float raw_voltage_v1() const;
    float raw_voltage_v2() const;

private:
    TmrSensorConfig cfg_;
    float angle_rad_ {0.0f};
    float v1_norm_ {0.0f};
    float v2_norm_ {0.0f};
    float zero_offset_ {0.0f};
    bool has_zero_ {false};

    float calib_off_v1_ {0.0f};
    float calib_off_v2_ {0.0f};
    float calib_amp_v1_ {1.0f};
    float calib_amp_v2_ {1.0f};

    bool read_adc(uint16_t &v1_raw, uint16_t &v2_raw);
    float compute_angle(float v1, float v2);
};
