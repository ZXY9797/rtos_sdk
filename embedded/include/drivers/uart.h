#pragma once

#include <drivers/status.h>
#include <device_base.h>
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

/// UART 运行时统计
struct UartStats {
    uint32_t tx_bytes {0};        ///< 发送字节数
    uint32_t rx_bytes {0};        ///< 接收字节数
    uint32_t overrun_count {0};   ///< 溢出错误次数 (ORE)
    uint32_t framing_errors {0};  ///< 帧错误次数 (FE)
    uint32_t parity_errors {0};   ///< 校验错误次数 (PE)
};

class UartBase : public DeviceBase {
public:
    [[nodiscard]] Status init(const UartConfig &config);
    [[nodiscard]] Status deinit();

    [[nodiscard]] Status send(const uint8_t *data, size_t len, size_t *bytes_sent, uint32_t timeout_ms);
    [[nodiscard]] Status recv(uint8_t *data, size_t len, size_t *bytes_read, uint32_t timeout_ms);
    [[nodiscard]] size_t rx_available() const;

    /// 获取运行时统计
    [[nodiscard]] UartStats get_stats() const { return m_stats; }

    /// 重置统计计数器
    void reset_stats() { m_stats = {}; }

    void isr_handler();

protected:
    explicit UartBase(uintptr_t base, int irq)
        : m_base(base), m_irq(irq) {}
    uintptr_t m_base;
    int m_irq;
    osal::StreamBuffer m_rx_stream;
    osal::Semaphore m_tx_sem {0};
    UartStats m_stats {};
};

template <uintptr_t Base, int Irq>
class Uart : public UartBase {
public:
    constexpr Uart() : UartBase(Base, Irq) {}
};

} // namespace hal
