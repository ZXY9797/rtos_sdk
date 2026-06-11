#include <drivers/flash.h>
#include <cstring>

extern "C" {
#include <gd32f50x_fmc.h>
}

namespace hal {

constexpr uint32_t GD32_FLASH_BASE = 0x08000000U;
constexpr uint32_t GD32_WRITE_BLOCK = 4;
constexpr uint32_t GD32_SECTOR_SIZE = 2048;

static Status gd32_write_block(void *, uint32_t addr,
                               const void *data, size_t len) {
    uint32_t word = 0xFFFFFFFFU;
    std::memcpy(&word, data, (len < 4) ? len : 4);
    fmc_state_enum ret = fmc_word_program(addr, word);
    return (ret == FMC_READY) ? Status::Ok
                              : Status::HardwareError;
}

static Status gd32_erase_sector(void *, uint32_t addr) {
    fmc_state_enum ret = fmc_page_erase(addr);
    return (ret == FMC_READY) ? Status::Ok
                              : Status::HardwareError;
}

static void gd32_unlock(void *) {
    fmc_bank0_unlock();
}

static void gd32_lock(void *) {
    fmc_bank0_lock();
}

Flash flash_create_default() {
    return Flash(GD32_FLASH_BASE, GD32_WRITE_BLOCK,
                    GD32_SECTOR_SIZE, nullptr,
                    gd32_write_block, gd32_erase_sector,
                    gd32_lock, gd32_unlock);
}

} // namespace hal
