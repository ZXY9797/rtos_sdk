#pragma once

#include <device.h>
#include <drivers/adc.h>
#include <devicetree/dma.h>

#define HAL_ADC_NO_SCAN_CONFIG(node_id, config)
#define HAL_ADC_SCAN_CONFIG(node_id, config)                        \
    do {                                                           \
        (config).oversample_ratio = DT_PROP(node_id, oversampling_ratio); \
        (config).oversample_shift = DT_PROP(node_id, oversampling_shift); \
        (config).scan_dma = {                                      \
            DT_REG_ADDR(DT_DMAS_CTLR_BY_NAME(node_id, rx)),         \
            DT_DMAS_CELL_BY_NAME(node_id, rx, request_id),          \
            DT_DMAS_CELL_BY_NAME(node_id, rx, channel),             \
            DT_PROP(DT_DMAS_CTLR_BY_NAME(node_id, rx), dma_offset) + \
                DT_DMAS_CELL_BY_NAME(node_id, rx, channel)};        \
    } while (false)

#define HAL_ADC_DT_ADAPT_IMPL(node_id, config_hook)                 \
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
            config_hook(node_id, config);                          \
            return static_cast<int>(instance.init(config));        \
        }                                                          \
                                                                   \
        static void isr_global(osal::IsrContext& context)          \
        {                                                          \
            instance.isr_handler(context);                         \
        }                                                          \
        static void isr_dma_rx(osal::IsrContext& context)          \
        { instance.scan_dma_isr(context); }                        \
    };

#define HAL_ADC_DT_ADAPT(node_id) HAL_ADC_DT_ADAPT_IMPL(node_id, HAL_ADC_NO_SCAN_CONFIG)
#define HAL_GD32_ADC_SCAN_DT_ADAPT(node_id) HAL_ADC_DT_ADAPT_IMPL(node_id, HAL_ADC_SCAN_CONFIG)
