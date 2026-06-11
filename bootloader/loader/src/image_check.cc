#include <boot/image.h>
#include <boot/product_info.h>
#include <boot/sha256.h>
#include <cstring>

namespace boot {

bool validate_image_header(const ImageHeader &hdr) {
    return hdr.magic == IMAGE_MAGIC
        && hdr.hdr_size == sizeof(ImageHeader)
        && hdr.img_size > 0
        && hdr.load_addr >= 0x08000000;
}

bool verify_image_sha256(uint32_t addr, const ImageHeader &hdr) {
    // SHA-256 覆盖: header 前 64 字节 + payload
    Sha256Ctx ctx;
    sha256_init(ctx);

    // header 前 64 字节 (magic ~ _pad1)
    sha256_update(ctx, reinterpret_cast<const uint8_t *>(addr), 64);

    // payload
    sha256_update(ctx,
        reinterpret_cast<const uint8_t *>(addr + hdr.hdr_size),
        hdr.img_size);

    uint8_t computed[32];
    sha256_final(ctx, computed);

    return memcmp(computed, hdr.sha256, 32) == 0;
}

bool verify_product_info(uint32_t app_addr, uint32_t expected_product_id) {
    auto *info = reinterpret_cast<const ProductInfo *>(
        app_addr + PRODUCT_INFO_OFFSET);

    if (info->magic != PRODUCT_INFO_MAGIC) return false;

    // CRC-32 校验前 48 字节
    // TODO: 实现 CRC-32 校验

    if (expected_product_id != 0 &&
        info->product_id != expected_product_id) {
        return false;
    }

    return true;
}

bool check_image(uint32_t addr, uint32_t expected_product_id) {
    auto *hdr = reinterpret_cast<const ImageHeader *>(addr);
    if (!validate_image_header(*hdr)) return false;
    if (!verify_image_sha256(addr, *hdr)) return false;
    if (!verify_product_info(addr, expected_product_id)) return false;
    return true;
}

} // namespace boot
