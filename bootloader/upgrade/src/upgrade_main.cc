#include <boot/boot_ctrl.h>
#include <boot/flash_map.h>
#include <boot/flash_ops.h>
#include <boot/image.h>
#include <upgrade/upgrade_pkg.h>

#include <algorithm>
#include <cstdint>

namespace upgrade {

constexpr uint32_t kMaxCopySectorSize = 4096;

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

static bool loader_upgrade_pending() {
    uint8_t flag = 0;
    (void)boot::loader_upgrade_read(flag);
    return flag != 0;
}

static bool image_size_in_flash(boot::FlashAreaId area_id,
                                uint32_t &image_size) {
    const auto &area = boot::flash_area_get(area_id);
    const uint32_t base = boot::flash_area_addr(area_id);

    boot::ImageHeader hdr{};
    if (!boot::flash_read(base, &hdr, sizeof(hdr))) return false;
    if (hdr.magic != boot::IMAGE_MAGIC ||
        hdr.hdr_size != sizeof(boot::ImageHeader) ||
        hdr.img_size == 0) {
        return false;
    }

    const uint64_t copy_len64 =
        static_cast<uint64_t>(hdr.hdr_size) + hdr.img_size;
    if (copy_len64 > area.size || copy_len64 > UINT32_MAX) {
        return false;
    }

    image_size = static_cast<uint32_t>(copy_len64);
    return true;
}

static bool copy_upgrade_image_to_slot0() {
    const auto &src_area = boot::flash_area_get(boot::FLASH_AREA_UPGRADE);
    const auto &dst_area = boot::flash_area_get(boot::FLASH_AREA_SLOT0);
    const uint32_t sector_size = boot::flash_erase_sector_size();

    uint32_t copy_len = 0;
    if (!image_size_in_flash(boot::FLASH_AREA_UPGRADE, copy_len)) return false;
    if (sector_size == 0 || sector_size > kMaxCopySectorSize ||
        copy_len > src_area.size || copy_len > dst_area.size) {
        return false;
    }

    const uint32_t src_base = boot::flash_area_addr(boot::FLASH_AREA_UPGRADE);
    const uint32_t dst_base = boot::flash_area_addr(boot::FLASH_AREA_SLOT0);
    if (src_base > UINT32_MAX - copy_len ||
        dst_base > UINT32_MAX - copy_len) {
        return false;
    }

    static uint8_t sector[kMaxCopySectorSize];
    for (uint32_t offset = 0; offset < copy_len; offset += sector_size) {
        const uint32_t chunk =
            std::min<uint32_t>(sector_size, copy_len - offset);
        if (!boot::flash_read(src_base + offset, sector, chunk)) return false;
        if (!boot::flash_erase(dst_base + offset, sector_size)) return false;
        if (!boot::flash_write(dst_base + offset, sector, chunk)) return false;
    }

    return true;
}

} // namespace upgrade

extern "C" int main() {
    if (upgrade::loader_upgrade_pending()) {
        if (!upgrade::copy_upgrade_image_to_slot0()) {
            upgrade::halt_with_led();
        }
    }

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
