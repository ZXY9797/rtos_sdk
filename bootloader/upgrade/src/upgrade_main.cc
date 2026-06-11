#include <boot/image.h>
#include <boot/flash_map.h>
#include <upgrade/upgrade_pkg.h>
#include <cstring>

namespace upgrade {

static bool verify_new_loader(const uint8_t *data, uint32_t size) {
    if (size < sizeof(boot::ImageHeader)) return false;

    auto *hdr = reinterpret_cast<const boot::ImageHeader *>(data);
    if (hdr->magic != boot::IMAGE_MAGIC) return false;
    if (hdr->hdr_size != 128) return false;
    if (hdr->img_size == 0 || hdr->img_size > size - sizeof(boot::ImageHeader)) return false;

    return true;
}

static void halt_with_led() {
    while (1) {}
}

} // namespace upgrade

extern "C" int main() {
    auto payload = upgrade::get_loader_payload();

    if (!upgrade::verify_new_loader(payload.data, payload.size)) {
        upgrade::halt_with_led();
    }

    uint32_t boot_addr = boot::flash_area_addr(boot::FLASH_AREA_BOOTLOADER);
    uint32_t boot_size = boot::flash_area_get(boot::FLASH_AREA_BOOTLOADER).size;
    (void)boot_addr;

    if (payload.size > boot_size) {
        upgrade::halt_with_led();
    }

    // TODO: 擦除 boot 区，逐扇区写入新 loader
    uint32_t sector_size = 2048;
    for (uint32_t offset = 0; offset < boot_size; offset += sector_size) {
        // flash_erase + flash_write
    }

    // TODO: 清除 NVS loader_upgrade 标志
    // NVIC_SystemReset();
    while (1) {}
    return 0;
}
