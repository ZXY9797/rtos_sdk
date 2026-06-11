#include <boot/image.h>
#include <boot/flash_map.h>
#include <cstring>

// NVS loader_upgrade 标志存储在 storage 区固定偏移处
// 简化实现: 直接读写 flash 标志 (不依赖 NVS mount)
static constexpr uint32_t UPGRADE_FLAG_MAGIC  = 0x55504752; // "UPGR"

struct UpgradeFlag {
    uint32_t magic;
    uint32_t flag;   // 1 = loader upgrade pending
};

namespace preloader {

// 检查 loader_upgrade 标志
static bool is_loader_upgrade_pending() {
    auto *flag = reinterpret_cast<const UpgradeFlag *>(
        boot::flash_area_addr(boot::FLASH_AREA_STORAGE));
    return flag->magic == UPGRADE_FLAG_MAGIC && flag->flag == 1;
}

static void clear_upgrade_flag() {
    // TODO: flash erase + write 清除标志
}

// 将 upgrade 区拷贝到 slot0，然后清除标志
bool check_and_do_loader_upgrade() {
    if (!is_loader_upgrade_pending()) return false;

    uint32_t src_addr = boot::flash_area_addr(boot::FLASH_AREA_UPGRADE);
    uint32_t dst_addr = boot::flash_area_addr(boot::FLASH_AREA_SLOT0);
    uint32_t size = boot::flash_area_get(boot::FLASH_AREA_UPGRADE).size;

    // 校验 upgrade 区镜像
    auto *hdr = reinterpret_cast<const boot::ImageHeader *>(src_addr);
    if (hdr->magic != boot::IMAGE_MAGIC) {
        clear_upgrade_flag();
        return false;
    }

    // 逐扇区拷贝
    uint32_t sector_size = 2048;
    for (uint32_t offset = 0; offset < size; offset += sector_size) {
        // TODO: 实际 flash 操作
        // 1. erase dst sector
        // 2. copy src sector to dst
    }

    // 清除标志
    clear_upgrade_flag();

    // 跳转到 slot0 (升级 app)
    uint32_t sp = *reinterpret_cast<uint32_t *>(dst_addr);
    uint32_t entry = *reinterpret_cast<uint32_t *>(dst_addr + 4);
    asm volatile("cpsid i" ::: "memory");
    asm volatile("msr msp, %0" ::"r"(sp) : "memory");
    *reinterpret_cast<volatile uint32_t *>(0xE000ED08) = dst_addr;
    reinterpret_cast<void(*)()>(entry)();
    while (1) {}

    return true;
}

} // namespace preloader
