#include <boot/flash_ops.h>
#include <boot/image.h>

#include <cstdint>

extern "C" uint32_t __rom_region_start;

namespace boot {

void confirm_image() {
    const uint32_t image_addr = reinterpret_cast<uint32_t>(&__rom_region_start);

    ImageHeader copy{};
    if (!flash_read(image_addr, &copy, sizeof(copy))) return;
    if (copy.magic != IMAGE_MAGIC) return;
    if (copy.flags & IMAGE_F_CONFIRMED) return;

    copy.flags = static_cast<uint16_t>(copy.flags | IMAGE_F_CONFIRMED);
    (void)flash_update(image_addr, &copy, sizeof(copy));
}

} // namespace boot
