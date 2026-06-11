#include <boot/boot_ctrl.h>
#include <boot/flash_map.h>
#include <boot/flash_ops.h>
#include <boot/image.h>

#include <algorithm>
#include <cstdint>

namespace preloader {
namespace {

void jump_to(uint32_t addr) {
    const uint32_t sp = *reinterpret_cast<uint32_t *>(addr);
    const uint32_t entry = *reinterpret_cast<uint32_t *>(addr + 4);

    asm volatile("cpsid i" ::: "memory");
    asm volatile("msr msp, %0" ::"r"(sp) : "memory");
    *reinterpret_cast<volatile uint32_t *>(0xE000ED08U) = addr;
    reinterpret_cast<void (*)()>(entry)();
    while (1) {}
}

bool has_loader_upgrade() {
    uint8_t flag = 0;
    (void)boot::loader_upgrade_read(flag);
    return flag != 0;
}

bool copy_upgrade_to_slot0(uint32_t copy_len) {
    const auto &src_area = boot::flash_area_get(boot::FLASH_AREA_UPGRADE);
    const auto &dst_area = boot::flash_area_get(boot::FLASH_AREA_SLOT0);
    const uint32_t sector_size = boot::flash_erase_sector_size();

    if (sector_size == 0 || sector_size > 4096 ||
        copy_len > src_area.size || copy_len > dst_area.size) {
        return false;
    }

    const uint32_t src_addr = boot::flash_area_addr(boot::FLASH_AREA_UPGRADE);
    const uint32_t dst_addr = boot::flash_area_addr(boot::FLASH_AREA_SLOT0);
    static uint8_t sector[4096];

    for (uint32_t offset = 0; offset < copy_len; offset += sector_size) {
        const uint32_t chunk = std::min(sector_size, copy_len - offset);
        if (!boot::flash_read(src_addr + offset, sector, chunk)) return false;
        if (!boot::flash_erase(dst_addr + offset, sector_size)) return false;
        if (!boot::flash_write(dst_addr + offset, sector, chunk)) return false;
    }

    return true;
}

} // namespace

bool check_and_do_loader_upgrade() {
    if (!has_loader_upgrade()) return false;

    const uint32_t src_addr = boot::flash_area_addr(boot::FLASH_AREA_UPGRADE);
    boot::ImageHeader hdr{};
    if (!boot::flash_read(src_addr, &hdr, sizeof(hdr)) ||
        hdr.magic != boot::IMAGE_MAGIC ||
        hdr.hdr_size != sizeof(boot::ImageHeader) ||
        hdr.img_size == 0) {
        (void)boot::loader_upgrade_clear();
        return false;
    }

    const uint64_t copy_len64 =
        static_cast<uint64_t>(hdr.hdr_size) + hdr.img_size;
    if (copy_len64 > UINT32_MAX ||
        !copy_upgrade_to_slot0(static_cast<uint32_t>(copy_len64))) {
        return false;
    }

    (void)boot::loader_upgrade_clear();
    jump_to(boot::flash_area_addr(boot::FLASH_AREA_SLOT0));
    return true;
}

} // namespace preloader
