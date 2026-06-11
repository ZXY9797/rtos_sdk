#include <boot/image.h>

#include <cstdint>

extern "C" uint32_t __rom_region_start;

namespace boot {

void confirm_image() {
    auto *hdr = reinterpret_cast<ImageHeader *>(&__rom_region_start);
    if (hdr->magic != IMAGE_MAGIC) return;
    if (hdr->flags & IMAGE_F_CONFIRMED) return;

    // 待办：应用自写闪存能力接通后，通过闪存驱动完成确认写回。
    // 1. 将镜像头读到 RAM。
    // 2. 设置 IMAGE_F_CONFIRMED 标志。
    // 3. 擦除并重写镜像头所在扇区。
}

} // 命名空间 boot
