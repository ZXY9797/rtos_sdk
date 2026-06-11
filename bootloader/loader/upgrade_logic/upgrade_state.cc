#include <boot/protocol.h>
#include <boot/image.h>
#include <boot/flash_map.h>
#include <boot/sha256.h>
#include <cstring>

namespace boot {

// 由 protocol.cc 实现
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

bool upgrade_erase() {
#if defined(CONFIG_BOOT_MODE_AB)
    uint32_t addr = flash_area_addr(FLASH_AREA_UPGRADE);
    uint32_t size = flash_area_get(FLASH_AREA_UPGRADE).size;
#else
    uint32_t addr = flash_area_addr(FLASH_AREA_SLOT0);
    uint32_t size = flash_area_get(FLASH_AREA_SLOT0).size;
#endif
    (void)addr;
    (void)size;
    // TODO: flash erase
    g_rx_offset = 0;
    g_state = UpgradeState::ERASED;
    return true;
}

uint8_t upgrade_write(uint32_t offset, const uint8_t *data, uint32_t len) {
    if (g_state != UpgradeState::ERASED && g_state != UpgradeState::RECEIVING) {
        return boot_proto::ACK_ERR_STATE;
    }

#if defined(CONFIG_BOOT_MODE_AB)
    uint32_t base = flash_area_addr(FLASH_AREA_UPGRADE);
    uint32_t area_size = flash_area_get(FLASH_AREA_UPGRADE).size;
#else
    uint32_t base = flash_area_addr(FLASH_AREA_SLOT0);
    uint32_t area_size = flash_area_get(FLASH_AREA_SLOT0).size;
#endif

    (void)base;

    if (offset + len > area_size) {
        return boot_proto::ACK_ERR_ADDR;
    }

    // TODO: flash write
    g_rx_offset = offset + len;
    g_state = UpgradeState::RECEIVING;
    return boot_proto::ACK_OK;
}

uint8_t upgrade_verify(const uint8_t expected_hash[32]) {
    // 计算已接收数据的 SHA-256
#if defined(CONFIG_BOOT_MODE_AB)
    uint32_t base = flash_area_addr(FLASH_AREA_UPGRADE);
#else
    uint32_t base = flash_area_addr(FLASH_AREA_SLOT0);
#endif

    (void)base;

    uint8_t computed[32];
    // TODO: 对实际写入的数据计算 SHA-256
    // sha256(reinterpret_cast<const uint8_t *>(base), g_rx_offset, computed);

    if (memcmp(computed, expected_hash, 32) != 0) {
        return boot_proto::ACK_ERR_HASH;
    }

    g_state = UpgradeState::RECEIVED;
    memcpy(g_expected_hash, expected_hash, 32);
    return boot_proto::ACK_OK;
}

void upgrade_state_machine() {
    // 进入 DFU 循环：等待并处理协议帧
    while (1) {
        protocol_process();
    }
}

UpgradeState upgrade_get_state() { return g_state; }

} // namespace boot
