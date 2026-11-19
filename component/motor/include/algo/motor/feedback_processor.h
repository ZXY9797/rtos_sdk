#pragma once
#include <algo/motor/parameters.h>

namespace algo::motor {

struct AdcFormat {
    uint8_t resolution_bits {12U};
    uint32_t oversample_ratio {1U};
    uint8_t oversample_shift {0U};
    uint32_t reference_mv {3300U};
};

class AdcScale {
public:
    bool configure(const AdcFormat& format);
    float voltage(uint32_t raw) const { return float(raw) * scale_; }
    uint32_t full_scale() const { return full_scale_; }
private:
    uint32_t full_scale_ {0U};
    float scale_ {0.0F};
};

// Owns filter/unwrapping state. The optional output LUT view is borrowed; its
// owner must quiesce updates before changing/detaching/publishing that view.
class FeedbackProcessor {
public:
    bool configure(const MotorParameters& parameters, const MotorElecCalibData& electrical,
                   const MotorSensorCalibData& sensors, const MotorCalibData& joint_reference);
    bool current_sample(float u_adc_voltage, float v_adc_voltage, float w_adc_voltage,
                        float bus_adc_voltage, float rotor_tmr1, float rotor_tmr2);
    // Electrical sampling remains available before rotor calibration is valid.
    bool electrical_sample(float u_adc_voltage, float v_adc_voltage, float w_adc_voltage,
                           float bus_adc_voltage);
    bool rotor_sample(float rotor_tmr1, float rotor_tmr2);
    bool joint_sample(float output_tmr1, float output_tmr2);
    const MotorElecInfoFeedback& electrical() const { return electrical_; }
    const MotorElecAngleFeedback& rotor() const { return rotor_; }
    const MotorVelocityFeedback& velocity() const { return velocity_; }
    const MotorJointAngleFeedback& joint() const { return joint_; }
    JointLoopFeedback loop_feedback() const {
        return {joint_.multi_turn_angle_filted, velocity_.output_angular_velocity,
                velocity_.angular_velocity};
    }
    // Snapshot before existing zero/offset references, after output LUT.
    bool recover_turn(float known_position, float tolerance);
    void align_unloaded(float elastic_zero);
    bool joint_reference_candidate(bool zero_reference, MotorCalibData& candidate) const;

private:
    MotorBasicConfig motor_ {};
    float joint_frequency_hz_ {0.0F};
    float invert_sign_ {1.0F};
    bool channels_swapped_ {false};
    MotorSensorCalibData sensors_ {};
    MotorCalibData reference_ {};
    MotorElecInfoFeedback electrical_ {};
    MotorElecAngleFeedback rotor_ {};
    MotorVelocityFeedback velocity_ {};
    MotorJointAngleFeedback joint_ {};
    bool configured_ {false};
};

}  // namespace algo::motor
