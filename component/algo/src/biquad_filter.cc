#include "algo/biquad_filter.h"
#include <cmath>

namespace algo {

void BiquadFilter::init(const BiquadConfig &cfg) {
    cfg_ = cfg;
    calculate_coefficients();
    reset();
}

void BiquadFilter::set_params(float f0, float Q) {
    cfg_.f0 = f0;
    cfg_.Q = Q;
    calculate_coefficients();
}

float BiquadFilter::update(float x) {
    float y = b0_ * x + b1_ * x1_ + b2_ * x2_ - a1_ * y1_ - a2_ * y2_;
    x2_ = x1_;
    x1_ = x;
    y2_ = y1_;
    y1_ = y;
    return y;
}

void BiquadFilter::reset() {
    x1_ = x2_ = y1_ = y2_ = 0.0f;
}

void BiquadFilter::calculate_coefficients() {
    float w0 = 2.0f * 3.14159265f * cfg_.f0 / cfg_.fs;
    float cos_w0 = cosf(w0);
    float sin_w0 = sinf(w0);
    float alpha = sin_w0 / (2.0f * cfg_.Q);

    float a0_inv = 1.0f / (1.0f + alpha);

    switch (cfg_.type) {
    case BiquadType::LowPass:
        b0_ = (1.0f - cos_w0) * 0.5f * a0_inv;
        b1_ = (1.0f - cos_w0) * a0_inv;
        b2_ = b0_;
        a1_ = -2.0f * cos_w0 * a0_inv;
        a2_ = (1.0f - alpha) * a0_inv;
        break;

    case BiquadType::HighPass:
        b0_ = (1.0f + cos_w0) * 0.5f * a0_inv;
        b1_ = -(1.0f + cos_w0) * a0_inv;
        b2_ = b0_;
        a1_ = -2.0f * cos_w0 * a0_inv;
        a2_ = (1.0f - alpha) * a0_inv;
        break;

    case BiquadType::BandPass:
        b0_ = alpha * a0_inv;
        b1_ = 0.0f;
        b2_ = -alpha * a0_inv;
        a1_ = -2.0f * cos_w0 * a0_inv;
        a2_ = (1.0f - alpha) * a0_inv;
        break;

    case BiquadType::Notch:
        b0_ = a0_inv;
        b1_ = -2.0f * cos_w0 * a0_inv;
        b2_ = a0_inv;
        a1_ = -2.0f * cos_w0 * a0_inv;
        a2_ = (1.0f - alpha) * a0_inv;
        break;
    }
}

} // namespace algo
