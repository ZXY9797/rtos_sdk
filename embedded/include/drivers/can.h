#pragma once

#include <drivers/status.h>
#include <cstdint>
#include <cstddef>

namespace hal {

struct CanConfig {
    uint32_t bitrate {500000U};
    uint8_t irq_priority {8U};
};

class Can {
public:
    static Can &instance(uint8_t port);
    constexpr uint8_t port() const { return m_port; }

    [[nodiscard]] Status init(const CanConfig &config);
    [[nodiscard]] Status deinit();
    [[nodiscard]] bool is_initialized() const { return m_initialized; }

    [[nodiscard]] Status start();
    [[nodiscard]] Status stop();
    [[nodiscard]] Status send(uint32_t id, const uint8_t *data, uint8_t len, uint32_t id_ext);
    [[nodiscard]] Status get_rx_message(uint32_t *id, uint8_t *data, uint8_t *len);

private:
    constexpr explicit Can(uint8_t port) : m_port(port), m_initialized(false) {}
    uint8_t m_port;
    bool m_initialized;
};

/// CAN 驱动模板包装器（适配 DeviceTrait 体系）
/// Base: CAN 外设基地址，用于从设备树获取 port 编号
template <uintptr_t Base>
class CanDriver {
public:
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
                                        uint8_t *len) {
        return can().get_rx_message(id, data, len);
    }

private:
    static constexpr uint8_t base_to_port(uintptr_t base);
    Can &can() { return Can::instance(base_to_port(Base)); }
};

template <uintptr_t Base>
constexpr uint8_t CanDriver<Base>::base_to_port(uintptr_t base) {
    // GD32 CAN: CAN0=0, CAN1=1; STM32: CAN1=0
    // 由 DeviceTrait 特化覆盖此函数
    return static_cast<uint8_t>(base & 0xFFU);
}

} // namespace hal
