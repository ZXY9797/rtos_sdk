#pragma once

#include <cstdint>

namespace boot {

struct FlashArea {
    uint32_t offset;
    uint32_t size;
};

enum FlashAreaId : uint8_t {
    FLASH_AREA_PRELOADER = 0,
    FLASH_AREA_BOOTLOADER,
    FLASH_AREA_SLOT0,
    FLASH_AREA_UPGRADE,
    FLASH_AREA_STORAGE,
    FLASH_AREA_COUNT,
};

uint32_t flash_base_addr();
const FlashArea &flash_area_get(FlashAreaId id);

inline uint32_t flash_area_addr(FlashAreaId id) {
    return flash_base_addr() + flash_area_get(id).offset;
}

} // namespace boot
