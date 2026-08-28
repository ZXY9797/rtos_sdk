#pragma once

#include <drivers/status.h>
#include <osal/osal.h>
#include <cstdint>

namespace hal {

enum class PwmChannel : uint8_t { Ch1 = 0, Ch2, Ch3, Ch4 };

[[nodiscard]] constexpr bool is_valid_pwm_channel(PwmChannel channel)
{
    return static_cast<uint8_t>(channel)
        <= static_cast<uint8_t>(PwmChannel::Ch4);
}

struct PwmConfig {
    uint32_t prescaler {0U};
    uint32_t period {0U};
    uint32_t dead_time {0U};
    bool three_phase {false};
    bool center_aligned {false};
    bool complementary {false};
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
    [[nodiscard]] Status set_three_phase_pulses(uint32_t channel_1,
                                                uint32_t channel_2,
                                                uint32_t channel_3);
    [[nodiscard]] Status enable_output();
    [[nodiscard]] Status disable_output();

    using IrqCallback = void (*)(void *arg);
    [[nodiscard]] Status set_update_callback(IrqCallback cb, void *arg);
    void isr_handler(osal::IsrContext& context);

    /// IrqTimer interface.
    [[nodiscard]] bool enable_update_irq(IrqCallback cb, void *arg) override {
        return set_update_callback(cb, arg) == Status::Ok;
    }

    /// Register base used by the checked fast path.
    [[nodiscard]] constexpr uintptr_t base_addr() const { return m_base; }

    /// Bound channel used by the checked fast path.
    [[nodiscard]] constexpr PwmChannel channel() const { return m_channel; }

protected:
    constexpr PwmBase(uintptr_t base, PwmChannel ch)
        : m_base(base), m_channel(ch) {}
    uintptr_t m_base;
    PwmChannel m_channel;
    bool m_initialized {false};
    bool m_three_phase {false};
    bool m_complementary {false};
    IrqCallback m_update_cb {nullptr};
    void *m_update_arg {nullptr};
};

template <uintptr_t Base, PwmChannel Ch = PwmChannel::Ch1>
class Pwm : public PwmBase {
public:
    constexpr Pwm() : PwmBase(Base, Ch) {}
};

// Distinguishes instances sharing one timer base.
template <uintptr_t Base, int ChannelIdx>
class PwmCh : public PwmBase {
public:
    constexpr PwmCh()
        : PwmBase(Base, static_cast<PwmChannel>(ChannelIdx)) {}
};

/// Zero-overhead register path for high-rate ISR control.
///
/// The caller owns serialization and must pass a valid initialized timer.
namespace fast {

/// Timer offsets shared by the supported STM32 and GD32 layouts.
namespace reg {
    constexpr uintptr_t CR1   = 0x00;
    constexpr uintptr_t BDTR  = 0x44;
    constexpr uintptr_t CCR1  = 0x34;
    constexpr uintptr_t CCR2  = 0x38;
    constexpr uintptr_t CCR3  = 0x3C;
    constexpr uintptr_t CCR4  = 0x40;
    constexpr uintptr_t CCER  = 0x20;

    constexpr uint32_t BDTR_MOE = 1U << 15;
    constexpr uint32_t CCER_CC1E = 1U << 0;
} // namespace reg

/// Return the capture/compare register offset for a channel.
[[nodiscard]] constexpr uintptr_t ccr_offset(PwmChannel ch) {
    switch (ch) {
        case PwmChannel::Ch1: return reg::CCR1;
        case PwmChannel::Ch2: return reg::CCR2;
        case PwmChannel::Ch3: return reg::CCR3;
        case PwmChannel::Ch4: return reg::CCR4;
        default: return reg::CCR1;
    }
}

/// Set the bound channel duty without runtime validation.
[[gnu::always_inline]]
inline void set_duty_count(PwmBase &pwm, uint32_t count) {
    auto *ccr = reinterpret_cast<volatile uint32_t *>(
        pwm.base_addr() + ccr_offset(pwm.channel()));
    *ccr = count;
}

/// Set an explicit channel duty without runtime validation.
[[gnu::always_inline]]
inline void set_duty_count(PwmBase &pwm, PwmChannel ch, uint32_t count) {
    auto *ccr = reinterpret_cast<volatile uint32_t *>(
        pwm.base_addr() + ccr_offset(ch));
    *ccr = count;
}

/// Enable the advanced-timer main output.
[[gnu::always_inline]]
inline void enable_output(PwmBase &pwm) {
    auto *bdtr = reinterpret_cast<volatile uint32_t *>(
        pwm.base_addr() + reg::BDTR);
    *bdtr |= reg::BDTR_MOE;
}

/// Disable the advanced-timer main output.
[[gnu::always_inline]]
inline void disable_output(PwmBase &pwm) {
    auto *bdtr = reinterpret_cast<volatile uint32_t *>(
        pwm.base_addr() + reg::BDTR);
    *bdtr &= ~reg::BDTR_MOE;
}

/// Enable an explicit timer channel.
[[gnu::always_inline]]
inline void enable_channel(PwmBase &pwm, PwmChannel ch) {
    auto *ccer = reinterpret_cast<volatile uint32_t *>(
        pwm.base_addr() + reg::CCER);
    *ccer |= (reg::CCER_CC1E << (static_cast<uint8_t>(ch) * 4));
}

/// Disable an explicit timer channel.
[[gnu::always_inline]]
inline void disable_channel(PwmBase &pwm, PwmChannel ch) {
    auto *ccer = reinterpret_cast<volatile uint32_t *>(
        pwm.base_addr() + reg::CCER);
    *ccer &= ~(reg::CCER_CC1E << (static_cast<uint8_t>(ch) * 4));
}

} // namespace fast

} // namespace hal
