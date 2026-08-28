#include <gimbal/controller.h>

#include <gimbal/math.h>

#include <algorithm>
#include <cmath>

namespace gimbal {
namespace {

constexpr float kMinimumQualityFactor = 0.1F;
constexpr float kNyquistMargin = 0.45F;
constexpr float kLowPassQualityFactor = 0.70710678F;
constexpr float kSamplePeriodTolerance = 0.25F;
constexpr float kMinimumMappingDeterminant = 1.0e-6F;

struct FilterCoefficients {
    float b0 {1.0F};
    float b1 {0.0F};
    float b2 {0.0F};
    float a1 {0.0F};
    float a2 {0.0F};
};

[[nodiscard]] float component(const Vector3 &value, size_t axis)
{
    const float values[kAxisCount] {value.x, value.y, value.z};
    return axis < kAxisCount ? values[axis] : 0.0F;
}

void set_component(Vector3 &value, size_t axis, float component_value)
{
    float *values[kAxisCount] {&value.x, &value.y, &value.z};
    if (axis < kAxisCount) {
        *values[axis] = component_value;
    }
}

[[nodiscard]] bool finite_vector(const Vector3 &value)
{
    return std::isfinite(value.x) && std::isfinite(value.y)
        && std::isfinite(value.z);
}

[[nodiscard]] bool finite_quaternion(const Quaternion &value)
{
    return std::isfinite(value.w) && std::isfinite(value.x)
        && std::isfinite(value.y) && std::isfinite(value.z);
}

[[nodiscard]] bool finite_array(
    const std::array<float, kAxisCount> &values)
{
    return std::all_of(values.begin(), values.end(),
                       [](float value) { return std::isfinite(value); });
}

[[nodiscard]] bool finite_matrix(const Matrix3 &value)
{
    for (const auto &row : value.value) {
        for (float element : row) {
            if (!std::isfinite(element)) return false;
        }
    }
    return true;
}

[[nodiscard]] bool valid_frequency(float frequency, float sample_frequency)
{
    return std::isfinite(frequency) && frequency > 0.0F
        && frequency < kNyquistMargin * sample_frequency;
}

[[nodiscard]] bool finite_axis_config(const AxisControllerConfig &config,
                                      float sample_frequency)
{
    const bool notch_valid = config.notch_frequency_hz == 0.0F
        || (valid_frequency(config.notch_frequency_hz, sample_frequency)
            && std::isfinite(config.notch_quality_factor)
            && config.notch_quality_factor >= kMinimumQualityFactor);
    const bool lead_bypassed = config.lead_zero_frequency_hz == 0.0F
        && config.lead_pole_frequency_hz == 0.0F;
    const bool lead_valid = lead_bypassed
        || (valid_frequency(config.lead_zero_frequency_hz, sample_frequency)
            && valid_frequency(config.lead_pole_frequency_hz,
                               sample_frequency)
            && config.lead_zero_frequency_hz
                < config.lead_pole_frequency_hz);
    return std::isfinite(config.attitude_gain)
        && std::isfinite(config.rate_proportional_gain)
        && std::isfinite(config.rate_integral_gain)
        && std::isfinite(config.integral_limit)
        && std::isfinite(config.command_limit)
        && config.attitude_gain >= 0.0F
        && config.rate_proportional_gain >= 0.0F
        && config.rate_integral_gain >= 0.0F
        && config.integral_limit >= 0.0F
        && config.command_limit > 0.0F
        && notch_valid && lead_valid
        && valid_frequency(config.low_pass_frequency_hz,
                           sample_frequency);
}

[[nodiscard]] FilterCoefficients notch_coefficients(
    float frequency, float quality_factor, float sample_frequency)
{
    if (frequency == 0.0F) {
        return {};
    }
    const float omega = 2.0F * math::kPi * frequency / sample_frequency;
    const float cosine = std::cos(omega);
    const float alpha = std::sin(omega) / (2.0F * quality_factor);
    const float inverse_a0 = 1.0F / (1.0F + alpha);
    return {
        inverse_a0, -2.0F * cosine * inverse_a0, inverse_a0,
        -2.0F * cosine * inverse_a0,
        (1.0F - alpha) * inverse_a0,
    };
}

[[nodiscard]] FilterCoefficients low_pass_coefficients(
    float frequency, float sample_frequency)
{
    const float omega = 2.0F * math::kPi * frequency / sample_frequency;
    const float cosine = std::cos(omega);
    const float alpha =
        std::sin(omega) / (2.0F * kLowPassQualityFactor);
    const float inverse_a0 = 1.0F / (1.0F + alpha);
    const float numerator = 0.5F * (1.0F - cosine) * inverse_a0;
    return {
        numerator, 2.0F * numerator, numerator,
        -2.0F * cosine * inverse_a0,
        (1.0F - alpha) * inverse_a0,
    };
}

[[nodiscard]] FilterCoefficients lead_coefficients(
    float zero_frequency, float pole_frequency, float sample_frequency)
{
    if (zero_frequency == 0.0F && pole_frequency == 0.0F) {
        return {};
    }
    const float rate = 2.0F * sample_frequency;
    const float zero_rate = 2.0F * math::kPi * zero_frequency;
    const float pole_rate = 2.0F * math::kPi * pole_frequency;
    const float inverse_denominator = 1.0F / (1.0F + rate / pole_rate);
    return {
        (1.0F + rate / zero_rate) * inverse_denominator,
        (1.0F - rate / zero_rate) * inverse_denominator,
        0.0F,
        (1.0F - rate / pole_rate) * inverse_denominator,
        0.0F,
    };
}

} // namespace

bool valid_controller_config(const ControllerConfig &config)
{
    if (!std::isfinite(config.sample_frequency_hz)
        || config.sample_frequency_hz <= 0.0F) {
        return false;
    }
    for (const AxisControllerConfig &axis : config.axis) {
        if (!finite_axis_config(axis, config.sample_frequency_hz)) {
            return false;
        }
    }
    for (const auto &row : config.decoupling_matrix.value) {
        for (float value : row) {
            if (!std::isfinite(value)) {
                return false;
            }
        }
    }
    return std::abs(math::determinant(config.decoupling_matrix)) > 1.0e-4F;
}

float TwoDegreeController::DigitalFilter::update(float input)
{
    const float output = b0 * input + state1;
    state1 = b1 * input - a1 * output + state2;
    state2 = b2 * input - a2 * output;
    return output;
}

void TwoDegreeController::DigitalFilter::reset()
{
    state1 = 0.0F;
    state2 = 0.0F;
}

bool TwoDegreeController::configure_filters()
{
    for (size_t axis = 0U; axis < kAxisCount; ++axis) {
        const AxisControllerConfig &axis_config = config_.axis[axis];
        const FilterCoefficients notch = notch_coefficients(
            axis_config.notch_frequency_hz,
            axis_config.notch_quality_factor,
            config_.sample_frequency_hz);
        const FilterCoefficients lead = lead_coefficients(
            axis_config.lead_zero_frequency_hz,
            axis_config.lead_pole_frequency_hz,
            config_.sample_frequency_hz);
        const FilterCoefficients low_pass = low_pass_coefficients(
            axis_config.low_pass_frequency_hz,
            config_.sample_frequency_hz);
        filters_[axis].notch = {notch.b0, notch.b1, notch.b2,
                                notch.a1, notch.a2};
        filters_[axis].lead = {lead.b0, lead.b1, lead.b2,
                               lead.a1, lead.a2};
        filters_[axis].low_pass = {
            low_pass.b0, low_pass.b1, low_pass.b2,
            low_pass.a1, low_pass.a2,
        };
    }
    return true;
}

bool TwoDegreeController::configure(const ControllerConfig &config,
                                    MotorControlMode mode)
{
    if (!valid_controller_config(config)
        || static_cast<uint8_t>(mode)
            > static_cast<uint8_t>(MotorControlMode::CurrentFoc)) {
        is_configured_ = false;
        return false;
    }
    config_ = config;
    mode_ = mode;
    if (!configure_filters()) {
        is_configured_ = false;
        return false;
    }
    sequence_ = 0U;
    reset();
    is_configured_ = true;
    return true;
}

void TwoDegreeController::reset()
{
    integral_ = {};
    for (AxisFilters &axis : filters_) {
        axis.notch.reset();
        axis.lead.reset();
        axis.low_pass.reset();
    }
}

MotorCommand TwoDegreeController::idle(uint32_t publish_time_us)
{
    reset();
    MotorCommand output {};
    output.header.sequence = ++sequence_;
    output.header.capture_time_us = publish_time_us;
    output.header.publish_time_us = publish_time_us;
    output.header.health = is_configured_ ? Health::Valid : Health::Fault;
    return output;
}

bool TwoDegreeController::calculate_feedback(
    const ControllerInput &input, Vector3 &feedback)
{
    const Quaternion attitude_error = math::multiply(
        math::conjugate(input.attitude.camera_in_world),
        input.reference.attitude);
    const Vector3 angle_error = math::rotation_vector(attitude_error);
    const Vector3 body_rate = math::rotate(
        math::conjugate(input.attitude.camera_in_world),
        input.attitude.angular_rate_world_rad_s);
    for (size_t axis = 0U; axis < kAxisCount; ++axis) {
        const AxisControllerConfig &axis_config = config_.axis[axis];
        const float rate_target =
            component(input.reference.angular_rate_rad_s, axis)
            + axis_config.attitude_gain * component(angle_error, axis);
        const float rate_error = rate_target - component(body_rate, axis);
        float shaped_error = filters_[axis].notch.update(rate_error);
        shaped_error = filters_[axis].lead.update(shaped_error);
        shaped_error = filters_[axis].low_pass.update(shaped_error);
        const float integral_candidate = component(integral_, axis)
            + axis_config.rate_integral_gain
                * rate_error * input.sample_period_s;
        const float integral = math::clamp(
            integral_candidate,
            -axis_config.integral_limit, axis_config.integral_limit);
        const float feedback_value =
            axis_config.rate_proportional_gain * shaped_error + integral;
        if (!std::isfinite(rate_target) || !std::isfinite(rate_error)
            || !std::isfinite(shaped_error)
            || !std::isfinite(integral_candidate)
            || !std::isfinite(feedback_value)) {
            reset();
            return false;
        }
        set_component(integral_, axis, integral);
        set_component(feedback, axis, feedback_value);
    }
    return true;
}

MotorCommand TwoDegreeController::update(const ControllerInput &input)
{
    MotorCommand output {};
    output.header.sequence = ++sequence_;
    output.header.capture_time_us = input.publish_time_us;
    output.header.publish_time_us = input.publish_time_us;
    output.header.health = Health::Fault;
    const float expected_period = 1.0F / config_.sample_frequency_hz;
    const bool inputs_valid = finite_quaternion(input.reference.attitude)
        && finite_vector(input.reference.angular_rate_rad_s)
        && finite_quaternion(input.attitude.camera_in_world)
        && finite_vector(input.attitude.angular_rate_world_rad_s)
        && finite_array(input.feedforward.current_a)
        && finite_array(input.feedforward.voltage_v)
        && input.feedforward.health == Health::Valid
        && finite_matrix(input.camera_effort_to_joint)
        && std::abs(math::determinant(input.camera_effort_to_joint))
            > kMinimumMappingDeterminant;
    if (!is_configured_
        || input.reference.header.health != Health::Valid
        || input.attitude.header.health != Health::Valid || !inputs_valid
        || !std::isfinite(input.sample_period_s)
        || input.sample_period_s <= 0.0F
        || std::abs(input.sample_period_s - expected_period)
            > kSamplePeriodTolerance * expected_period) {
        return output;
    }
    Vector3 feedback {};
    if (!calculate_feedback(input, feedback)) {
        return output;
    }
    const Vector3 camera_effort = math::multiply(
        config_.decoupling_matrix, feedback);
    const Vector3 joint_effort = math::multiply(
        input.camera_effort_to_joint, camera_effort);
    if (!finite_vector(camera_effort) || !finite_vector(joint_effort)) {
        reset();
        return output;
    }
    for (size_t axis = 0U; axis < kAxisCount; ++axis) {
        const float forward = mode_ == MotorControlMode::CurrentFoc
            ? input.feedforward.current_a[axis]
            : input.feedforward.voltage_v[axis];
        const float unrestricted = component(joint_effort, axis) + forward;
        if (!std::isfinite(unrestricted)) {
            reset();
            return output;
        }
        output.q_axis_command[axis] = math::clamp(
            unrestricted,
            -config_.axis[axis].command_limit,
            config_.axis[axis].command_limit);
    }
    output.enable = true;
    output.header.health = Health::Valid;
    output.header.valid_flags = kAllAxesMask;
    return output;
}

} // namespace gimbal
