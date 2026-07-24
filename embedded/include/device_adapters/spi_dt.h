#pragma once

#include <device.h>
#include <drivers/spi.h>
#include <cstdint>

#define HAL_SPI_DT_ADAPT(node_id)                                  \
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
            return static_cast<int>(instance.init(config));        \
        }                                                          \
    };
