#include <algo/controller/leso.h>

#include <algo/math/basic_math.h>

namespace algo::motor::controller {

bool Leso::configure(const LesoConfig& config)
{
    if ((config.omega <= 0.0F) ||
        (config.sample_time_s <= 0.0F) ||
        (config.b0 <= 0.0F) ||
        (config.output_min > config.output_max) ||
        !basic_math::is_finite_float(config.omega) ||
        !basic_math::is_finite_float(config.sample_time_s) ||
        !basic_math::is_finite_float(config.b0) ||
        !basic_math::is_finite_float(config.kp) ||
        !basic_math::is_finite_float(config.kd) ||
        !basic_math::is_finite_float(config.output_min) ||
        !basic_math::is_finite_float(config.output_max)) {
        configured_ = false;
        return false;
    }

    type_ = config.type;
    omega_ = config.omega;
    b0_ = config.b0;
    ts_ = config.sample_time_s;
    kp_ = config.kp * omega_;
    kd_ = config.kd * omega_;
    output_min_ = config.output_min;
    output_max_ = config.output_max;

    switch (type_) {
    case LesoType::FirstOrder:
        beta1_ = 2.0F * omega_;
        beta2_ = omega_ * omega_;
        beta3_ = 0.0F;
        break;
    case LesoType::SecondOrder:
        beta1_ = 3.0F * omega_;
        beta2_ = 3.0F * omega_ * omega_;
        beta3_ = omega_ * omega_ * omega_;
        break;
    default:
        configured_ = false;
        return false;
    }

    configured_ = true;
    reset();
    return true;
}

void Leso::reset()
{
    z1_ = 0.0F;
    z2_ = 0.0F;
    z3_ = 0.0F;
    last_output_ = 0.0F;
    output_ = 0.0F;
}

// Seed an equilibrium at the measured velocity with the already applied current.
void Leso::seed(float feedback, float output)
{
    if (!configured_ || !basic_math::is_finite_float(feedback) || !basic_math::is_finite_float(output)) return;
    z1_ = feedback;
    output_ = basic_math::clamp_float(output, output_min_, output_max_);
    last_output_ = output_;
    z2_ = type_ == LesoType::FirstOrder ? -b0_ * output_ : 0.0F;
    z3_ = type_ == LesoType::SecondOrder ? -b0_ * output_ : 0.0F;
}

bool Leso::track_output(float applied_output)
{
    if (!configured_ || !basic_math::is_finite_float(applied_output)) return false;
    const float applied = basic_math::clamp_float(applied_output, output_min_, output_max_);
    float& disturbance = type_ == LesoType::FirstOrder ? z2_ : z3_;
    const float corrected = disturbance + b0_ * (output_ - applied);
    if (!basic_math::is_finite_float(corrected)) return false;
    disturbance = corrected;
    output_ = last_output_ = applied;
    return true;
}

float Leso::update(float setpoint, float feedback)
{
    if (!configured_) {
        return 0.0F;
    }

    switch (type_) {
    case LesoType::FirstOrder:
        return update_first_order(setpoint, feedback);
    case LesoType::SecondOrder:
        return update_second_order(setpoint, feedback);
    default:
        return 0.0F;
    }
}

float Leso::update_first_order(float setpoint, float feedback)
{
    const float error = feedback - z1_;

    z1_ += ts_ * (z2_ + (beta1_ * error) + (b0_ * last_output_));
    z2_ += ts_ * (beta2_ * error);

    const float track = kp_ * (setpoint - z1_);
    const float unlimited = (track - z2_) / b0_;
    output_ = basic_math::clamp_float(unlimited, output_min_, output_max_);
    if (output_ != unlimited) {
        z2_ += b0_ * (unlimited - output_);
    }

    last_output_ = output_;
    return output_;
}

float Leso::update_second_order(float setpoint, float feedback)
{
    const float error = feedback - z1_;

    z1_ += ts_ * (z2_ + (beta1_ * error));
    z2_ += ts_ * (z3_ + (b0_ * last_output_) + (beta2_ * error));
    z3_ += ts_ * (beta3_ * error);

    const float tracking =
        (kp_ * (setpoint - z1_)) + (kd_ * (0.0F - z2_));
    const float unlimited = (tracking - z3_) / b0_;
    output_ = basic_math::clamp_float(unlimited, output_min_, output_max_);
    if (output_ != unlimited) {
        z3_ += b0_ * (unlimited - output_);
    }

    last_output_ = output_;
    return output_;
}

}  // namespace algo::motor::controller
