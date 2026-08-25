#include <boot/flash_ops.h>
#include <boot/image.h>

#include <cstddef>
#include <cstdint>

extern "C" uint32_t __image_header_start;

namespace boot {

bool confirm_image() {
    const uint32_t image_addr =
        reinterpret_cast<uint32_t>(&__image_header_start);

    ImageHeader copy{};
    if (!flash_read(image_addr, &copy, sizeof(copy))) return false;
    if (copy.magic != IMAGE_MAGIC) return false;
    if (copy.flags == IMAGE_F_CONFIRMED) return true;
    if (copy.flags != IMAGE_F_PENDING) return false;

    const uint16_t confirmed = IMAGE_F_CONFIRMED;
    if (!flash_write(image_addr + offsetof(ImageHeader, flags),
                     &confirmed, sizeof(confirmed))) {
        return false;
    }
    uint16_t readback = 0U;
    return flash_read(image_addr + offsetof(ImageHeader, flags),
                      &readback, sizeof(readback))
        && readback == confirmed;
}

} // namespace boot
