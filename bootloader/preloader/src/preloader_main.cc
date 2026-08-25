#include <boot/boot_ctrl.h>
#include <boot/flash_map.h>
#include <boot/handoff.h>
#include <boot/image.h>
#include <boot_layout.h>

#include <cstdint>

namespace preloader {
namespace {

[[noreturn]] void halt()
{
    asm volatile("cpsid i" ::: "memory");
    while (1) {
    }
}

bool valid_vectors(boot::FlashAreaId area_id, uint32_t& sp, uint32_t& entry)
{
    const boot::FlashArea& area = boot::flash_area_get(area_id);
    const uint32_t address = boot::flash_area_addr(area_id);
    sp = *reinterpret_cast<const uint32_t*>(address);
    entry = *reinterpret_cast<const uint32_t*>(address + sizeof(uint32_t));
    const uint32_t entry_address = entry & ~1U;
    const uint64_t area_end = static_cast<uint64_t>(address) + area.size;

    const uint64_t ram_end =
        static_cast<uint64_t>(boot::layout::kRamBase) +
        boot::layout::kRamSize;
    return sp >= boot::layout::kRamBase && sp <= ram_end
        && (sp & 0x7U) == 0U
        && (entry & 1U) != 0U
        && entry_address >= address
        && entry_address < area_end;
}

[[noreturn]] void jump_to(boot::FlashAreaId area_id)
{
    uint32_t sp = 0U;
    uint32_t entry = 0U;
    if (!valid_vectors(area_id, sp, entry)) {
        halt();
    }
    const uint32_t address = boot::flash_area_addr(area_id);

    boot::handoff_to_image(address, sp, entry);
}

} // namespace

void main()
{
    uint8_t flag = boot::BOOT_CTRL_NORMAL;
    (void)boot::boot_ctrl_read(flag);

    // A production preloader must not execute the field-upgrade loader until
    // that loader has its own authenticated image format. The fixed loader is
    // expected to reside in a hardware write-protected partition.
#if !defined(CONFIG_BOOT_PRODUCTION)
    if (flag == boot::BOOT_CTRL_UPGRADE_LOADER) {
        uint32_t sp = 0U;
        uint32_t entry = 0U;
        if (valid_vectors(boot::FLASH_AREA_UPGRADE, sp, entry)) {
            jump_to(boot::FLASH_AREA_UPGRADE);
        }
    }
#else
    (void)flag;
    const boot::FlashArea& loader =
        boot::flash_area_get(boot::FLASH_AREA_BOOTLOADER);
    if (!boot::verify_loader_image(
            boot::flash_area_addr(boot::FLASH_AREA_BOOTLOADER),
            loader.size)) {
        halt();
    }
#endif

    jump_to(boot::FLASH_AREA_BOOTLOADER);
}

} // namespace preloader
