#include <drivers/flash.h>

extern "C" {
#include "gr55xx_hal_exflash.h"
}

namespace hal {
namespace {

constexpr uint32_t GOODIX_FLASH_BASE = EXFLASH_START_ADDR;
constexpr uint32_t GOODIX_WRITE_BLOCK = 4;
constexpr uint32_t GOODIX_SECTOR_SIZE = EXFLASH_SIZE_SECTOR_BYTES;

static Status goodix_read(void *, uint32_t offset, void *data, size_t len) {
    const uint32_t addr = GOODIX_FLASH_BASE + offset;
    return hal_exflash_read(addr, static_cast<uint8_t *>(data),
                            static_cast<uint32_t>(len)) == HAL_OK
        ? Status::Ok
        : Status::HardwareError;
}

static Status goodix_write_block(void *, uint32_t addr,
                                 const void *data, size_t len) {
    return hal_exflash_write(addr,
                             const_cast<uint8_t *>(
                                 static_cast<const uint8_t *>(data)),
                             static_cast<uint32_t>(len)) == HAL_OK
        ? Status::Ok
        : Status::HardwareError;
}

static Status goodix_erase_sector(void *, uint32_t addr) {
    return hal_exflash_erase(EXFLASH_ERASE_SECTOR, addr, GOODIX_SECTOR_SIZE)
               == HAL_OK
        ? Status::Ok
        : Status::HardwareError;
}

static void goodix_noop(void *) {}

} // namespace

Flash flash_create_default() {
    (void)hal_exflash_init();
    return Flash(GOODIX_FLASH_BASE, GOODIX_WRITE_BLOCK,
                 GOODIX_SECTOR_SIZE, nullptr,
                 goodix_write_block, goodix_erase_sector,
                 goodix_noop, goodix_noop, goodix_read);
}

} // namespace hal
