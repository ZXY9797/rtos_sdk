#include <boot/flash_map.h>
#include <boot/flash_ops.h>
#include <boot/image.h>

#include <algorithm>
#include <cstdint>

namespace boot {

bool flash_copy_upgrade_to_slot0() {
    const auto &src_area = flash_area_get(FLASH_AREA_UPGRADE);
    const auto &dst_area = flash_area_get(FLASH_AREA_SLOT0);
    const uint32_t sector_size = flash_erase_sector_size();

    ImageHeader hdr{};
    const uint32_t src_base = flash_area_addr(FLASH_AREA_UPGRADE);
    if (!flash_read(src_base, &hdr, sizeof(hdr))) return false;
    if (hdr.magic != IMAGE_MAGIC ||
        hdr.hdr_size != sizeof(ImageHeader) ||
        hdr.img_size == 0) {
        return false;
    }

    const uint64_t copy_len64 =
        static_cast<uint64_t>(hdr.hdr_size) + hdr.img_size;
    if (sector_size == 0 || sector_size > 4096 ||
        copy_len64 > src_area.size || copy_len64 > dst_area.size) {
        return false;
    }

    const uint32_t copy_len = static_cast<uint32_t>(copy_len64);
    const uint32_t dst_base = flash_area_addr(FLASH_AREA_SLOT0);
    static uint8_t sector[4096];

    for (uint32_t offset = 0; offset < copy_len; offset += sector_size) {
        const uint32_t chunk = std::min(sector_size, copy_len - offset);
        if (!flash_read(src_base + offset, sector, chunk)) return false;
        if (!flash_erase(dst_base + offset, sector_size)) return false;
        if (!flash_write(dst_base + offset, sector, chunk)) return false;
    }

    return true;
}

} // namespace boot
