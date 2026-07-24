#pragma once

#include <ble/ble_device.h>
#include <device.h>
#include <cstdint>

#define HAL_BLE_DT_ADAPT(node_id)                                  \
    template <>                                                    \
    struct DeviceTrait<DT_ORD(node_id)> {                          \
        static constexpr uint32_t kIntervalMin =                   \
            DT_PROP_OR(node_id, conn_interval_min, 320U);          \
        static constexpr uint32_t kIntervalMax =                   \
            DT_PROP_OR(node_id, conn_interval_max, 520U);          \
        static constexpr uint32_t kSlaveLatency =                  \
            DT_PROP_OR(node_id, slave_latency, 0U);                \
        static constexpr uint32_t kSupervisionTimeout =            \
            DT_PROP_OR(node_id, sup_timeout, 400U);                \
        static constexpr uint32_t kSecurityLevel =                 \
            DT_PROP_OR(node_id, security_level, 2U);               \
        static constexpr uint32_t kBonding =                       \
            DT_PROP_OR(node_id, bonding, 1U);                      \
        static constexpr uint32_t kAdvIntervalMin =                \
            DT_PROP_OR(node_id, adv_interval_min, 48U);            \
        static constexpr uint32_t kAdvIntervalMax =                \
            DT_PROP_OR(node_id, adv_interval_max, 80U);            \
                                                                   \
        static_assert(kIntervalMin <= kIntervalMax,                \
                      "BLE connection interval range is invalid"); \
        static_assert(kIntervalMax <= UINT16_MAX,                  \
                      "BLE connection interval exceeds uint16_t"); \
        static_assert(kSlaveLatency <= UINT16_MAX,                 \
                      "BLE slave latency exceeds uint16_t");       \
        static_assert(kSupervisionTimeout <= UINT16_MAX,           \
                      "BLE supervision timeout exceeds uint16_t"); \
        static_assert(kSecurityLevel <= 3U,                        \
                      "BLE security level is invalid");            \
        static_assert(kBonding <= 1U,                              \
                      "BLE bonding must be zero or one");          \
        static_assert(kAdvIntervalMin <= kAdvIntervalMax,          \
                      "BLE advertising interval range is invalid");\
        static_assert(kAdvIntervalMax <= UINT16_MAX,               \
                      "BLE advertising interval exceeds uint16_t");\
                                                                   \
        using type = ble::BleDevice;                               \
        static type instance;                                      \
                                                                   \
        static int init()                                          \
        {                                                          \
            ble::StackConfig config{};                             \
            config.device_name =                                  \
                DT_PROP_OR(node_id, device_name, "BLE");           \
            config.conn_param.interval_min =                       \
                static_cast<uint16_t>(kIntervalMin);               \
            config.conn_param.interval_max =                       \
                static_cast<uint16_t>(kIntervalMax);               \
            config.conn_param.slave_latency =                      \
                static_cast<uint16_t>(kSlaveLatency);              \
            config.conn_param.sup_timeout =                        \
                static_cast<uint16_t>(kSupervisionTimeout);        \
            config.sec_param.level =                              \
                static_cast<ble::SecParam::Level>(kSecurityLevel); \
            config.sec_param.bonding = (kBonding != 0U);           \
            config.adv_interval_min =                              \
                static_cast<uint16_t>(kAdvIntervalMin);            \
            config.adv_interval_max =                              \
                static_cast<uint16_t>(kAdvIntervalMax);            \
            return instance.init(config);                          \
        }                                                          \
    };
