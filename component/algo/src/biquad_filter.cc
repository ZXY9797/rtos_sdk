#include <algo/biquad_filter.h>

#include <algo/spatial_math.h>

#include <cmath>

namespace algo {
namespace {

constexpr float kMinimumQualityFactor = 0.1F;
constexpr float kNyquistMargin = 0.45F;

} // namespace

bool valid_biquad_config(const BiquadConfig &config)
{
    return std::isfinite(config.sample_frequency_hz)
        && config.sample_frequency_hz > 0.0F
        && std::isfinite(config.center_frequency_hz)
        && config.center_frequency_hz > 0.0F
        && config.center_frequency_hz
            < kNyquistMargin * config.sample_frequency_hz
        && std::isfinite(config.quality_factor)
        && config.quality_factor >= kMinimumQualityFactor;
}

bool BiquadFilter::configure(const BiquadConfig &config)
{
    if (!valid_biquad_config(config)) {
        set_identity_coefficients();
        reset();
        is_configured_ = false;
        return false;
    }
    const float omega = 2.0F * spatial::kPi
        * config.center_frequency_hz / config.sample_frequency_hz;
    const float cosine = std::cos(omega);
    const float alpha = std::sin(omega)
        / (2.0F * config.quality_factor);
    const float inverse_a0 = 1.0F / (1.0F + alpha);
    switch (config.type) {
    case BiquadType::LowPass:
        b0_ = 0.5F * (1.0F - cosine) * inverse_a0;
        b1_ = (1.0F - cosine) * inverse_a0;
        b2_ = b0_;
        break;
    case BiquadType::HighPass:
        b0_ = 0.5F * (1.0F + cosine) * inverse_a0;
        b1_ = -(1.0F + cosine) * inverse_a0;
        b2_ = b0_;
        break;
    case BiquadType::BandPass:
        b0_ = alpha * inverse_a0;
        b1_ = 0.0F;
        b2_ = -b0_;
        break;
    case BiquadType::Notch:
        b0_ = inverse_a0;
        b1_ = -2.0F * cosine * inverse_a0;
        b2_ = inverse_a0;
        break;
    default:
        set_identity_coefficients();
        reset();
        is_configured_ = false;
        return false;
    }
    a1_ = -2.0F * cosine * inverse_a0;
    a2_ = (1.0F - alpha) * inverse_a0;
    reset();
    is_configured_ = true;
    return true;
}

void BiquadFilter::bypass()
{
    set_identity_coefficients();
    reset();
    is_configured_ = true;
}

bool BiquadFilter::update(float input, float &output)
{
    output = 0.0F;
    if (!is_configured_ || !std::isfinite(input)) {
        reset();
        return false;
    }
    const float candidate = b0_ * input + state1_;
    const float next_state1 = b1_ * input - a1_ * candidate + state2_;
    const float next_state2 = b2_ * input - a2_ * candidate;
    if (!std::isfinite(candidate) || !std::isfinite(next_state1)
        || !std::isfinite(next_state2)) {
        reset();
        return false;
    }
    state1_ = next_state1;
    state2_ = next_state2;
    output = candidate;
    return true;
}

void BiquadFilter::reset()
{
    state1_ = 0.0F;
    state2_ = 0.0F;
}

void BiquadFilter::set_identity_coefficients()
{
    b0_ = 1.0F;
    b1_ = 0.0F;
    b2_ = 0.0F;
    a1_ = 0.0F;
    a2_ = 0.0F;
}

} // namespace algo
