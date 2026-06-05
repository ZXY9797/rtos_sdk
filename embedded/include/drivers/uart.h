#pragma once

#include <drivers/status.h>
#include <ringbuf.h>
#include <osal.h>
#include <cstdint>
#include <cstddef>

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
    size_t rx_buffer_size {0};
};

class UartBase {
public:
    explicit UartBase(uintptr_t base, int irq)
        : m_base(base), m_irq(irq), m_rx_ring(nullptr, 0) {}

    [[nodiscard]] Status init(const UartConfig &config);
    [[nodiscard]] Status deinit();
    [[nodiscard]] bool is_initialized() const { return m_initialized; }

    [[nodiscard]] Status send(const uint8_t *data, size_t len, size_t *bytes_sent, uint32_t timeout_ms);
    [[nodiscard]] Status recv(uint8_t *data, size_t len, size_t *bytes_read, uint32_t timeout_ms);
    [[nodiscard]] size_t rx_available() const;

    void isr_handler();

protected:
    uintptr_t m_base;
    int m_irq;
    RingBuf m_rx_ring;
    osal::Semaphore m_rx_sem {0};
    bool m_initialized {false};
};

template <uintptr_t Base, int Irq>
class Uart : public UartBase {
public:
    Uart() : UartBase(Base, Irq) {}
};

} // namespace hal
