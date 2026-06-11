#pragma once

#include <cstdint>

namespace boot {

static constexpr uint32_t IMAGE_MAGIC = 0x96f3b83d;

struct ImageVersion {
    uint8_t major;
    uint8_t minor;
    uint16_t revision;
    uint32_t build_num;
};

struct ImageHeader {
    uint32_t magic;      // IMAGE_MAGIC
    uint32_t load_addr;  // Runtime address.
    uint16_t hdr_size;   // Header size in bytes.
    uint16_t flags;      // ImageFlags.
    uint32_t img_size;   // Payload size, excluding this header.
    ImageVersion version;
    uint32_t _pad1;
    uint8_t sha256[32];     // SHA-256(first 64 B of header + payload).
    uint8_t _pad2[32];      // Reserved signature space.
    uint8_t _reserved[36];  // Padding to 128 bytes.
};
static_assert(sizeof(ImageHeader) == 128);

enum ImageFlags : uint16_t {
    IMAGE_F_CONFIRMED = 0x0001,
    IMAGE_F_PENDING = 0x0002,
    IMAGE_F_NON_BOOTABLE = 0x0004,
};

} // namespace boot
