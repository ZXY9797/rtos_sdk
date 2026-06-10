#include "algo/lead_lag_compensator.h"
#include <cmath>

namespace algo {

void LeadLagCompensator::init(const LeadLagConfig &cfg) {
    cfg_ = cfg;
    calculate_coefficients();
    reset();
}

void LeadLagCompensator::set_params(float fz, float fp) {
    cfg_.fz = fz;
    cfg_.fp = fp;
    calculate_coefficients();
}

float LeadLagCompensator::update(float x) {
    // Direct Form II: y = b0*x + b1*x1 - a1*y1
    float y = cfg_.gain * (b0_ * x + b1_ * x1_) - a1_ * y1_;
    x1_ = x;
    y1_ = y;
    return y;
}

void LeadLagCompensator::reset() {
    x1_ = 0.0f;
    y1_ = 0.0f;
}

void LeadLagCompensator::calculate_coefficients() {
    // H(s) = (s + wz) / (s + wp)
    // Bilinear transform: s = 2*fs * (z-1)/(z+1)
    float wz = 2.0f * 3.14159265f * cfg_.fz;
    float wp = 2.0f * 3.14159265f * cfg_.fp;
    float two_fs = 2.0f * cfg_.fs;

    // Discrete coefficients via bilinear transform
    // num: (2*fs + wz) + (wz - 2*fs) * z^-1
    // den: (2*fs + wp) + (wp - 2*fs) * z^-1
    float den = two_fs + wp;
    float den_inv = 1.0f / den;

    b0_ = (two_fs + wz) * den_inv;
    b1_ = (wz - two_fs) * den_inv;
    a1_ = (wp - two_fs) * den_inv;
}

} // namespace algo
