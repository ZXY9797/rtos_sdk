#pragma once

#include <algo/filter/transfer_function.h>

#include <stdint.h>

namespace algo::motor::filter {

inline float low_pass_filter(float input, float last_output, float alpha)
{
    return (alpha * input) + ((1.0F - alpha) * last_output);
}

enum class FilterProto : uint8_t {
    Butter = 0,
    Cheby2 = 1,
};

enum class FilterPassType : uint8_t {
    LowPass = 0,
    HighPass = 1,
};

struct FilterSysConfig {
    FilterProto proto = FilterProto::Butter;
    FilterPassType pass_type = FilterPassType::LowPass;
    Order order = Order::First;
    float sample_freq_hz = 0.0F;
    float freq_hz = 0.0F;
    float attenuation_db = 0.0F;
};

class FilterSys {
public:
    bool configure(const FilterSysConfig& config);
    void reset();
    float update(float input);

    const FilterSysConfig& design_config() const { return design_config_; }
    const TransferFunctionConfig& transfer_config() const { return transfer_config_; }
    bool configured() const { return configured_; }

private:
    FilterSysConfig design_config_ {};
    TransferFunctionConfig transfer_config_ {};
    TransferFunction tf_ {};
    bool configured_ = false;
};

}  // namespace algo::motor::filter
