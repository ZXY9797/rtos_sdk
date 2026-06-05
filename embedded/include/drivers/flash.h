#pragma once

#include <drivers/status.h>
#include <cstdint>
#include <cstddef>

namespace hal {

class FlashBase {
public:
    [[nodiscard]] Status init();
    [[nodiscard]] Status deinit();
    [[nodiscard]] bool is_initialized() const { return m_initialized; }

    [[nodiscard]] Status read(uint32_t offset, void *data, size_t length) const;
    [[nodiscard]] Status write(uint32_t offset, const void *data, size_t length);
    [[nodiscard]] Status erase(uint32_t offset, size_t length);

protected:
    constexpr explicit FlashBase(uintptr_t base, uint32_t flash_base_addr)
        : m_base(base), m_flash_base(flash_base_addr) {}
    uintptr_t m_base;
    uint32_t m_flash_base;
    bool m_initialized {false};
};

template <uintptr_t Base, uint32_t FlashBaseAddr>
class Flash : public FlashBase {
public:
    constexpr Flash() : FlashBase(Base, FlashBaseAddr) {}
};

} // namespace hal
