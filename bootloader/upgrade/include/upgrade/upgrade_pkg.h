#pragma once

#include <cstdint>

namespace upgrade {

struct LoaderPayload {
    const uint8_t *data;
    uint32_t       size;
};

extern "C" {
    extern const uint8_t _loader_payload_start[];
    extern const uint8_t _loader_payload_size[];
}

inline LoaderPayload get_loader_payload() {
    return {
        _loader_payload_start,
        reinterpret_cast<uint32_t>(&(_loader_payload_size[0])),
    };
}

} // namespace upgrade
