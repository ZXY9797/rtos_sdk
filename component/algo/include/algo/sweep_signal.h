#pragma once

#include <cstdint>

struct SweepConfig {
    float fs {4000.0f};       // 采样频率 (Hz)
    float fstart {0.1f};      // 起始频率 (Hz)
    float fend {20.0f};       // 结束频率 (Hz)
    float fgap {0.1f};        // 频率步进 (Hz)
    float magnitude {1.0f};   // 幅值
    uint32_t repeat_time {10};// 每个频率点重复次数
};

class SweepSignal {
public:
    void init(const SweepConfig &cfg);

    // 每个采样周期调用，返回当前扫频输出值
    // 返回 true 表示扫频完成
    bool update(float &output);

    void reset();

    bool is_done() const { return done_; }
    float current_freq() const { return current_freq_; }

private:
    SweepConfig cfg_;
    float current_freq_ {0.0f};
    float phase_ {0.0f};
    float sin_val_ {0.0f};
    uint32_t repeat_cnt_ {0};
    bool done_ {false};
};
