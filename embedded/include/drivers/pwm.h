#pragma once

#include <drivers/status.h>
#include <cstdint>

namespace hal {

enum class PwmChannel : uint8_t { Ch1 = 0, Ch2, Ch3, Ch4 };

struct PwmConfig {
    uint32_t prescaler {0U};
    uint32_t period {0U};
};

class PwmBase {
public:
    [[nodiscard]] Status init(const PwmConfig &config);
    [[nodiscard]] Status deinit();
    [[nodiscard]] bool is_initialized() const { return m_initialized; }

    [[nodiscard]] Status start();
    [[nodiscard]] Status stop();
    [[nodiscard]] Status set_pulse(uint32_t pulse);

protected:
    constexpr PwmBase(uintptr_t base, PwmChannel ch) : m_base(base), m_channel(ch) {}
    uintptr_t m_base;
    PwmChannel m_channel;
    bool m_initialized {false};
};

template <uintptr_t Base, PwmChannel Ch = PwmChannel::Ch1>
class Pwm : public PwmBase {
public:
    constexpr Pwm() : PwmBase(Base, Ch) {}
};

} // namespace hal
