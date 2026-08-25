#pragma once

#include <device.h>
#include <drivers/i2c.h>

#define HAL_I2C_DT_ADAPT(node_id)                                  \
    template <>                                                    \
    struct DeviceTrait<DT_ORD(node_id)> {                          \
        static constexpr uint32_t kTiming =                        \
            DT_PROP(node_id, timing);                              \
        static_assert(kTiming != 0U,                               \
                      "I2C TIMINGR value must be nonzero");       \
                                                                   \
        using type = I2c<DT_REG_ADDR(node_id)>;                    \
        static type instance;                                      \
                                                                   \
        static int init()                                          \
        {                                                          \
            return static_cast<int>(instance.init(kTiming));       \
        }                                                          \
                                                                   \
        static void isr_event(osal::IsrContext& context)           \
        {                                                          \
            instance.isr_handler(context);                         \
        }                                                          \
                                                                   \
        static void isr_error(osal::IsrContext& context)           \
        {                                                          \
            instance.isr_handler(context);                         \
        }                                                          \
    };
