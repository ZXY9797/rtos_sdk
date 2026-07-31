#pragma once

#include <device.h>
#include <devicetree/dma.h>
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

#define HAL_UART_NO_EXTRA_CONFIG(node_id, config)

#define HAL_GD32_UART_DMA_CONFIG(node_id, config)                  \
    do {                                                           \
        (config).dma_tx = {                                        \
            DT_REG_ADDR(DT_DMAS_CTLR_BY_NAME(node_id, tx)),        \
            DT_DMAS_CELL_BY_NAME(node_id, tx, request_id),         \
            DT_DMAS_CELL_BY_NAME(node_id, tx, channel),            \
            DT_PROP(DT_DMAS_CTLR_BY_NAME(node_id, tx),             \
                    dma_offset) +                                  \
                DT_DMAS_CELL_BY_NAME(node_id, tx, channel),        \
        };                                                         \
    } while (false)

#define HAL_UART_DT_ADAPT_IMPL(node_id, config_hook)               \
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
        using type = Uart<DT_REG_ADDR(node_id)>;                   \
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
            config_hook(node_id, config);                          \
            return static_cast<int>(instance.init(config));        \
        }                                                          \
                                                                   \
        static void isr_global(osal::IsrContext& context)          \
        {                                                          \
            instance.isr_handler(context);                         \
        }                                                          \
                                                                   \
        static void isr_dma_tx(osal::IsrContext& context)          \
        {                                                          \
            instance.dma_tx_isr(context);                          \
        }                                                          \
    };

#define HAL_UART_DT_ADAPT(node_id)                                 \
    HAL_UART_DT_ADAPT_IMPL(node_id, HAL_UART_NO_EXTRA_CONFIG)

#define HAL_GD32_UART_DT_ADAPT(node_id)                            \
    HAL_UART_DT_ADAPT_IMPL(node_id, HAL_GD32_UART_DMA_CONFIG)
