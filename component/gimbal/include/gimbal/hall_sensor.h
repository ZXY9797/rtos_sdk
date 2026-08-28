#pragma once

#include <gimbal/types.h>

#include <cstdint>

namespace gimbal {

struct HallRawSample {
    float channel_a {0.0F};
    float channel_b {0.0F};
    uint32_t capture_time_us {0U};
};

struct HallCalibration {
    Vector2 offset {};
    float correction[2][2] {
        {1.0F, 0.0F},
        {0.0F, 1.0F},
    };
    float mechanical_zero_rad {0.0F};
    float electrical_zero_rad {0.0F};
    float mechanical_min_rad {-3.14159265F};
    float mechanical_max_rad {3.14159265F};
    float minimum_signal_norm {0.25F};
    float maximum_signal_norm {4.0F};
    float maximum_speed_rad_s {40.0F};
    float speed_filter_hz {100.0F};
    int8_t mechanical_direction {1};
    uint8_t pole_pairs {7U};
    uint8_t reserved[2] {};
};

enum class HallStatus : uint8_t {
    Ok = 0U,
    InvalidCalibration,
    NonFiniteSample,
    SignalOutOfRange,
    RateOutOfRange,
    MechanicalLimit,
};

struct HallState {
    float hall_angle_rad {0.0F};
    float mechanical_angle_rad {0.0F};
    float electrical_angle_rad {0.0F};
    float speed_rad_s {0.0F};
    float signal_norm {0.0F};
    uint32_t capture_time_us {0U};
    HallStatus status {HallStatus::InvalidCalibration};
};

class HallAngleSensor {
public:
    [[nodiscard]] bool configure(const HallCalibration &calibration);
    void reset();
    [[nodiscard]] HallStatus update(const HallRawSample &sample,
                                    float sample_period_s,
                                    HallState &state);
    [[nodiscard]] bool is_configured() const { return is_configured_; }

private:
    HallCalibration calibration_ {};
    float previous_angle_rad_ {0.0F};
    float unwrapped_angle_rad_ {0.0F};
    float filtered_speed_rad_s_ {0.0F};
    bool is_configured_ {false};
    bool has_previous_sample_ {false};
};

class HallSignalCalibrator {
public:
    HallSignalCalibrator() { reset(); }
    void reset();
    [[nodiscard]] bool add_sample(const HallRawSample &sample);
    [[nodiscard]] bool finish(HallCalibration &calibration) const;
    [[nodiscard]] uint32_t sample_count() const { return sample_count_; }

private:
    double sum_a_ {0.0};
    double sum_b_ {0.0};
    double sum_aa_ {0.0};
    double sum_ab_ {0.0};
    double sum_bb_ {0.0};
    float minimum_a_ {0.0F};
    float maximum_a_ {0.0F};
    float minimum_b_ {0.0F};
    float maximum_b_ {0.0F};
    uint32_t sample_count_ {0U};
};

[[nodiscard]] bool valid_hall_calibration(
    const HallCalibration &calibration);

} // namespace gimbal
