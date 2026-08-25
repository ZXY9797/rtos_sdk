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
    uint32_t security_version;  // Monotonic anti-rollback version.
    uint8_t sha256[32];         // SHA-256 of the payload only.
    uint8_t signature[64];      // Raw ECDSA-P256 r || s.
    uint32_t header_crc32;      // CRC-32 of canonicalized header bytes.
};
static_assert(sizeof(ImageHeader) == 128);

static constexpr uint32_t IMAGE_HEADER_SIZE = sizeof(ImageHeader);

enum ImageFlags : uint16_t {
    // State transitions only clear Flash bits: pending -> confirmed -> blocked.
    IMAGE_F_PENDING = 0xFFFE,
    IMAGE_F_CONFIRMED = 0xFFFC,
    IMAGE_F_NON_BOOTABLE = 0xFFF8,
};

/**
 * Product security provider: return the accepted anti-rollback floor.
 *
 * Production implementations must read monotonic, write-protected storage and
 * return UINT32_MAX on any read/integrity failure. The common loader only
 * provides a development fallback.
 */
uint32_t minimum_security_version();

/**
 * Product security provider: monotonically advance the accepted version.
 * Return false on storage/programming failure. Implementations must be
 * idempotent and must never lower the stored value.
 */
bool commit_security_version(uint32_t version);

/**
 * Product security provider: verify a raw ECDSA-P256 signature over the
 * common loader's canonical authentication digest. The digest binds the full
 * header after clearing mutable flags, signature and header_crc32.
 */
bool verify_image_signature(const uint8_t authentication_digest[32],
                            const uint8_t signature[64]);

/**
 * Product root-of-trust hook for the immutable preloader.
 *
 * Production implementations must authenticate the complete loader area
 * against a write-protected key, digest, or signed loader manifest.
 */
bool verify_loader_image(uint32_t address, uint32_t area_size);

void authentication_digest(const ImageHeader &header, uint8_t digest[32]);
[[nodiscard]] bool validate_image_header(uint32_t addr,
                                         const ImageHeader &header);
[[nodiscard]] bool verify_image_sha256(uint32_t addr,
                                       const ImageHeader &header);
[[nodiscard]] bool verify_product_info(uint32_t app_addr,
                                       uint32_t expected_product_id);
[[nodiscard]] bool check_image(uint32_t addr,
                               uint32_t expected_product_id);

} // namespace boot
