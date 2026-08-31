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
    float sample_frequency_hz {1000.0F};
    float center_frequency_hz {100.0F};
    float quality_factor {0.70710678F};
};

class BiquadFilter {
public:
    BiquadFilter() = default;

    [[nodiscard]] bool configure(const BiquadConfig &config);
    void bypass();
    [[nodiscard]] bool update(float input, float &output);
    void reset();

    [[nodiscard]] bool is_configured() const { return is_configured_; }

private:
    void set_identity_coefficients();

    float b0_ {1.0F};
    float b1_ {0.0F};
    float b2_ {0.0F};
    float a1_ {0.0F};
    float a2_ {0.0F};
    float state1_ {0.0F};
    float state2_ {0.0F};
    bool is_configured_ {true};
};

[[nodiscard]] bool valid_biquad_config(const BiquadConfig &config);

} // namespace algo
