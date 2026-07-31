#pragma once

#include <device.h>
#include <drivers/adc.h>

#define HAL_ADC_DT_ADAPT(node_id)                                  \
    template <>                                                    \
    struct DeviceTrait<DT_ORD(node_id)> {                          \
        static constexpr uint32_t kResolution =                    \
            DT_PROP_OR(node_id, resolution, 12U);                  \
                                                                   \
        static_assert(kResolution == 6U || kResolution == 8U ||    \
                      kResolution == 10U || kResolution == 12U,    \
                      "ADC resolution must be 6, 8, 10, or 12");   \
                                                                   \
        using type = Adc<DT_REG_ADDR(node_id)>;                    \
        static type instance;                                      \
                                                                   \
        static int init()                                          \
        {                                                          \
            AdcConfig config{};                                    \
            config.resolution = kResolution;                       \
            return static_cast<int>(instance.init(config));        \
        }                                                          \
                                                                   \
        static void isr_global(osal::IsrContext& context)          \
        {                                                          \
            instance.isr_handler(context);                         \
        }                                                          \
    };
