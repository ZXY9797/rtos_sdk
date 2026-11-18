#pragma once

#include <cstdint>

enum class LagrangeOrder : uint8_t {
    First = 1,
    Second = 2,
};

struct LagrangeConfig {
    const char *name {""};
    LagrangeOrder order {LagrangeOrder::Second};
    float x_min {0.0f};
    float x_max {1.0f};
    uint32_t point_count {0};
};

class LagrangeInterp {
public:
    LagrangeInterp() = default;
    void init(const LagrangeConfig &cfg, float *y_offsets);

    // 给定 x，查表插值得到 y 偏移量
    bool fit(float x, float *result) const;

    bool is_valid() const { return y_ != nullptr && cfg_.point_count >= 2; }

private:
    LagrangeConfig cfg_;
    float *y_ {nullptr};
    float dx_ {0.0f};

    bool fit_order1(float x, uint32_t idx, float *result) const;
    bool fit_order2(float x, uint32_t idx, float *result) const;
};
