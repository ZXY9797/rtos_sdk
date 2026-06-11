#include <boot/boot_ctrl.h>
#include <boot/flash_map.h>

#include <cstdint>

namespace preloader {

[[noreturn]] static void jump_to(uint32_t addr) {
    const uint32_t sp = *reinterpret_cast<uint32_t *>(addr);
    const uint32_t entry = *reinterpret_cast<uint32_t *>(addr + 4);

    asm volatile("cpsid i" ::: "memory");
    asm volatile("msr msp, %0" ::"r"(sp) : "memory");
    *reinterpret_cast<volatile uint32_t *>(0xE000ED08U) = addr;
    reinterpret_cast<void (*)()>(entry)();
    while (1) {}
}

static uint32_t target_for_flag(uint8_t flag) {
    switch (flag) {
    case boot::BOOT_CTRL_UPGRADE_LOADER:
        return boot::flash_area_addr(boot::FLASH_AREA_UPGRADE);
    case boot::BOOT_CTRL_ENTER_DFU:
    case boot::BOOT_CTRL_UPGRADE_APP:
        return boot::flash_area_addr(boot::FLASH_AREA_BOOTLOADER);
    case boot::BOOT_CTRL_NORMAL:
        return boot::flash_area_addr(boot::FLASH_AREA_SLOT0);
    default:
        return boot::flash_area_addr(boot::FLASH_AREA_BOOTLOADER);
    }
}

void main() {
    uint8_t flag = boot::BOOT_CTRL_NORMAL;
    (void)boot::boot_ctrl_read(flag);
    jump_to(target_for_flag(flag));
}

} // namespace preloader
