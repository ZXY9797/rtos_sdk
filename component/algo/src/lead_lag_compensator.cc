#include <algo/lead_lag_compensator.h>

#include <algo/spatial_math.h>

#include <cmath>

namespace algo {
namespace {

constexpr float kNyquistMargin = 0.45F;

} // namespace

bool valid_lead_lag_config(const LeadLagConfig &config)
{
    return std::isfinite(config.sample_frequency_hz)
        && config.sample_frequency_hz > 0.0F
        && std::isfinite(config.zero_frequency_hz)
        && config.zero_frequency_hz > 0.0F
        && std::isfinite(config.pole_frequency_hz)
        && config.pole_frequency_hz > config.zero_frequency_hz
        && config.pole_frequency_hz
            < kNyquistMargin * config.sample_frequency_hz
        && std::isfinite(config.gain) && config.gain > 0.0F;
}

bool LeadLagCompensator::configure(const LeadLagConfig &config)
{
    if (!valid_lead_lag_config(config)) {
        bypass();
        is_configured_ = false;
        return false;
    }
    const float sample_rate = 2.0F * config.sample_frequency_hz;
    const float zero_rate = 2.0F * spatial::kPi
        * config.zero_frequency_hz;
    const float pole_rate = 2.0F * spatial::kPi
        * config.pole_frequency_hz;
    const float denominator = sample_rate + pole_rate;
    if (!std::isfinite(denominator) || denominator <= 0.0F) {
        bypass();
        is_configured_ = false;
        return false;
    }
    const float inverse_denominator = 1.0F / denominator;
    b0_ = config.gain * (sample_rate + zero_rate)
        * inverse_denominator;
    b1_ = config.gain * (zero_rate - sample_rate)
        * inverse_denominator;
    a1_ = (pole_rate - sample_rate) * inverse_denominator;
    if (!std::isfinite(b0_) || !std::isfinite(b1_)
        || !std::isfinite(a1_)) {
        bypass();
        is_configured_ = false;
        return false;
    }
    reset();
    is_configured_ = true;
    return true;
}

void LeadLagCompensator::bypass()
{
    b0_ = 1.0F;
    b1_ = 0.0F;
    a1_ = 0.0F;
    reset();
    is_configured_ = true;
}

bool LeadLagCompensator::update(float input, float &output)
{
    output = 0.0F;
    if (!is_configured_ || !std::isfinite(input)) {
        reset();
        return false;
    }
    const float candidate = b0_ * input + b1_ * previous_input_
        - a1_ * previous_output_;
    if (!std::isfinite(candidate)) {
        reset();
        return false;
    }
    previous_input_ = input;
    previous_output_ = candidate;
    output = candidate;
    return true;
}

void LeadLagCompensator::reset()
{
    previous_input_ = 0.0F;
    previous_output_ = 0.0F;
}

} // namespace algo
