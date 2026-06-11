#include <boot/flash_map.h>
#include <boot/flash_ops.h>
#include <boot/image.h>
#include <boot/product_info.h>
#include <boot/sha256.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace boot {
namespace {

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
    uint8_t buf[256];
    uint32_t done = 0;

    while (done < len) {
        const uint32_t chunk = std::min<uint32_t>(
            sizeof(buf), len - done);
        if (!flash_read(addr + done, buf, chunk)) return false;
        sha256_update(ctx, buf, chunk);
        done += chunk;
    }

    return true;
}

} // namespace

bool validate_image_header(const ImageHeader &hdr) {
    return hdr.magic == IMAGE_MAGIC
        && hdr.hdr_size == sizeof(ImageHeader)
        && hdr.img_size > 0
        && hdr.load_addr >= flash_base_addr();
}

bool verify_image_sha256(uint32_t addr, const ImageHeader &hdr) {
    if (hdr.hdr_size > UINT32_MAX - addr ||
        hdr.img_size > UINT32_MAX - addr - hdr.hdr_size) {
        return false;
    }

    Sha256Ctx ctx;
    sha256_init(ctx);
    if (!hash_flash_region(ctx, addr, 64)) return false;
    if (!hash_flash_region(ctx, addr + hdr.hdr_size, hdr.img_size)) {
        return false;
    }

    uint8_t computed[32];
    sha256_final(ctx, computed);
    return std::memcmp(computed, hdr.sha256, 32) == 0;
}

bool verify_product_info(uint32_t app_addr, uint32_t expected_product_id) {
    ProductInfo info{};
    if (!flash_read(app_addr + PRODUCT_INFO_OFFSET, &info, sizeof(info))) {
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
    if (!validate_image_header(hdr)) return false;
    if (!verify_image_sha256(addr, hdr)) return false;
    if (!verify_product_info(addr, expected_product_id)) return false;
    return true;
}

} // namespace boot
