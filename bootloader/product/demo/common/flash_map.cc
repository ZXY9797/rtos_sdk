#include <boot/flash_map.h>

namespace boot {
namespace {

constexpr uint32_t kFlashBaseAddr = 0x08000000;

const FlashArea kFlashAreas[FLASH_AREA_COUNT] = {
    [FLASH_AREA_PRELOADER] = { 0x00000000, 16 * 1024 },
    [FLASH_AREA_BOOTLOADER]= { 0x00004000, 24 * 1024 },
    [FLASH_AREA_SLOT0]     = { 0x0000A000, 160 * 1024 },
    [FLASH_AREA_UPGRADE]   = { 0x00032000, 152 * 1024 },
    [FLASH_AREA_STORAGE]   = { 0x00058000, 32 * 1024 },
};

} // namespace

uint32_t flash_base_addr() {
    return kFlashBaseAddr;
}

const FlashArea &flash_area_get(FlashAreaId id) {
    return kFlashAreas[id];
}

} // namespace boot
