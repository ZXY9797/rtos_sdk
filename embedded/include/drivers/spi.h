#pragma once

#include <drivers/status.h>
#include <cstdint>
#include <cstddef>

namespace hal {

enum class SpiMode : uint8_t { Mode0 = 0, Mode1, Mode2, Mode3 };

struct SpiConfig {
    SpiMode mode {SpiMode::Mode0};
    uint32_t clock_hz {1000000U};
    uint8_t data_bits {8};
};

class SpiBase {
public:
    [[nodiscard]] Status init(const SpiConfig &config);
    [[nodiscard]] Status deinit();
    [[nodiscard]] bool is_initialized() const { return m_initialized; }

    [[nodiscard]] Status sync_send(const uint8_t *tx, uint8_t *rx, size_t len, uint32_t timeout_ms);

protected:
    constexpr explicit SpiBase(uintptr_t base) : m_base(base) {}
    uintptr_t m_base;
    bool m_initialized {false};
};

template <uintptr_t Base>
class Spi : public SpiBase {
public:
    constexpr Spi() : SpiBase(Base) {}
};

} // namespace hal
