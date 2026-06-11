#include <boot/boot_ctrl.h>
#include <boot/flash_map.h>
#include <boot/image.h>
#include <boot/product_info.h>

#include <cstdint>

namespace boot {

bool check_image(uint32_t addr, uint32_t expected_product_id);
void jump_to_app(uint32_t app_addr);
void upgrade_state_machine();
bool flash_copy_upgrade_to_slot0();

__attribute__((weak))
uint32_t get_product_id() {
#if defined(CONFIG_SOC_SERIES_GR5525X)
    return PRODUCT_ID_DEMO_BLE;
#else
    return PRODUCT_ID_DEMO;
#endif
}

static void enter_dfu() {
    upgrade_state_machine();
}

void boot_logic() {
    uint8_t flag = BOOT_CTRL_NORMAL;
    (void)boot_ctrl_read(flag);

    if (flag == BOOT_CTRL_ENTER_DFU) {
        (void)boot_ctrl_clear();
        enter_dfu();
        return;
    }

    if (flag == BOOT_CTRL_UPGRADE_LOADER) {
        const uint32_t slot0_addr = flash_area_addr(FLASH_AREA_SLOT0);
        if (check_image(slot0_addr, 0)) {
            jump_to_app(slot0_addr);
        }

        (void)boot_ctrl_clear();
        (void)loader_upgrade_clear();
        enter_dfu();
        return;
    }

    const uint32_t slot0_addr = flash_area_addr(FLASH_AREA_SLOT0);
    if (check_image(slot0_addr, get_product_id())) {
        jump_to_app(slot0_addr);
    }

#if defined(CONFIG_BOOT_MODE_AB)
    const uint32_t upg_addr = flash_area_addr(FLASH_AREA_UPGRADE);
    if (check_image(upg_addr, get_product_id())) {
        if (flash_copy_upgrade_to_slot0() &&
            check_image(slot0_addr, get_product_id())) {
            jump_to_app(slot0_addr);
        }
    }
#endif

    enter_dfu();
}

} // namespace boot
