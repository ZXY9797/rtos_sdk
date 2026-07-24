#pragma once

#include <device.h>
#include <drivers/uart.h>
#include <cstddef>
#include <cstdint>

namespace hal::device_adapter {

constexpr StopBits uart_stop_bits(uint32_t enum_index)
{
    return enum_index == 3U ? StopBits::Two : StopBits::One;
}

constexpr Parity uart_parity(uint32_t enum_index)
{
    return static_cast<Parity>(enum_index);
}

} // namespace hal::device_adapter

#define HAL_UART_DT_ADAPT(node_id)                                 \
    template <>                                                    \
    struct DeviceTrait<DT_ORD(node_id)> {                          \
        static constexpr uint32_t kDataBits =                      \
            DT_PROP_OR(node_id, data_bits, 8U);                    \
        static constexpr uint32_t kStopBitsIndex =                 \
            DT_ENUM_IDX_OR(node_id, stop_bits, 1U);                \
        static constexpr uint32_t kParityIndex =                   \
            DT_ENUM_IDX_OR(node_id, parity, 0U);                   \
        static constexpr std::size_t kRxBufferSize =               \
            DT_PROP_OR(node_id, rx_buffer_size, 256U);             \
                                                                   \
        static_assert(kDataBits >= 7U && kDataBits <= 9U,          \
                      "UART data bits must be 7, 8, or 9");        \
        static_assert(kStopBitsIndex == 1U ||                      \
                      kStopBitsIndex == 3U,                        \
                      "UART supports one or two stop bits");       \
        static_assert(kParityIndex <= 2U,                          \
                      "UART supports none, odd, or even parity");  \
        static_assert(kRxBufferSize > 0U,                          \
                      "UART RX buffer must be nonzero");           \
                                                                   \
        using type = Uart<DT_REG_ADDR(node_id), DT_IRQN(node_id)>; \
        static type instance;                                      \
        inline static uint8_t rx_buffer[kRxBufferSize]{};          \
                                                                   \
        static int init()                                          \
        {                                                          \
            UartConfig config{};                                   \
            config.baudrate =                                     \
                DT_PROP_OR(node_id, current_speed, 115200U);       \
            config.data_bits = static_cast<DataBits>(kDataBits);   \
            config.stop_bits = device_adapter::uart_stop_bits(     \
                kStopBitsIndex);                                   \
            config.parity = device_adapter::uart_parity(           \
                kParityIndex);                                     \
            config.rx_buffer = rx_buffer;                          \
            config.rx_buffer_size = sizeof(rx_buffer);             \
            return static_cast<int>(instance.init(config));        \
        }                                                          \
                                                                   \
        static void isr()                                          \
        {                                                          \
            instance.isr_handler();                                \
        }                                                          \
    };
