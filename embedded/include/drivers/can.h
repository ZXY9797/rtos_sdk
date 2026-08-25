#pragma once

#include <drivers/status.h>
#include <osal.h>
#include <atomic>
#include <cstdint>
#include <cstddef>

namespace hal {

struct CanConfig {
    uint32_t bitrate {500000U};
    uint32_t clock_hz {0U};
    uint32_t data_bitrate {2000000U};
    uint16_t nominal_prescaler {12U};
    uint8_t nominal_time_seg1 {15U};
    uint8_t nominal_time_seg2 {4U};
    uint8_t nominal_sjw {1U};
    uint16_t data_prescaler {3U};
    uint8_t data_time_seg1 {15U};
    uint8_t data_time_seg2 {4U};
    uint8_t data_sjw {1U};
};

class Can {
public:
    static Can &instance(uint8_t port);
    constexpr uint8_t port() const { return m_port; }

    [[nodiscard]] Status init(const CanConfig &config);
    [[nodiscard]] Status deinit();
    [[nodiscard]] bool is_initialized() const {
        return m_initialized.load(std::memory_order_acquire);
    }
    [[nodiscard]] bool is_started() const;

    [[nodiscard]] Status start();
    [[nodiscard]] Status stop();
    [[nodiscard]] Status send(uint32_t id, const uint8_t *data, uint8_t len, uint32_t id_ext);
    [[nodiscard]] Status get_rx_message(uint32_t *id, uint8_t *data,
                                        uint8_t capacity, uint8_t *len,
                                        bool *is_extended = nullptr);

private:
    explicit Can(uint8_t port) : m_port(port) {}
    uint8_t m_port;
    mutable osal::Mutex m_operation_mutex;
    std::atomic<bool> m_initialized {false};
};

/// CAN 驱动模板包装器（适配 DeviceTrait 体系）
/// Base: CAN 外设基地址，用于从设备树获取 port 编号
template <uintptr_t Base, uint8_t Port>
class CanDriver {
public:
    static_assert(Base != 0U, "CAN register base must be nonzero");
    [[nodiscard]] Status init(const CanConfig &config) {
        return can().init(config);
    }

    [[nodiscard]] Status deinit() {
        return can().deinit();
    }

    [[nodiscard]] bool is_initialized() const {
        return can().is_initialized();
    }

    [[nodiscard]] Status start() { return can().start(); }
    [[nodiscard]] Status stop() { return can().stop(); }

    [[nodiscard]] Status send(uint32_t id, const uint8_t *data,
                              uint8_t len, uint32_t id_ext = 0) {
        return can().send(id, data, len, id_ext);
    }

    [[nodiscard]] Status get_rx_message(uint32_t *id, uint8_t *data,
                                        uint8_t capacity, uint8_t *len,
                                        bool *is_extended = nullptr) {
        return can().get_rx_message(id, data, capacity, len, is_extended);
    }

    Can &native() { return can(); }

private:
    Can &can() { return Can::instance(Port); }
    const Can &can() const { return Can::instance(Port); }
};

} // namespace hal
