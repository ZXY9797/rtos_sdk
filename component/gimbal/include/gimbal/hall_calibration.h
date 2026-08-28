#pragma once

#include <gimbal/hall_sensor.h>

#include <cstdint>

namespace gimbal {

struct HallZeroCalibrationQuality {
    uint32_t sample_count {0U};
    float concentration {0.0F};
    float circular_standard_deviation_rad {0.0F};
};

class HallZeroCalibrator {
public:
    HallZeroCalibrator() { reset(); }

    void reset();
    [[nodiscard]] bool add_mechanical_observation(
        float hall_angle_rad, float mechanical_reference_rad,
        int8_t mechanical_direction);
    [[nodiscard]] bool add_electrical_observation(
        float mechanical_angle_rad, float electrical_reference_rad,
        uint8_t pole_pairs);
    [[nodiscard]] bool finish_mechanical(
        HallCalibration &calibration, float minimum_concentration,
        HallZeroCalibrationQuality &quality) const;
    [[nodiscard]] bool finish_electrical(
        HallCalibration &calibration, float minimum_concentration,
        HallZeroCalibrationQuality &quality) const;

    [[nodiscard]] static bool infer_mechanical_direction(
        float first_hall_angle_rad, float second_hall_angle_rad,
        float first_reference_rad, float second_reference_rad,
        int8_t &direction);

private:
    struct CircularAccumulator {
        double sine_sum {0.0};
        double cosine_sum {0.0};
        uint32_t sample_count {0U};
    };

    [[nodiscard]] static bool add_offset(
        CircularAccumulator &accumulator, float offset_rad);
    [[nodiscard]] static bool estimate(
        const CircularAccumulator &accumulator,
        float minimum_concentration, float &mean_rad,
        HallZeroCalibrationQuality &quality);

    CircularAccumulator mechanical_ {};
    CircularAccumulator electrical_ {};
    int8_t mechanical_direction_ {0};
    uint8_t pole_pairs_ {0U};
};

} // namespace gimbal
