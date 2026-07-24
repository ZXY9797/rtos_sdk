#pragma once

#include <device.h>
#include <drivers/can.h>
#include <cstdint>

#define HAL_CAN_DT_ADAPT(node_id)                                  \
    template <>                                                    \
    struct DeviceTrait<DT_ORD(node_id)> {                          \
        static constexpr uint32_t kBitrate =                       \
            DT_PROP_OR(node_id, bitrate, 500000U);                 \
        static constexpr uint32_t kIrqPriority =                   \
            DT_PROP_OR(node_id, irq_priority, 8U);                 \
                                                                   \
        static_assert(kBitrate > 0U, "CAN bitrate must be nonzero");\
        static_assert(kIrqPriority <= UINT8_MAX,                   \
                      "CAN IRQ priority does not fit uint8_t");     \
                                                                   \
        using type = CanDriver<DT_REG_ADDR(node_id)>;              \
        static type instance;                                      \
                                                                   \
        static int init()                                          \
        {                                                          \
            CanConfig config{};                                    \
            config.bitrate = kBitrate;                             \
            config.irq_priority =                                  \
                static_cast<uint8_t>(kIrqPriority);                \
            return static_cast<int>(instance.init(config));        \
        }                                                          \
    };
