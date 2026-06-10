#include "position_sensor.h"
#include <device.h>
#include <drivers_generated.h>
#include <algorithm>
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846f
#endif

void PositionSensor::init(const TmrSensorConfig &cfg) {
    cfg_ = cfg;
    if (cfg.calib_done) {
        calib_off_v1_ = cfg.offset_v1;
        calib_off_v2_ = cfg.offset_v2;
        calib_amp_v1_ = cfg.scale_v1;
        calib_amp_v2_ = cfg.scale_v2;
    }
}

bool PositionSensor::update() {
    uint16_t raw_v1 = 0, raw_v2 = 0;
    if (!read_adc(raw_v1, raw_v2)) {
        return false;
    }

    // Convert to voltage
    float v1 = static_cast<float>(raw_v1) * cfg_.v_ref / 4096.0f;
    float v2 = static_cast<float>(raw_v2) * cfg_.v_ref / 4096.0f;

    // Apply calibration: normalize to [-1, 1]
    v1_norm_ = (v1 - calib_off_v1_) * calib_amp_v1_;
    v2_norm_ = (v2 - calib_off_v2_) * calib_amp_v2_;

    v1_norm_ = std::clamp(v1_norm_, -1.0f, 1.0f);
    v2_norm_ = std::clamp(v2_norm_, -1.0f, 1.0f);

    // Compute angle
    angle_rad_ = compute_angle(v1_norm_, v2_norm_);

    // Apply zero offset
    if (has_zero_) {
        angle_rad_ -= zero_offset_;
        if (angle_rad_ < 0.0f) angle_rad_ += 2.0f * static_cast<float>(M_PI);
        if (angle_rad_ > 2.0f * static_cast<float>(M_PI)) angle_rad_ -= 2.0f * static_cast<float>(M_PI);
    }

    return true;
}

void PositionSensor::set_calib_params(float off_v1, float off_v2, float amp_v1, float amp_v2) {
    calib_off_v1_ = off_v1;
    calib_off_v2_ = off_v2;
    calib_amp_v1_ = (amp_v1 > 0.001f) ? (1.0f / amp_v1) : 1.0f;
    calib_amp_v2_ = (amp_v2 > 0.001f) ? (1.0f / amp_v2) : 1.0f;
}

bool PositionSensor::read_adc(uint16_t &v1_raw, uint16_t &v2_raw) {
    auto &adc = device_get(adc0);

    if (adc.read(static_cast<hal::AdcChannel>(cfg_.ch_v1), v1_raw) != hal::Status::Ok) {
        return false;
    }
    if (adc.read(static_cast<hal::AdcChannel>(cfg_.ch_v2), v2_raw) != hal::Status::Ok) {
        return false;
    }
    return true;
}

float PositionSensor::compute_angle(float v1, float v2) {
    float angle = atan2f(v1, v2);
    if (angle < 0.0f) angle += 2.0f * static_cast<float>(M_PI);
    return angle;
}

float PositionSensor::raw_voltage_v1() const {
    return v1_norm_ / calib_amp_v1_ + calib_off_v1_;
}

float PositionSensor::raw_voltage_v2() const {
    return v2_norm_ / calib_amp_v2_ + calib_off_v2_;
}
