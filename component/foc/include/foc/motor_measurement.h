#pragma once

#include "types.h"
#include <cstdint>

namespace foc {

class MotorMeasurement {
public:
    enum class State : uint8_t {
        Idle,
        MeasuringRs,    // 测量定子电阻
        MeasuringLs,    // 测定电感
        MeasuringFlux,  // 测量磁链
        Done
    };

    MotorMeasurement() = default;

    // 启动测量序列
    void start();

    // ISR 中调用 — 返回需要的电压矢量
    // 返回 false 表示测量完成
    bool update(float vbus, float dt, Vec2 &v_ab_out);

    // 结果
    float rs() const { return rs_; }
    float ld() const { return ld_; }
    float lq() const { return lq_; }
    float flux_linkage() const { return flux_; }
    State state() const { return state_; }
    bool is_done() const { return state_ == State::Done; }

    // 设置测量参数
    void set_test_current(float current) { test_current_ = current; }
    void set_motor_pole_pairs(uint8_t pp) { pole_pairs_ = pp; }

private:
    State state_ {State::Idle};
    float rs_ {0.0f};
    float ld_ {0.0f};
    float lq_ {0.0f};
    float flux_ {0.0f};
    float test_current_ {1.0f};
    uint8_t pole_pairs_ {7};

    // 内部状态
    float accum_ {0.0f};
    float accum2_ {0.0f};
    uint32_t count_ {0};
    float phase_ {0.0f};
};

} // namespace foc
