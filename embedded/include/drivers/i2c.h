#pragma once

#include <drivers/status.h>
#include <cstdint>
#include <cstddef>

namespace hal {

enum class MemAddrSize : uint8_t { Bit8 = 0, Bit16 };

class I2cBase {
public:
    [[nodiscard]] Status init(uint32_t timing);
    [[nodiscard]] Status deinit();
    [[nodiscard]] bool is_initialized() const { return m_initialized; }

    [[nodiscard]] Status master_transmit(uint16_t addr, const uint8_t *data, size_t len, uint32_t timeout_ms);
    [[nodiscard]] Status master_receive(uint16_t addr, uint8_t *data, size_t len, uint32_t timeout_ms);
    [[nodiscard]] Status mem_write(uint16_t addr, uint16_t mem_addr, MemAddrSize addr_size,
                                   const uint8_t *data, size_t len, uint32_t timeout_ms);
    [[nodiscard]] Status mem_read(uint16_t addr, uint16_t mem_addr, MemAddrSize addr_size,
                                  uint8_t *data, size_t len, uint32_t timeout_ms);
    [[nodiscard]] Status is_ready(uint16_t addr, uint32_t retries, uint32_t timeout_ms);

protected:
    constexpr explicit I2cBase(uintptr_t base) : m_base(base) {}
    uintptr_t m_base;
    bool m_initialized {false};
};

template <uintptr_t Base>
class I2c : public I2cBase {
public:
    constexpr I2c() : I2cBase(Base) {}
};

} // namespace hal
