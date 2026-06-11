#include <boot/boot_ctrl.h>
#include <boot/flash_map.h>
#include <boot/flash_ops.h>
#include <boot/image.h>
#include <upgrade/upgrade_pkg.h>

#include <algorithm>
#include <cstdint>

namespace upgrade {

int flash_erase_sector(uint32_t addr, uint32_t size);
int flash_write(uint32_t addr, const uint8_t *data, uint32_t len);

static bool verify_new_loader(const uint8_t *data, uint32_t size) {
    if (!data || size < sizeof(boot::ImageHeader)) return false;

    auto *hdr = reinterpret_cast<const boot::ImageHeader *>(data);
    if (hdr->magic != boot::IMAGE_MAGIC) return false;
    if (hdr->hdr_size != sizeof(boot::ImageHeader)) return false;
    if (hdr->img_size == 0 ||
        hdr->img_size > size - sizeof(boot::ImageHeader)) {
        return false;
    }

    return true;
}

[[noreturn]] static void halt_with_led() {
    while (1) {}
}

[[noreturn]] static void system_reset() {
    auto *aircr = reinterpret_cast<volatile uint32_t *>(0xE000ED0CU);
    *aircr = 0x05FA0004U;
    while (1) {}
}

} // namespace upgrade

extern "C" int main() {
    auto payload = upgrade::get_loader_payload();

    if (!upgrade::verify_new_loader(payload.data, payload.size)) {
        upgrade::halt_with_led();
    }

    const uint32_t boot_addr =
        boot::flash_area_addr(boot::FLASH_AREA_BOOTLOADER);
    const uint32_t boot_size =
        boot::flash_area_get(boot::FLASH_AREA_BOOTLOADER).size;
    const uint32_t sector_size = boot::flash_erase_sector_size();

    if (sector_size == 0 || payload.size > boot_size) {
        upgrade::halt_with_led();
    }

    for (uint32_t offset = 0; offset < boot_size; offset += sector_size) {
        if (upgrade::flash_erase_sector(boot_addr + offset, sector_size) != 0) {
            upgrade::halt_with_led();
        }
    }

    for (uint32_t offset = 0; offset < payload.size;) {
        const uint32_t chunk =
            std::min<uint32_t>(sector_size, payload.size - offset);
        if (upgrade::flash_write(boot_addr + offset,
                                 payload.data + offset, chunk) != 0) {
            upgrade::halt_with_led();
        }
        offset += chunk;
    }

    (void)boot::loader_upgrade_clear();
    upgrade::system_reset();
}
