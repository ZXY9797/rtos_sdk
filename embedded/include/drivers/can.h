#pragma once

#include <drivers/status.h>
#include <cstdint>

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

} // namespace hal
