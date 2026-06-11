#include <boot/image.h>
#include <cstring>

// 由链接脚本定义
extern "C" uint32_t __rom_region_start;

namespace boot {

void confirm_image() {
    auto *hdr = reinterpret_cast<ImageHeader *>(&__rom_region_start);
    if (hdr->magic != IMAGE_MAGIC) return;
    if (hdr->flags & IMAGE_F_CONFIRMED) return;

    // TODO: 实际 flash 操作
    // 1. 读出 header 到 RAM
    // 2. 修改 flags |= IMAGE_F_CONFIRMED
    // 3. erase + write flash
}

} // namespace boot
