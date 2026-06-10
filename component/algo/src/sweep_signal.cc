#include "algo/sweep_signal.h"
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

void SweepSignal::init(const SweepConfig &cfg) {
    cfg_ = cfg;
    reset();
}

void SweepSignal::reset() {
    current_freq_ = cfg_.fstart;
    phase_ = 0.0f;
    sin_val_ = 0.0f;
    repeat_cnt_ = 0;
    done_ = false;
}

bool SweepSignal::update(float &output) {
    if (done_) {
        output = 0.0f;
        return true;
    }

    sin_val_ = cfg_.magnitude * std::sin(phase_);
    output = sin_val_;

    // 相位递增: dPhase = 2*pi*freq/fs
    phase_ += 2.0f * static_cast<float>(M_PI) * current_freq_ / cfg_.fs;
    if (phase_ > 2.0f * static_cast<float>(M_PI)) {
        phase_ -= 2.0f * static_cast<float>(M_PI);
        repeat_cnt_++;

        if (repeat_cnt_ >= cfg_.repeat_time) {
            repeat_cnt_ = 0;
            current_freq_ += cfg_.fgap;
            if (current_freq_ > cfg_.fend) {
                done_ = true;
                output = 0.0f;
                return true;
            }
        }
    }

    return false;
}
