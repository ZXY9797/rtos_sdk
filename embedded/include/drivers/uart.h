#pragma once

#include <device_base.h>
#include <drivers/dma.h>
#include <drivers/status.h>
#include <irq.h>
#include <osal.h>

#include <cstddef>
#include <cstdint>

namespace hal {

enum class DataBits : uint8_t { Bits7 = 7, Bits8 = 8, Bits9 = 9 };
enum class StopBits : uint8_t { One = 0, Two };
enum class Parity : uint8_t { None = 0, Odd, Even };

struct UartConfig {
    uint32_t baudrate {115200U};
    DataBits data_bits {DataBits::Bits8};
    StopBits stop_bits {StopBits::One};
    Parity parity {Parity::None};
    uint8_t *rx_buffer {nullptr};
    size_t rx_buffer_size {0U};
    DmaChannelConfig dma_tx {};
};

struct UartStats {
    uint32_t tx_bytes {0U};
    uint32_t rx_bytes {0U};
    uint32_t overrun_count {0U};
    uint32_t framing_errors {0U};
    uint32_t parity_errors {0U};
    uint32_t tx_timeouts {0U};
    uint32_t rx_dropped {0U};
};

class UartBase : public DeviceBase {
public:
    [[nodiscard]] Status init(const UartConfig &config);
    [[nodiscard]] Status deinit();

    [[nodiscard]] Status send(const uint8_t *data, size_t len,
                              size_t *bytes_sent, uint32_t timeout_ms);
    [[nodiscard]] Status recv(uint8_t *data, size_t len,
                              size_t *bytes_read, uint32_t timeout_ms);
    [[nodiscard]] size_t rx_available() const;

    [[nodiscard]] UartStats get_stats() const
    {
        const IrqGuard guard;
        return m_stats;
    }

    void reset_stats()
    {
        const IrqGuard guard;
        m_stats = {};
    }

    void isr_handler(osal::IsrContext &context);
    void dma_tx_isr(osal::IsrContext &context);

protected:
    explicit UartBase(uintptr_t base) : m_base(base) {}
    uintptr_t m_base;
    osal::StreamBuffer m_rx_stream;
    osal::Mutex m_tx_mutex;
    osal::Semaphore m_tx_sem {0U};
    UartStats m_stats {};
    DmaChannelConfig m_dma_tx {};
};

template <uintptr_t Base>
class Uart : public UartBase {
public:
    constexpr Uart() : UartBase(Base) {}
};

} // namespace hal
