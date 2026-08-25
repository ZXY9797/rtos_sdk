#include <boot/handoff.h>
#include <boot/flash_ops.h>
#include <boot/image.h>
#include <boot/runtime.h>
#include <boot_layout.h>

#include <cstdint>

namespace boot {
namespace {

[[noreturn]] void halt() {
    while (true) {
        asm volatile("wfi");
    }
}

bool valid_vectors(const ImageHeader &header, const uint32_t vectors[2]) {
    const uint32_t stack_pointer = vectors[0];
    const uint32_t reset_handler = vectors[1];
    const uint32_t entry_addr = reset_handler & ~1U;
    const uint64_t payload_end =
        static_cast<uint64_t>(header.load_addr) + header.img_size;
    const uint64_t ram_end =
        static_cast<uint64_t>(layout::kRamBase) + layout::kRamSize;

    return header.img_size >= sizeof(uint32_t) * 2U
        && stack_pointer >= layout::kRamBase
        && stack_pointer <= ram_end
        && (stack_pointer & 0x7U) == 0U
        && (reset_handler & 1U) != 0U
        && entry_addr >= header.load_addr
        && entry_addr < payload_end;
}

} // namespace

[[noreturn]] void jump_to_app(uint32_t image_addr) {
    ImageHeader header{};
    uint32_t vectors[2] {};
    if (!flash_read(image_addr, &header, sizeof(header)) ||
        header.magic != IMAGE_MAGIC ||
        header.hdr_size != sizeof(ImageHeader) ||
        !flash_read(header.load_addr, vectors, sizeof(vectors)) ||
        !valid_vectors(header, vectors)) {
        halt();
    }

    if (!transport_shutdown() || !watchdog_prepare_handoff()) {
        halt();
    }

    handoff_to_image(header.load_addr, vectors[0], vectors[1]);
}

} // namespace boot
