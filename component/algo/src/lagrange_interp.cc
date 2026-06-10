#include "algo/lagrange_interp.h"
#include <cmath>
#include <cstring>

void LagrangeInterp::init(const LagrangeConfig &cfg, float *y_offsets) {
    cfg_ = cfg;
    y_ = y_offsets;
    dx_ = (cfg.x_max - cfg.x_min) / static_cast<float>(cfg.point_count);
}

bool LagrangeInterp::fit(float x, float *result) const {
    if (!y_ || !result || cfg_.point_count < 2) return false;
    if (x < cfg_.x_min || x > cfg_.x_max) return false;

    int index = static_cast<int>((x - cfg_.x_min) / dx_);
    if (index < 0) index = 0;
    if (index >= static_cast<int>(cfg_.point_count)) index = static_cast<int>(cfg_.point_count) - 1;

    if (cfg_.order == LagrangeOrder::First) {
        if (index >= static_cast<int>(cfg_.point_count) - 1)
            index = static_cast<int>(cfg_.point_count) - 2;
        return fit_order1(x, static_cast<uint32_t>(index), result);
    } else {
        if (index == 0) {
            index = static_cast<int>(cfg_.point_count) - 1;
        } else {
            index = index - 1;
        }
        return fit_order2(x, static_cast<uint32_t>(index), result);
    }
}

bool LagrangeInterp::fit_order1(float x, uint32_t idx, float *result) const {
    float x0 = cfg_.x_min + idx * dx_;
    float x1 = cfg_.x_min + (idx + 1) * dx_;
    float y0 = y_[idx];
    float y1 = y_[idx + 1];

    float denom = x1 - x0;
    if (std::fabs(denom) < 1e-9f) {
        *result = y0;
        return true;
    }

    float w0 = (x - x1) / denom;
    float w1 = (x - x0) / denom;
    *result = y0 * w0 + y1 * w1;
    return true;
}

bool LagrangeInterp::fit_order2(float x, uint32_t idx, float *result) const {
    uint32_t n = cfg_.point_count;
    float x0 = cfg_.x_min + idx * dx_;
    float x1 = cfg_.x_min + ((idx + 1) % n) * dx_;
    float x2 = cfg_.x_min + ((idx + 2) % n) * dx_;
    float y0 = y_[idx];
    float y1 = y_[(idx + 1) % n];
    float y2 = y_[(idx + 2) % n];

    float d0 = (x0 - x1) * (x0 - x2);
    float d1 = (x1 - x0) * (x1 - x2);
    float d2 = (x2 - x0) * (x2 - x1);
    constexpr float eps = 1e-9f;

    if (std::fabs(d0) < eps || std::fabs(d1) < eps || std::fabs(d2) < eps) {
        float denom = x2 - x1;
        if (std::fabs(denom) < eps) { *result = y1; return true; }
        *result = y1 + (y2 - y1) * (x - x1) / denom;
        return true;
    }

    float w0 = ((x - x1) * (x - x2)) / d0;
    float w1 = ((x - x0) * (x - x2)) / d1;
    float w2 = ((x - x0) * (x - x1)) / d2;
    *result = y0 * w0 + y1 * w1 + y2 * w2;
    return true;
}
