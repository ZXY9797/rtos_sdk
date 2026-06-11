#include <boot/flash_map.h>
#include <boot/flash_ops.h>
#include <boot/image.h>
#include <boot/protocol.h>
#include <boot/sha256.h>

#include <algorithm>
#include <cstdint>
#include <cstring>

namespace boot {

void protocol_process();

enum class UpgradeState : uint8_t {
    IDLE,
    ERASED,
    RECEIVING,
    RECEIVED,
};

static UpgradeState g_state = UpgradeState::IDLE;
static uint32_t g_rx_offset = 0;
static uint8_t g_expected_hash[32] = {};

static uint32_t target_base() {
#if defined(CONFIG_BOOT_MODE_AB)
    return flash_area_addr(FLASH_AREA_UPGRADE);
#else
    return flash_area_addr(FLASH_AREA_SLOT0);
#endif
}

static uint32_t target_size() {
#if defined(CONFIG_BOOT_MODE_AB)
    return flash_area_get(FLASH_AREA_UPGRADE).size;
#else
    return flash_area_get(FLASH_AREA_SLOT0).size;
#endif
}

bool upgrade_erase() {
    if (!flash_erase(target_base(), target_size())) {
        return false;
    }

    g_rx_offset = 0;
    g_state = UpgradeState::ERASED;
    return true;
}

uint8_t upgrade_write(uint32_t offset, const uint8_t *data, uint32_t len) {
    if (g_state != UpgradeState::ERASED &&
        g_state != UpgradeState::RECEIVING) {
        return boot_proto::ACK_ERR_STATE;
    }

    const uint32_t area_size = target_size();
    if (!data || len == 0 || offset > area_size || len > area_size - offset) {
        return boot_proto::ACK_ERR_ADDR;
    }

    if (!flash_write(target_base() + offset, data, len)) {
        return boot_proto::ACK_ERR_ERASE;
    }

    g_rx_offset = offset + len;
    g_state = UpgradeState::RECEIVING;
    return boot_proto::ACK_OK;
}

uint8_t upgrade_verify(const uint8_t expected_hash[32], uint8_t computed[32]) {
    if (!expected_hash || !computed) return boot_proto::ACK_ERR_HASH;

    Sha256Ctx ctx;
    sha256_init(ctx);

    uint8_t buf[256];
    uint32_t offset = 0;
    while (offset < g_rx_offset) {
        const uint32_t chunk = std::min<uint32_t>(
            sizeof(buf), g_rx_offset - offset);
        if (!flash_read(target_base() + offset, buf, chunk)) {
            return boot_proto::ACK_ERR_HASH;
        }
        sha256_update(ctx, buf, chunk);
        offset += chunk;
    }
    sha256_final(ctx, computed);

    if (std::memcmp(computed, expected_hash, 32) != 0) {
        return boot_proto::ACK_ERR_HASH;
    }

    g_state = UpgradeState::RECEIVED;
    std::memcpy(g_expected_hash, expected_hash, 32);
    return boot_proto::ACK_OK;
}

void upgrade_state_machine() {
    while (1) {
        protocol_process();
    }
}

UpgradeState upgrade_get_state() {
    return g_state;
}

} // namespace boot
