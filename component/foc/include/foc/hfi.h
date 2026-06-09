#pragma once

#include "types.h"
#include <cstdint>

namespace foc {

class HfiInjector {
public:
    struct Config {
        float amplitude {0.0f};    // 注入电压幅值
        float frequency {0.0f};    // 注入频率 Hz
        bool dual_axis {false};    // 双轴注入
    };

    explicit HfiInjector(const Config &cfg = {});

    // 注入信号生成
    // 返回附加到 v_ab 上的注入电压
    Vec2 inject(float dt);

    // 响应分析
    // i_ab: 采样电流, angle: 当前估算角度
    void analyze(const Vec2 &i_ab, uint16_t angle);

    // 角度误差输出
    float angle_error() const { return angle_error_; }

    // 是否正在运行
    bool is_active() const { return active_; }
    void set_active(bool enable) { active_ = enable; }

private:
    Config cfg_;
    float phase_ {0.0f};
    float angle_error_ {0.0f};
    bool active_ {false};

    // 电流差分
    float i_d_pos_ {0.0f};
    float i_d_neg_ {0.0f};
    float i_q_pos_ {0.0f};
    float i_q_neg_ {0.0f};
};

} // namespace foc
