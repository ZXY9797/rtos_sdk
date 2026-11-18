#pragma once

#include <stdint.h>

namespace algo::motor::controller {

struct FreqSweepConfig {
    float sample_freq_hz = 0.0F;
    float start_freq_hz = 0.0F;
    float end_freq_hz = 0.0F;
    float step_freq_hz = 0.0F;
    float magnitude = 0.0F;
    uint8_t repeat_cycles = 0U;
};

class FreqSweep {
public:
    bool configure(const FreqSweepConfig& config);
    void reset();

    // Returns true when the sweep reaches end_freq_hz.
    bool update();

    float output() const { return output_; }
    float current_freq_hz() const { return current_freq_hz_; }
    // Frequency used to generate the most recent output sample. This differs
    // from current_freq_hz() for the single update that advances to a new bin.
    float output_freq_hz() const { return output_freq_hz_; }
    bool running() const { return running_; }

private:
    FreqSweepConfig config_ {};
    float phase_rad_ = 0.0F;
    float current_freq_hz_ = 0.0F;
    float output_freq_hz_ = 0.0F;
    float output_ = 0.0F;
    uint8_t cycle_count_ = 0U;
    bool configured_ = false;
    bool running_ = false;
};

}  // namespace algo::motor::controller
