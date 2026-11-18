#pragma once

#include <drivers/status.h>
#include <drivers/dma.h>
#include <cstdint>
#include <cstddef>

namespace osal {
class IsrContext;
}

namespace hal {

enum class AdcChannel : uint8_t {
    Ch0 = 0, Ch1, Ch2, Ch3, Ch4, Ch5, Ch6, Ch7,
    Ch8, Ch9, Ch10, Ch11, Ch12, Ch13, Ch14, Ch15, Ch16, Ch17
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
    uint16_t oversample_ratio {1U};
    uint8_t oversample_shift {0U};
    DmaChannelConfig scan_dma {};
};

struct AdcInjectedChannel {
    AdcChannel channel;
    AdcSampleTime sample_time {AdcSampleTime::Cycles64};
};

struct AdcInjectedConfig {
    uint32_t trigger_source;
    uint16_t channel_count;
    AdcInjectedChannel channels[4];
};

struct AdcScanConfig {
    static constexpr uint32_t software_trigger = UINT32_MAX;
    // SoC trigger-multiplexer input, or software_trigger.
    uint32_t trigger_input {software_trigger};
    uint8_t channel_count {0U};
    AdcInjectedChannel channels[16] {};
};

class AdcBase {
public:
    [[nodiscard]] Status init(const AdcConfig &cfg);
    [[nodiscard]] Status deinit();
    [[nodiscard]] bool is_initialized() const { return m_initialized; }

    // Poll one routine conversion.
    [[nodiscard]] Status read(AdcChannel ch, uint16_t &value);

    // Configure the acquisition time for a channel.
    [[nodiscard]] Status set_sample_time(AdcChannel channel,
                                         AdcSampleTime sample_time);

    // Configure TIMER-triggered inserted channels for FOC sampling.
    [[nodiscard]] Status config_injected(const AdcInjectedConfig &cfg);

    // Read one inserted result rank.
    [[nodiscard]] Status read_injected(uint8_t rank, uint16_t &value);

    // Start an inserted conversion.
    [[nodiscard]] Status start_injected();

    // Owns a circular DMA scan. The caller supplies two complete scan frames.
    // Each callback borrows the completed half until the next scan finishes.
    using ScanCallback = void (*)(void*, const uint16_t*, size_t, Status);
    [[nodiscard]] Status start_scan(const AdcScanConfig& config, uint16_t* buffer,
                                    size_t count, ScanCallback callback, void* argument);
    [[nodiscard]] Status stop_scan();
    void scan_dma_isr(osal::IsrContext& context);

    // Configure the analog watchdog.
    [[nodiscard]] Status config_watchdog(AdcChannel channel,
                                         uint16_t high_threshold,
                                         uint16_t low_threshold);

    using IrqCallback = void (*)(void *arg);
    [[nodiscard]] Status set_eoc_callback(IrqCallback cb, void *arg);
    void isr_handler(osal::IsrContext &context);

protected:
    constexpr AdcBase(uintptr_t base) : m_base(base) {}
    uintptr_t m_base;
    bool m_initialized {false};
    uint8_t m_injected_count {0U};
    IrqCallback m_eoc_cb {nullptr};
    void *m_eoc_arg {nullptr};
    DmaChannelConfig m_scan_dma {};
    DmaChannel m_scan_channel {0U, 0xFFU, 0U};
    uint16_t* m_scan_buffer {nullptr};
    uint8_t m_scan_count {0U};
    ScanCallback m_scan_callback {nullptr};
    void* m_scan_argument {nullptr};
};

template <uintptr_t Base>
class Adc : public AdcBase {
public:
    constexpr Adc() : AdcBase(Base) {}
};

} // namespace hal
