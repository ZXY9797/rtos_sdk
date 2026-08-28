#pragma once

#include <gimbal/dynamics.h>
#include <gimbal/types.h>

#include <array>

namespace gimbal {

struct AxisControllerConfig {
    float attitude_gain {8.0F};
    float rate_proportional_gain {0.8F};
    float rate_integral_gain {0.2F};
    float integral_limit {1.0F};
    float command_limit {4.0F};
    float notch_frequency_hz {0.0F};
    float notch_quality_factor {3.0F};
    float lead_zero_frequency_hz {0.0F};
    float lead_pole_frequency_hz {0.0F};
    float low_pass_frequency_hz {80.0F};
};

struct ControllerConfig {
    std::array<AxisControllerConfig, kAxisCount> axis {};
    Matrix3 decoupling_matrix {};
    float sample_frequency_hz {1000.0F};
};

struct ControllerInput {
    const MotionReference &reference;
    const AttitudeState &attitude;
    const DynamicsOutput &feedforward;
    const Matrix3 &camera_effort_to_joint;
    float sample_period_s;
    uint32_t publish_time_us;
};

class TwoDegreeController {
public:
    [[nodiscard]] bool configure(const ControllerConfig &config,
                                 MotorControlMode mode);
    void reset();
    [[nodiscard]] MotorCommand idle(uint32_t publish_time_us);
    [[nodiscard]] MotorCommand update(const ControllerInput &input);

private:
    struct DigitalFilter {
        float b0 {1.0F};
        float b1 {0.0F};
        float b2 {0.0F};
        float a1 {0.0F};
        float a2 {0.0F};
        float state1 {0.0F};
        float state2 {0.0F};

        [[nodiscard]] float update(float input);
        void reset();
    };

    struct AxisFilters {
        DigitalFilter notch {};
        DigitalFilter lead {};
        DigitalFilter low_pass {};
    };

    [[nodiscard]] bool configure_filters();
    [[nodiscard]] bool calculate_feedback(
        const ControllerInput &input, Vector3 &feedback);

    ControllerConfig config_ {};
    std::array<AxisFilters, kAxisCount> filters_ {};
    Vector3 integral_ {};
    MotorControlMode mode_ {MotorControlMode::VoltageFoc};
    uint32_t sequence_ {0U};
    bool is_configured_ {false};
};

[[nodiscard]] bool valid_controller_config(const ControllerConfig &config);

} // namespace gimbal
