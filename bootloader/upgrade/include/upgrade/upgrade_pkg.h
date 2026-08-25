#pragma once

#include <cstdint>

namespace upgrade {

static constexpr uint32_t LOADER_PAYLOAD_MAGIC = 0x5052444CU; // "LDRP"
static constexpr uint16_t LOADER_PAYLOAD_VERSION = 1U;

#pragma pack(push, 1)
struct LoaderPayloadHeader {
    uint32_t magic;
    uint16_t header_size;
    uint16_t version;
    uint32_t payload_size;
    uint8_t sha256[32];
    uint32_t reserved;
};
#pragma pack(pop)

static_assert(sizeof(LoaderPayloadHeader) == 48U,
              "Loader payload manifest ABI changed");

struct LoaderPayload {
    const uint8_t *blob;
    uint32_t blob_size;
};

extern "C" {
extern const uint8_t g_loader_payload[];
extern const uint32_t g_loader_payload_size;
}

inline LoaderPayload get_loader_payload() {
    return {g_loader_payload, g_loader_payload_size};
}

} // namespace upgrade
