#include <algo/signal/sweep_freq.h>

#include <algo/math/basic_math.h>

#include <math.h>

namespace algo::motor::controller {

bool FreqSweep::configure(const FreqSweepConfig& config)
{
    if ((config.sample_freq_hz <= 0.0F) ||
        (config.start_freq_hz < 0.0F) ||
        (config.end_freq_hz <= 0.0F) ||
        (config.step_freq_hz <= 0.0F) ||
        (config.repeat_cycles == 0U) ||
        !basic_math::is_finite_float(config.sample_freq_hz) ||
        !basic_math::is_finite_float(config.start_freq_hz) ||
        !basic_math::is_finite_float(config.end_freq_hz) ||
        !basic_math::is_finite_float(config.step_freq_hz) ||
        !basic_math::is_finite_float(config.magnitude)) {
        configured_ = false;
        return false;
    }

    config_ = config;
    if (config_.end_freq_hz > (config_.sample_freq_hz * 0.5F)) {
        config_.end_freq_hz = config_.sample_freq_hz * 0.5F;
    }
    configured_ = true;
    reset();
    return true;
}

void FreqSweep::reset()
{
    phase_rad_ = 0.0F;
    current_freq_hz_ = config_.start_freq_hz;
    output_freq_hz_ = config_.start_freq_hz;
    output_ = 0.0F;
    cycle_count_ = 0U;
    running_ = false;
}

bool FreqSweep::update()
{
    if (!configured_) {
        output_ = 0.0F;
        return false;
    }

    if (!running_) {
        running_ = true;
    }

    output_freq_hz_ = current_freq_hz_;
    output_ = config_.magnitude * sinf(phase_rad_);
    phase_rad_ +=
        basic_math::kTwoPi * current_freq_hz_ / config_.sample_freq_hz;

    if (phase_rad_ >= basic_math::kTwoPi) {
        phase_rad_ -= basic_math::kTwoPi;
        ++cycle_count_;
        if (cycle_count_ >= config_.repeat_cycles) {
            cycle_count_ = 0U;
            if (current_freq_hz_ >= config_.end_freq_hz) {
                running_ = false;
                return true;
            }
            current_freq_hz_ += config_.step_freq_hz;
            if (current_freq_hz_ > config_.end_freq_hz) {
                current_freq_hz_ = config_.end_freq_hz;
            }
            phase_rad_ = 0.0F;
        }
    }

    return false;
}

}  // namespace algo::motor::controller
