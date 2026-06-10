#include <algo/leso.h>
#include <cmath>

namespace algo {

Leso::Leso(const LesoConfig &cfg) : cfg_(cfg) {
    calculate_gains();
}

void Leso::init(const LesoConfig &cfg) {
    cfg_ = cfg;
    calculate_gains();
    reset();
}

void Leso::calculate_gains() {
    kp_ = cfg_.kp * cfg_.omega;
    kd_ = cfg_.kd * cfg_.omega;

    if (cfg_.type == LesoType::FirstOrder) {
        beta1_ = 2.0f * cfg_.omega;
        beta2_ = cfg_.omega * cfg_.omega;
    } else {
        beta1_ = 3.0f * cfg_.omega;
        beta2_ = 3.0f * cfg_.omega * cfg_.omega;
        beta3_ = cfg_.omega * cfg_.omega * cfg_.omega;
    }
}

float Leso::update(float setpoint, float feedback) {
    if (cfg_.type == LesoType::FirstOrder) {
        return update_first_order(setpoint, feedback);
    }
    return update_second_order(setpoint, feedback);
}

float Leso::update_first_order(float set, float fb) {
    float e = fb - z1_;

    z1_ += cfg_.ts * (z2_ + beta1_ * e + cfg_.b0 * u_prev_);
    z2_ += cfg_.ts * (beta2_ * e);

    float track_term = kp_ * (set - z1_);
    float u_unlimited = (track_term - z2_) / cfg_.b0;

    float u = u_unlimited;
    if (u > cfg_.output_max) {
        u = cfg_.output_max;
        z2_ += cfg_.b0 * (u_unlimited - u);
    } else if (u < cfg_.output_min) {
        u = cfg_.output_min;
        z2_ += cfg_.b0 * (u_unlimited - u);
    }

    u_prev_ = u;
    return u;
}

float Leso::update_second_order(float set, float fb) {
    float e = fb - z1_;

    z1_ += cfg_.ts * (z2_ + beta1_ * e);
    z2_ += cfg_.ts * (z3_ + cfg_.b0 * u_prev_ + beta2_ * e);
    z3_ += cfg_.ts * (beta3_ * e);

    float u0 = kp_ * (set - z1_) + kd_ * (0.0f - z2_);
    float u_unlimited = (u0 - z3_) / cfg_.b0;

    float u = u_unlimited;
    if (u > cfg_.output_max) {
        u = cfg_.output_max;
        z3_ += cfg_.b0 * (u_unlimited - u);
    } else if (u < cfg_.output_min) {
        u = cfg_.output_min;
        z3_ += cfg_.b0 * (u_unlimited - u);
    }

    u_prev_ = u;
    return u;
}

void Leso::reset() {
    z1_ = 0.0f;
    z2_ = 0.0f;
    z3_ = 0.0f;
    u_prev_ = 0.0f;
}

void Leso::set_output_limit(float min, float max) {
    cfg_.output_min = min;
    cfg_.output_max = max;
}

} // namespace algo
