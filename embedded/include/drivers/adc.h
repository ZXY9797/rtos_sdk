#pragma once

#include <drivers/status.h>
#include <cstdint>

namespace hal {

enum class AdcChannel : uint8_t {
    Ch0 = 0, Ch1, Ch2, Ch3, Ch4, Ch5, Ch6, Ch7,
    Ch8, Ch9, Ch10, Ch11, Ch12, Ch13, Ch14, Ch15
};

enum class AdcSampleTime : uint8_t {
    Cycles4 = 0,
    Cycles9,
    Cycles16,
    Cycles32,
    Cycles64,
    Cycles128,
    Cycles256,
    Cycles480
};

struct AdcConfig {
    uint32_t resolution {12};  // 6/8/10/12
};

struct AdcInjectedChannel {
    AdcChannel channel;
    AdcSampleTime sample_time {AdcSampleTime::Cycles64};
};

struct AdcInjectedConfig {
    uint32_t trigger_source;              // TIMER0 TRGO 等
    uint16_t channel_count;               // 注入通道数 (1-4)
    AdcInjectedChannel channels[4];
};

class AdcBase {
public:
    [[nodiscard]] Status init(const AdcConfig &cfg);
    [[nodiscard]] Status deinit();
    [[nodiscard]] bool is_initialized() const { return m_initialized; }

    // 轮询单次转换
    [[nodiscard]] Status read(AdcChannel ch, uint16_t &value);

    // 注入通道配置 (FOC 专用: TIMER0 TRGO 触发)
    [[nodiscard]] Status config_injected(const AdcInjectedConfig &cfg);

    // 读注入通道结果
    [[nodiscard]] Status read_injected(uint8_t rank, uint16_t &value);

    // 启动注入转换
    [[nodiscard]] Status start_injected();

    // 模拟看门狗
    [[nodiscard]] Status config_watchdog(AdcChannel ch, uint16_t high_threshold, uint16_t low_threshold);

    using IrqCallback = void (*)(void *arg);
    [[nodiscard]] Status set_eoc_callback(IrqCallback cb, void *arg);

protected:
    constexpr AdcBase(uintptr_t base) : m_base(base) {}
    uintptr_t m_base;
    bool m_initialized {false};
    IrqCallback m_eoc_cb {nullptr};
    void *m_eoc_arg {nullptr};
};

template <uintptr_t Base>
class Adc : public AdcBase {
public:
    constexpr Adc() : AdcBase(Base) {}
};

} // namespace hal
