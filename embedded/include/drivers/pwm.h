#pragma once

#include <drivers/status.h>
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

class PwmBase {
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

} // namespace hal
