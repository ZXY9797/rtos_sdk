#include <boot/flash_map.h>
#include <boot/image.h>

namespace boot {

// AB 模式: 逐扇区拷贝 upgrade 区 → slot0
// 断电安全: 通过 NVS 记录拷贝进度
bool flash_copy_upgrade_to_slot0() {
    const auto &src_area = flash_area_get(FLASH_AREA_UPGRADE);
    const auto &dst_area = flash_area_get(FLASH_AREA_SLOT0);
    uint32_t sector_size = 2048; // GD32F503: 2KB per sector

    uint32_t src_base = flash_area_addr(FLASH_AREA_UPGRADE);
    uint32_t dst_base = flash_area_addr(FLASH_AREA_SLOT0);
    uint32_t total = src_area.size;

    (void)dst_area;
    (void)src_base;
    (void)dst_base;
    (void)sector_size;

    for (uint32_t offset = 0; offset < total; offset += sector_size) {
        // TODO: 实现逐扇区拷贝
        // 1. 读取 src 扇区
        // 2. 擦除 dst 扇区
        // 3. 写入 dst 扇区
        // 4. 更新 NVS 进度 (offset + sector_size)
    }

    return true;
}

} // namespace boot
