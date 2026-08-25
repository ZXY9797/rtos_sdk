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
                                        uint8_t *len) {
        return can().get_rx_message(id, data, len);
    }

    Can &native() { return can(); }

private:
    Can &can() { return Can::instance(Port); }
    const Can &can() const { return Can::instance(Port); }
};

} // namespace hal
