#include <boot/boot_ctrl.h>
#include <boot/flash_map.h>
#include <boot/flash_ops.h>
#include <boot/image.h>
#include <boot/protocol.h>
#include <boot/runtime.h>
#include <boot/sha256.h>
#include <osal.h>

#include <algorithm>
#include <cstdint>
#include <cstring>

namespace boot {

uint32_t get_product_id();

enum class UpgradeState : uint8_t {
    IDLE,
    ERASED,
    RECEIVING,
    RECEIVED,
    FAILED,
};

static UpgradeState g_state = UpgradeState::IDLE;
static uint32_t g_rx_offset = 0;
static uint32_t g_expected_size = 0;

constexpr osal::Milliseconds kIdleSleepMs = 1U;

static uint32_t target_base() {
#if defined(CONFIG_BOOT_MODE_STAGED_COPY)
    return flash_area_addr(FLASH_AREA_UPGRADE);
#else
    return flash_area_addr(FLASH_AREA_SLOT0);
#endif
}

static uint32_t target_size() {
#if defined(CONFIG_BOOT_MODE_STAGED_COPY)
    return flash_area_get(FLASH_AREA_UPGRADE).size;
#else
    return flash_area_get(FLASH_AREA_SLOT0).size;
#endif
}

static bool erase_target() {
    const uint32_t sector_size = flash_erase_sector_size();
    const uint32_t base = target_base();
    const uint32_t size = target_size();
    if (sector_size == 0U || (base % sector_size) != 0U
        || (size % sector_size) != 0U) {
        return false;
    }

    for (uint32_t offset = 0U; offset < size; offset += sector_size) {
        watchdog_service();
        if (!flash_erase(base + offset, sector_size)) {
            return false;
        }
        watchdog_service();
    }
    return true;
}

bool upgrade_erase() {
    g_state = UpgradeState::FAILED;
    if (!erase_target()) {
        return false;
    }

    g_rx_offset = 0;
    g_expected_size = 0;
    g_state = UpgradeState::ERASED;
    return true;
}

static bool flash_matches(uint32_t offset, const uint8_t *data,
                          uint32_t len) {
    uint8_t verify[256];
    uint32_t done = 0U;
    while (done < len) {
        const uint32_t chunk = std::min<uint32_t>(
            sizeof(verify), len - done);
        if (!flash_read(target_base() + offset + done, verify, chunk)
            || std::memcmp(verify, data + done, chunk) != 0) {
            return false;
        }
        done += chunk;
        watchdog_service();
    }
    return true;
}

static bool capture_expected_size() {
    if (g_expected_size != 0U || g_rx_offset < sizeof(ImageHeader)) {
        return true;
    }
    ImageHeader header {};
    if (!flash_read(target_base(), &header, sizeof(header))
        || !validate_image_header(target_base(), header)) {
        return false;
    }
    const uint64_t total = static_cast<uint64_t>(header.hdr_size)
        + header.img_size;
    if (total > target_size()) {
        return false;
    }
    g_expected_size = static_cast<uint32_t>(total);
    return g_rx_offset <= g_expected_size;
}

uint8_t upgrade_write(uint32_t offset, const uint8_t *data, uint32_t len) {
    if (g_state != UpgradeState::ERASED &&
        g_state != UpgradeState::RECEIVING) {
        return boot_proto::ACK_ERR_STATE;
    }

    const uint32_t area_size = target_size();
    if (!data || len == 0U || offset > area_size
        || len > area_size - offset) {
        return boot_proto::ACK_ERR_ADDR;
    }

    if (offset < g_rx_offset) {
        return offset + len <= g_rx_offset
                && flash_matches(offset, data, len)
            ? boot_proto::ACK_OK
            : boot_proto::ACK_ERR_ADDR;
    }
    if (offset != g_rx_offset
        || (g_expected_size != 0U
            && len > g_expected_size - g_rx_offset)) {
        return boot_proto::ACK_ERR_ADDR;
    }

    if (!flash_write(target_base() + offset, data, len)) {
        return boot_proto::ACK_ERR_ERASE;
    }
    watchdog_service();

    g_rx_offset = offset + len;
    if (!capture_expected_size()) {
        g_state = UpgradeState::FAILED;
        return boot_proto::ACK_ERR_MAGIC;
    }
    g_state = UpgradeState::RECEIVING;
    return boot_proto::ACK_OK;
}

uint8_t upgrade_verify(const uint8_t expected_hash[32], uint8_t computed[32]) {
    if (!expected_hash || !computed) return boot_proto::ACK_ERR_HASH;
    if (g_state != UpgradeState::RECEIVING
        || g_expected_size == 0U
        || g_rx_offset != g_expected_size) {
        return boot_proto::ACK_ERR_STATE;
    }

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
        watchdog_service();
    }
    sha256_final(ctx, computed);

    if (std::memcmp(computed, expected_hash, 32) != 0) {
        return boot_proto::ACK_ERR_HASH;
    }

    if (!check_image(target_base(), get_product_id())) {
        return boot_proto::ACK_ERR_PROD;
    }

#if defined(CONFIG_BOOT_MODE_STAGED_COPY)
    if (!boot_ctrl_write(BOOT_CTRL_UPGRADE_APP)) {
        return boot_proto::ACK_ERR_STATE;
    }
#endif

    g_state = UpgradeState::RECEIVED;
    return boot_proto::ACK_OK;
}

void upgrade_state_machine() {
    while (true) {
        watchdog_service();
        if (!protocol_process()) {
            osal::this_thread::sleep_for(kIdleSleepMs);
        }
    }
}

UpgradeState upgrade_get_state() {
    return g_state;
}

} // namespace boot
