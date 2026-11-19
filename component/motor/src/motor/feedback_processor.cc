#include <algo/motor/feedback_processor.h>
#include <cmath>
#include <limits>

namespace algo::motor {

bool AdcScale::configure(const AdcFormat& format)
{
    full_scale_ = 0U;
    scale_ = 0.0F;
    if ((format.resolution_bits != 6U && format.resolution_bits != 8U &&
         format.resolution_bits != 10U && format.resolution_bits != 12U) ||
        format.oversample_ratio == 0U || format.oversample_shift >= 32U ||
        format.reference_mv == 0U) return false;
    uint64_t scale = (uint64_t(1U) << format.resolution_bits) - 1U;
    if (format.oversample_ratio > 1U) {
        scale *= format.oversample_ratio;
        scale >>= format.oversample_shift;
    }
    if (!scale || scale > std::numeric_limits<uint32_t>::max()) return false;
    full_scale_ = static_cast<uint32_t>(scale);
    scale_ = (float(format.reference_mv) * 0.001F) / float(full_scale_);
    return true;
}

bool FeedbackProcessor::configure(const MotorParameters& parameters,
                                  const MotorElecCalibData& electrical,
                                  const MotorSensorCalibData& sensors,
                                  const MotorCalibData& joint_reference)
{
    configured_ = false;
    if (!parameters.motor.valid || parameters.motor.pole_pairs == 0U ||
        !std::isfinite(parameters.motor.gear_ratio) || parameters.motor.gear_ratio <= 0.0F ||
        !std::isfinite(parameters.joint.frequency_hz) || parameters.joint.frequency_hz <= 0.0F ||
        !std::isfinite(parameters.joint.invert_sign) || parameters.joint.invert_sign == 0.0F)
        return false;
    motor_ = parameters.motor;
    joint_frequency_hz_ = parameters.joint.frequency_hz;
    invert_sign_ = parameters.joint.invert_sign;
    channels_swapped_ = parameters.input_channels_swapped;
    sensors_ = sensors;
    reference_ = joint_reference;
    if (!current_ctrl_feedback_init(parameters.raw_conversion, electrical, &electrical_,
                                    &rotor_, parameters.current_filter_alpha) ||
        !joint_ctrl_feedback_init(parameters.motor, &velocity_, &joint_)) return false;
    configured_ = true;
    return true;
}

bool FeedbackProcessor::current_sample(float u, float v, float w, float bus,
                                       float rotor_tmr1, float rotor_tmr2)
{
    return electrical_sample(u, v, w, bus) && rotor_sample(rotor_tmr1, rotor_tmr2);
}

bool FeedbackProcessor::electrical_sample(float u, float v, float w, float bus)
{
    if (!configured_ || !std::isfinite(u) || !std::isfinite(v) || !std::isfinite(w) ||
        !std::isfinite(bus))
        return false;
    return update_elec_info(u, v, w, bus, &electrical_);
}

bool FeedbackProcessor::rotor_sample(float rotor_tmr1, float rotor_tmr2)
{
    if (!configured_ || !std::isfinite(rotor_tmr1) || !std::isfinite(rotor_tmr2))
        return false;
    if (!update_sensor_angle(rotor_tmr1, rotor_tmr2, sensors_,
                             {channels_swapped_}, &rotor_)) return false;
    return elec_angle_calibrate(sensors_, motor_, &rotor_);
}

bool FeedbackProcessor::joint_sample(float tmr1, float tmr2)
{
    if (!configured_ || (sensors_.output_angle_calib && sensors_.output_angle_calib->updating))
        return false;
    if (!update_joint_angle(tmr1, tmr2, joint_frequency_hz_, sensors_, reference_, &joint_) ||
        !update_velocity(rotor_.mech_angle_calib, joint_frequency_hz_, motor_.gear_ratio,
                         invert_sign_, joint_.filter_alpha, &velocity_)) return false;
    // The first unwrapping sample can return before the primitive's final
    // finite check. Do not publish a non-finite persisted reference as ready.
    return std::isfinite(joint_.multi_turn_angle_filted) &&
           std::isfinite(velocity_.output_angular_velocity) &&
           std::isfinite(velocity_.angular_velocity);
}

bool FeedbackProcessor::recover_turn(float known_position, float tolerance)
{
    if (!joint_.internal.has_last_single_turn_angle || !std::isfinite(known_position) ||
        !std::isfinite(tolerance) || tolerance <= 0 || tolerance >= 3.14159F) return false;
    constexpr float tau = 6.283185307F;
    const float turns = std::round((known_position - joint_.multi_turn_angle_filted) / tau);
    if (!std::isfinite(turns) || std::fabs(turns) > 10000) return false;
    const float shift = turns * tau;
    if (std::fabs(joint_.multi_turn_angle_filted + shift - known_position) > tolerance) return false;
    velocity_.output_equivalent_multi_turn_angle_rad += shift;
    velocity_.output_equivalent_multi_turn_angle_filted_rad += shift;
    joint_.turn_count += static_cast<int32_t>(turns);
    joint_.internal.multi_turn_angle_raw += shift;
    joint_.multi_turn_angle += shift;
    joint_.multi_turn_angle_filted += shift;
    return true;
}

void FeedbackProcessor::align_unloaded(float elastic_zero)
{
    const float shift = joint_.multi_turn_angle_filted + elastic_zero -
                        velocity_.output_equivalent_multi_turn_angle_filted_rad;
    velocity_.output_equivalent_multi_turn_angle_rad += shift;
    velocity_.output_equivalent_multi_turn_angle_filted_rad += shift;
}

bool FeedbackProcessor::joint_reference_candidate(bool zero_reference,
                                                  MotorCalibData& candidate) const
{
    if (!configured_ || !joint_.internal.has_last_single_turn_angle) return false;
    candidate = reference_;
    const float offset = zero_reference ? joint_.single_turn_angle_calib :
                                         joint_.internal.multi_turn_angle_raw;
    if (!std::isfinite(offset)) return false;
    if (zero_reference) candidate.joint_zero_angle = {true, offset};
    else candidate.joint_offset_angle = {true, offset};
    return true;
}

}  // namespace algo::motor
