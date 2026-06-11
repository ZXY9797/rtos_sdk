#include <boot/flash_map.h>

namespace boot {
namespace {

constexpr uint32_t kFlashBaseAddr = 0x00200000;

const FlashArea kFlashAreas[FLASH_AREA_COUNT] = {
    [FLASH_AREA_PRELOADER] = { 0x00002000, 16 * 1024 },
    [FLASH_AREA_BOOTLOADER]= { 0x00006000, 24 * 1024 },
    [FLASH_AREA_SLOT0]     = { 0x0000C000, 160 * 1024 },
    [FLASH_AREA_UPGRADE]   = { 0x00034000, 784 * 1024 },
    [FLASH_AREA_STORAGE]   = { 0x000F8000, 32 * 1024 },
};

} // namespace

uint32_t flash_base_addr() {
    return kFlashBaseAddr;
}

const FlashArea &flash_area_get(FlashAreaId id) {
    return kFlashAreas[id];
}

} // namespace boot
