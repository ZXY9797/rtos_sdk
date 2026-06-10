#pragma once

#include <cstdint>

namespace algo {

enum class BiquadType : uint8_t {
    LowPass,
    HighPass,
    BandPass,
    Notch,
};

struct BiquadConfig {
    BiquadType type {BiquadType::LowPass};
    float fs {1000.0f};
    float f0 {100.0f};
    float Q {0.707f};
};

class BiquadFilter {
public:
    BiquadFilter() = default;

    void init(const BiquadConfig &cfg);
    void set_params(float f0, float Q);
    float update(float x);
    void reset();

    float b0() const { return b0_; }
    float b1() const { return b1_; }
    float b2() const { return b2_; }
    float a1() const { return a1_; }
    float a2() const { return a2_; }

private:
    BiquadConfig cfg_;
    float b0_{0}, b1_{0}, b2_{0}, a1_{0}, a2_{0};
    float x1_{0}, x2_{0}, y1_{0}, y2_{0};

    void calculate_coefficients();
};

} // namespace algo
