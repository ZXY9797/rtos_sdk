#include <boot/flash_map.h>
#include <boot/flash_ops.h>
#include <boot/image.h>
#include <boot_layout.h>
#include <boot/product_info.h>
#include <boot/runtime.h>
#include <boot/sha256.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace boot {
namespace {

constexpr size_t kHashBufferSize = 256U;

static_assert(layout::kImageHeaderSize == sizeof(ImageHeader),
              "layout image header size must match ImageHeader");

uint32_t crc32_update(uint32_t crc, const uint8_t *data, size_t len) {
    crc = ~crc;
    for (size_t i = 0; i < len; ++i) {
        crc ^= data[i];
        for (int bit = 0; bit < 8; ++bit) {
            crc = (crc >> 1) ^ (0xEDB88320U & (0U - (crc & 1U)));
        }
    }
    return ~crc;
}

bool hash_flash_region(Sha256Ctx &ctx, uint32_t addr, uint32_t len) {
    uint8_t buf[kHashBufferSize];
    uint32_t done = 0;

    while (done < len) {
        const uint32_t chunk = std::min<uint32_t>(
            sizeof(buf), len - done);
        if (!flash_read(addr + done, buf, chunk)) return false;
        sha256_update(ctx, buf, chunk);
        done += chunk;
        watchdog_service();
    }

    return true;
}

uint32_t image_header_crc32(const ImageHeader &header) {
    ImageHeader canonical = header;
    canonical.flags = 0U;
    canonical.header_crc32 = 0U;
    return crc32_update(
        0U,
        reinterpret_cast<const uint8_t *>(&canonical),
        sizeof(canonical));
}

bool image_fits_area(uint32_t addr, const ImageHeader &header) {
    for (uint8_t id = 0U; id < FLASH_AREA_COUNT; ++id) {
        const auto area_id = static_cast<FlashAreaId>(id);
        if (flash_area_addr(area_id) != addr) {
            continue;
        }
        const uint64_t image_size =
            static_cast<uint64_t>(header.hdr_size) + header.img_size;
        return image_size <= flash_area_get(area_id).size;
    }
    return false;
}

bool validate_vector_table(uint32_t addr, const ImageHeader &header) {
    uint32_t vectors[2]{};
    if (!flash_read(addr + header.hdr_size, vectors, sizeof(vectors))) {
        return false;
    }

    const uint32_t stack_pointer = vectors[0];
    const uint32_t reset_handler = vectors[1];
    const uint32_t entry_addr = reset_handler & ~1U;
    const uint64_t payload_end =
        static_cast<uint64_t>(header.load_addr) + header.img_size;

    const uint64_t ram_end =
        static_cast<uint64_t>(layout::kRamBase) + layout::kRamSize;
    return stack_pointer >= layout::kRamBase && stack_pointer <= ram_end &&
        (stack_pointer & 0x7U) == 0U &&
        (reset_handler & 1U) != 0U &&
        entry_addr >= header.load_addr && entry_addr < payload_end;
}

} // namespace

#if !defined(CONFIG_BOOT_PRODUCTION)
__attribute__((weak))
uint32_t minimum_security_version() {
#if defined(CONFIG_BOOT_MIN_SECURITY_VERSION)
    return CONFIG_BOOT_MIN_SECURITY_VERSION;
#else
    return 0U;
#endif
}

__attribute__((weak))
bool commit_security_version(uint32_t) {
    return true;
}

__attribute__((weak))
bool verify_image_signature(const uint8_t[32], const uint8_t[64]) {
    return false;
}
#endif

void authentication_digest(const ImageHeader &header, uint8_t digest[32]) {
    ImageHeader canonical = header;
    canonical.flags = 0U;
    std::memset(canonical.signature, 0, sizeof(canonical.signature));
    canonical.header_crc32 = 0U;
    Sha256Ctx ctx;
    sha256_init(ctx);
    sha256_update(ctx,
                  reinterpret_cast<const uint8_t *>(&canonical),
                  sizeof(canonical));
    sha256_final(ctx, digest);
}

bool validate_image_header(uint32_t addr, const ImageHeader &hdr) {
    const uint32_t slot0_payload =
        flash_area_addr(FLASH_AREA_SLOT0) + sizeof(ImageHeader);
    const bool valid_flags = hdr.flags == IMAGE_F_CONFIRMED
        || hdr.flags == IMAGE_F_PENDING;
    return hdr.magic == IMAGE_MAGIC
        && hdr.hdr_size == sizeof(ImageHeader)
        && hdr.img_size > 0
        && hdr.load_addr == slot0_payload
        && valid_flags
        && hdr.security_version >= minimum_security_version()
        && hdr.header_crc32 == image_header_crc32(hdr)
        && image_fits_area(addr, hdr);
}

bool verify_image_sha256(uint32_t addr, const ImageHeader &hdr) {
    if (hdr.hdr_size > UINT32_MAX - addr ||
        hdr.img_size > UINT32_MAX - addr - hdr.hdr_size) {
        return false;
    }

    Sha256Ctx ctx;
    sha256_init(ctx);
    if (!hash_flash_region(ctx, addr + hdr.hdr_size, hdr.img_size)) {
        return false;
    }

    uint8_t computed[32];
    sha256_final(ctx, computed);
    return std::memcmp(computed, hdr.sha256, 32) == 0;
}

bool verify_product_info(uint32_t app_addr, uint32_t expected_product_id) {
    ProductInfo info{};
    if (!flash_read(app_addr + layout::kProductInfoOffset,
                    &info, sizeof(info))) {
        return false;
    }

    if (info.magic != PRODUCT_INFO_MAGIC) return false;

    const uint32_t computed = crc32_update(
        0, reinterpret_cast<const uint8_t *>(&info), 48);
    if (computed != info.crc32) return false;

    if (expected_product_id != 0 &&
        info.product_id != expected_product_id) {
        return false;
    }

    return true;
}

bool check_image(uint32_t addr, uint32_t expected_product_id) {
    ImageHeader hdr{};
    if (!flash_read(addr, &hdr, sizeof(hdr))) return false;
    if (!validate_image_header(addr, hdr)) return false;
    if (!verify_image_sha256(addr, hdr)) return false;
#if defined(CONFIG_BOOT_REQUIRE_SIGNATURE)
    uint8_t auth_digest[32] {};
    authentication_digest(hdr, auth_digest);
    if (!verify_image_signature(auth_digest, hdr.signature)) return false;
#endif
    if (!validate_vector_table(addr, hdr)) return false;
    if (!verify_product_info(addr, expected_product_id)) return false;
    return true;
}

} // namespace boot
