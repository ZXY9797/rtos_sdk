#pragma once

#include <device.h>
#include <drivers/can.h>
#include <cstdint>

#define HAL_CAN_DT_ADAPT(node_id)                                  \
    template <>                                                    \
    struct DeviceTrait<DT_ORD(node_id)> {                          \
        static constexpr uint32_t kBitrate =                       \
            DT_PROP_OR(node_id, bitrate, 500000U);                 \
        static constexpr uint32_t kPort = DT_PROP(node_id, port);  \
        static constexpr uint32_t kClockHz =                       \
            DT_PROP_OR(node_id, clock_frequency, 0U);              \
        static constexpr uint32_t kDataBitrate =                   \
            DT_PROP_OR(node_id, bitrate_data, 2000000U);           \
        static constexpr uint32_t kNominalPrescaler =              \
            DT_PROP_OR(node_id, nominal_prescaler, 12U);           \
        static constexpr uint32_t kNominalTimeSeg1 =               \
            DT_PROP_OR(node_id, nominal_time_seg1, 15U);           \
        static constexpr uint32_t kNominalTimeSeg2 =               \
            DT_PROP_OR(node_id, nominal_time_seg2, 4U);            \
        static constexpr uint32_t kNominalSjw =                    \
            DT_PROP_OR(node_id, nominal_sjw, 1U);                  \
        static constexpr uint32_t kDataPrescaler =                 \
            DT_PROP_OR(node_id, data_prescaler, 3U);               \
        static constexpr uint32_t kDataTimeSeg1 =                  \
            DT_PROP_OR(node_id, data_time_seg1, 15U);              \
        static constexpr uint32_t kDataTimeSeg2 =                  \
            DT_PROP_OR(node_id, data_time_seg2, 4U);               \
        static constexpr uint32_t kDataSjw =                       \
            DT_PROP_OR(node_id, data_sjw, 1U);                     \
                                                                   \
        static_assert(kBitrate > 0U, "CAN bitrate must be nonzero");\
        static_assert(kClockHz > 0U,                               \
                      "CAN clock-frequency must be explicit");     \
        static_assert(kPort <= UINT8_MAX,                          \
                      "CAN port does not fit uint8_t");             \
        static_assert(kNominalPrescaler > 0U                       \
                      && kNominalTimeSeg1 > 0U                     \
                      && kNominalTimeSeg2 > 0U                     \
                      && kNominalSjw > 0U,                         \
                      "CAN nominal timing must be nonzero");       \
        static_assert(kDataPrescaler > 0U && kDataTimeSeg1 > 0U   \
                      && kDataTimeSeg2 > 0U && kDataSjw > 0U,     \
                      "CAN data timing must be nonzero");          \
        static_assert(kNominalPrescaler <= UINT16_MAX              \
                      && kDataPrescaler <= UINT16_MAX,             \
                      "CAN prescaler does not fit uint16_t");      \
        static_assert(kNominalTimeSeg1 <= UINT8_MAX                \
                      && kNominalTimeSeg2 <= UINT8_MAX             \
                      && kNominalSjw <= UINT8_MAX                  \
                      && kDataTimeSeg1 <= UINT8_MAX                \
                      && kDataTimeSeg2 <= UINT8_MAX                \
                      && kDataSjw <= UINT8_MAX,                    \
                      "CAN timing field does not fit uint8_t");    \
                                                                   \
        using type = CanDriver<DT_REG_ADDR(node_id),               \
                               static_cast<uint8_t>(kPort)>;       \
        static type instance;                                      \
                                                                   \
        static int init()                                          \
        {                                                          \
            CanConfig config{};                                    \
            config.bitrate = kBitrate;                             \
            config.clock_hz = kClockHz;                            \
            config.data_bitrate = kDataBitrate;                    \
            config.nominal_prescaler =                            \
                static_cast<uint16_t>(kNominalPrescaler);          \
            config.nominal_time_seg1 =                            \
                static_cast<uint8_t>(kNominalTimeSeg1);            \
            config.nominal_time_seg2 =                            \
                static_cast<uint8_t>(kNominalTimeSeg2);            \
            config.nominal_sjw = static_cast<uint8_t>(kNominalSjw);\
            config.data_prescaler =                               \
                static_cast<uint16_t>(kDataPrescaler);             \
            config.data_time_seg1 =                               \
                static_cast<uint8_t>(kDataTimeSeg1);               \
            config.data_time_seg2 =                               \
                static_cast<uint8_t>(kDataTimeSeg2);               \
            config.data_sjw = static_cast<uint8_t>(kDataSjw);      \
            return static_cast<int>(instance.init(config));        \
        }                                                          \
    };
