#pragma once

#include <drivers/status.h>
#include <osal/osal.h>
#include <cstdint>

namespace hal {

enum class PwmChannel : uint8_t { Ch1 = 0, Ch2, Ch3, Ch4 };

struct PwmConfig {
    uint32_t prescaler {0U};
    uint32_t period {0U};
    uint32_t dead_time {0U};       // 死区计数，0 = 不启用
    bool center_aligned {false};   // 中心对齐模式
    bool complementary {false};    // 互补输出(CH0/CH0N, CH1/CH1N, CH2/CH2N)
};

class PwmBase : public osal::IrqTimer {
public:
    [[nodiscard]] Status init(const PwmConfig &config);
    [[nodiscard]] Status deinit();
    [[nodiscard]] bool is_initialized() const { return m_initialized; }

    [[nodiscard]] Status start();
    [[nodiscard]] Status stop();
    [[nodiscard]] Status set_pulse(uint32_t pulse);
    [[nodiscard]] Status set_pulse(PwmChannel ch, uint32_t pulse);
    [[nodiscard]] Status enable_output();
    [[nodiscard]] Status disable_output();

    using IrqCallback = void (*)(void *arg);
    [[nodiscard]] Status set_update_callback(IrqCallback cb, void *arg);

    /// IrqTimer 接口
    [[nodiscard]] bool enable_update_irq(IrqCallback cb, void *arg) override {
        return set_update_callback(cb, arg) == Status::Ok;
    }

    /// 获取基础地址（供 fast 命名空间使用）
    [[nodiscard]] constexpr uintptr_t base_addr() const { return m_base; }

    /// 获取通道（供 fast 命名空间使用）
    [[nodiscard]] constexpr PwmChannel channel() const { return m_channel; }

protected:
    constexpr PwmBase(uintptr_t base, PwmChannel ch) : m_base(base), m_channel(ch) {}
    uintptr_t m_base;
    PwmChannel m_channel;
    bool m_initialized {false};
    IrqCallback m_update_cb {nullptr};
    void *m_update_arg {nullptr};
};

template <uintptr_t Base, PwmChannel Ch = PwmChannel::Ch1>
class Pwm : public PwmBase {
public:
    constexpr Pwm() : PwmBase(Base, Ch) {}
};

// 特化：用于区分相同基地址不同通道的实例
template <uintptr_t Base, int ChannelIdx>
class PwmCh : public PwmBase {
public:
    constexpr PwmCh() : PwmBase(Base, static_cast<PwmChannel>(ChannelIdx)) {}
};

/// ISR 快速路径命名空间 — 提供零开销的寄存器直操作接口
///
/// 这些函数专为 FOC 电机控制等高频 ISR 场景设计：
/// - 使用 [[gnu::always_inline]] 确保无函数调用开销
/// - 直接操作寄存器，绕过常规 API 的参数检查
/// - 不使用互斥锁，调用者需确保不在多线程中竞争
namespace fast {

/// 定时器寄存器偏移（STM32/GD32 通用）
namespace reg {
    constexpr uintptr_t CR1   = 0x00;
    constexpr uintptr_t BDTR  = 0x44;
    constexpr uintptr_t CCR1  = 0x34;
    constexpr uintptr_t CCR2  = 0x38;
    constexpr uintptr_t CCR3  = 0x3C;
    constexpr uintptr_t CCR4  = 0x40;
    constexpr uintptr_t CCER  = 0x20;

    constexpr uint32_t BDTR_MOE = 1U << 15;  // 主输出使能
    constexpr uint32_t CCER_CC1E = 1U << 0;   // 通道 1 输出使能
} // namespace reg

/// 获取通道对应的 CCR 寄存器偏移
[[nodiscard]] constexpr uintptr_t ccr_offset(PwmChannel ch) {
    switch (ch) {
        case PwmChannel::Ch1: return reg::CCR1;
        case PwmChannel::Ch2: return reg::CCR2;
        case PwmChannel::Ch3: return reg::CCR3;
        case PwmChannel::Ch4: return reg::CCR4;
    }
    return reg::CCR1;
}

/// 直接设置 PWM 占空比计数值（绕过所有检查）
[[gnu::always_inline]]
inline void set_duty_count(PwmBase &pwm, uint32_t count) {
    auto *ccr = reinterpret_cast<volatile uint32_t *>(
        pwm.base_addr() + ccr_offset(pwm.channel()));
    *ccr = count;
}

/// 直接设置指定通道的占空比计数值
[[gnu::always_inline]]
inline void set_duty_count(PwmBase &pwm, PwmChannel ch, uint32_t count) {
    auto *ccr = reinterpret_cast<volatile uint32_t *>(
        pwm.base_addr() + ccr_offset(ch));
    *ccr = count;
}

/// 使能主输出（MOE 位）
[[gnu::always_inline]]
inline void enable_output(PwmBase &pwm) {
    auto *bdtr = reinterpret_cast<volatile uint32_t *>(
        pwm.base_addr() + reg::BDTR);
    *bdtr |= reg::BDTR_MOE;
}

/// 禁用主输出（MOE 位）
[[gnu::always_inline]]
inline void disable_output(PwmBase &pwm) {
    auto *bdtr = reinterpret_cast<volatile uint32_t *>(
        pwm.base_addr() + reg::BDTR);
    *bdtr &= ~reg::BDTR_MOE;
}

/// 使能指定通道输出
[[gnu::always_inline]]
inline void enable_channel(PwmBase &pwm, PwmChannel ch) {
    auto *ccer = reinterpret_cast<volatile uint32_t *>(
        pwm.base_addr() + reg::CCER);
    *ccer |= (reg::CCER_CC1E << (static_cast<uint8_t>(ch) * 4));
}

/// 禁用指定通道输出
[[gnu::always_inline]]
inline void disable_channel(PwmBase &pwm, PwmChannel ch) {
    auto *ccer = reinterpret_cast<volatile uint32_t *>(
        pwm.base_addr() + reg::CCER);
    *ccer &= ~(reg::CCER_CC1E << (static_cast<uint8_t>(ch) * 4));
}

} // namespace fast

} // namespace hal
