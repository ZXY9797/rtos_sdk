#include <boot/image.h>
#include <boot/product_info.h>
#include <boot/flash_map.h>
#include <boot/boot_ctrl.h>

namespace boot {

// 由 image_check.cc 实现
bool check_image(uint32_t addr, uint32_t expected_product_id);

// 由 jump_app.cc 实现
void jump_to_app(uint32_t app_addr);

// 由 upgrade_state.cc 实现
void upgrade_state_machine();

// 产品 ID (由 product 层覆盖)
__attribute__((weak))
uint32_t get_product_id() { return PRODUCT_ID_DEMO; }

static void enter_dfu() {
    upgrade_state_machine();
}

void boot_logic() {
    uint8_t flag = BOOT_CTRL_NORMAL;
    boot_ctrl_read(flag);

    // 检查是否请求进入 DFU
    if (flag == BOOT_CTRL_ENTER_DFU) {
        boot_ctrl_clear();
        enter_dfu();
        return;
    }

    // 检查 loader_upgrade 标志 (bootloader 自升级)
    if (flag == BOOT_CTRL_UPGRADE_LOADER) {
        // 跳转到 slot0 (upgrade app 已由 preloader 拷贝到 slot0)
        uint32_t slot0_addr = flash_area_addr(FLASH_AREA_SLOT0);
        if (check_image(slot0_addr, 0)) {
            jump_to_app(slot0_addr);
        }
        // 校验失败 → 清除标志，进入 DFU
        boot_ctrl_clear();
        enter_dfu();
        return;
    }

    // 校验 slot0 镜像
    uint32_t slot0_addr = flash_area_addr(FLASH_AREA_SLOT0);
    if (check_image(slot0_addr, get_product_id())) {
        jump_to_app(slot0_addr);
    }

    // slot0 无效，AB 模式尝试从 upgrade 区恢复
#if defined(CONFIG_BOOT_MODE_AB)
    {
        uint32_t upg_addr = flash_area_addr(FLASH_AREA_UPGRADE);
        if (check_image(upg_addr, get_product_id())) {
            // TODO: 拷贝 upgrade → slot0
            // flash_copy_upgrade_to_slot0();
            // if (check_image(slot0_addr, get_product_id())) {
            //     jump_to_app(slot0_addr);
            // }
        }
    }
#endif

    // 所有尝试失败，进入 DFU
    enter_dfu();
}

} // namespace boot
