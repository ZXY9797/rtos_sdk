#pragma once

#include <device.h>
#include <devicetree/dma.h>
#include <drivers/spi.h>
#include <cstdint>

#define HAL_SPI_NO_EXTRA_CONFIG(node_id, config)

#define HAL_GD32_SPI_DMA_CONFIG(node_id, config)                   \
    do {                                                           \
        (config).dma_tx = {                                        \
            DT_REG_ADDR(DT_DMAS_CTLR_BY_NAME(node_id, tx)),        \
            DT_DMAS_CELL_BY_NAME(node_id, tx, request_id),         \
            DT_DMAS_CELL_BY_NAME(node_id, tx, channel),            \
            DT_PROP(DT_DMAS_CTLR_BY_NAME(node_id, tx),             \
                    dma_offset) +                                  \
                DT_DMAS_CELL_BY_NAME(node_id, tx, channel),        \
        };                                                         \
        (config).dma_rx = {                                        \
            DT_REG_ADDR(DT_DMAS_CTLR_BY_NAME(node_id, rx)),        \
            DT_DMAS_CELL_BY_NAME(node_id, rx, request_id),         \
            DT_DMAS_CELL_BY_NAME(node_id, rx, channel),            \
            DT_PROP(DT_DMAS_CTLR_BY_NAME(node_id, rx),             \
                    dma_offset) +                                  \
                DT_DMAS_CELL_BY_NAME(node_id, rx, channel),        \
        };                                                         \
    } while (false)

#define HAL_SPI_DT_ADAPT_IMPL(node_id, config_hook)                \
    template <>                                                    \
    struct DeviceTrait<DT_ORD(node_id)> {                          \
        static constexpr uint32_t kClockHz =                       \
            DT_PROP_OR(node_id, spi_max_frequency, 1000000U);      \
                                                                   \
        static_assert(kClockHz > 0U,                               \
                      "SPI clock must be greater than zero");      \
                                                                   \
        using type = Spi<DT_REG_ADDR(node_id)>;                    \
        static type instance;                                      \
                                                                   \
        static int init()                                          \
        {                                                          \
            SpiConfig config{};                                    \
            config.mode = SpiMode::Mode0;                          \
            config.clock_hz = kClockHz;                            \
            config.data_bits = 8U;                                 \
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
                                                                   \
        static void isr_dma_rx(osal::IsrContext& context)          \
        {                                                          \
            instance.dma_rx_isr(context);                          \
        }                                                          \
    };

#define HAL_SPI_DT_ADAPT(node_id)                                  \
    HAL_SPI_DT_ADAPT_IMPL(node_id, HAL_SPI_NO_EXTRA_CONFIG)

#define HAL_GD32_SPI_DT_ADAPT(node_id)                             \
    HAL_SPI_DT_ADAPT_IMPL(node_id, HAL_GD32_SPI_DMA_CONFIG)
