#pragma once

#include <algo/filter/transfer_function.h>

namespace algo::motor::filter {

struct NotchPeakConfig {
    float center_freq_hz = 0.0F;
    float bandwidth_hz = 0.0F;
    float as_param = 0.0F;
    float sample_freq_hz = 0.0F;
};

class NotchPeak {
public:
    bool configure(const NotchPeakConfig& config);
    void reset();
    float update(float input);

    const NotchPeakConfig& design_config() const { return design_config_; }
    const TransferFunctionConfig& transfer_config() const { return transfer_config_; }
    bool configured() const { return configured_; }

private:
    NotchPeakConfig design_config_ {};
    TransferFunctionConfig transfer_config_ {};
    TransferFunction tf_ {};
    bool configured_ = false;
};

}  // namespace algo::motor::filter
