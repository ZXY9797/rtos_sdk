#include <boot/boot_ctrl.h>
#include <boot/flash_map.h>
#include <boot/flash_ops.h>
#include <boot/image.h>
#include <boot/product_info.h>
#include <boot_layout.h>

#include <cstddef>
#include <cstdint>

namespace boot {

[[noreturn]] void jump_to_app(uint32_t image_addr);
void upgrade_state_machine();
bool flash_copy_upgrade_to_slot0();

__attribute__((weak))
uint32_t get_product_id() {
    return layout::kProductId;
}

static void enter_dfu() {
    upgrade_state_machine();
}

bool accept_confirmed_security_version(uint32_t image_addr)
{
    ImageHeader header {};
    if (!flash_read(image_addr, &header, sizeof(header))) {
        return false;
    }
    return header.flags != IMAGE_F_CONFIRMED
        || commit_security_version(header.security_version);
}

static bool read_image_header(uint32_t addr, ImageHeader &header)
{
    return flash_read(addr, &header, sizeof(header));
}

static bool staging_is_newer(uint32_t slot0_addr, uint32_t staging_addr)
{
    ImageHeader staging {};
    if (!read_image_header(staging_addr, staging)) {
        return false;
    }
    ImageHeader slot0 {};
    return !read_image_header(slot0_addr, slot0)
        || staging.security_version > slot0.security_version;
}

static bool invalidate_staging(uint32_t staging_addr)
{
    const uint16_t flags = IMAGE_F_NON_BOOTABLE;
    return flash_write(
        staging_addr + offsetof(ImageHeader, flags), &flags, sizeof(flags));
}

static bool install_staged_image(uint32_t slot0_addr, uint32_t staging_addr)
{
    if (!check_image(staging_addr, get_product_id())
        || !flash_copy_upgrade_to_slot0()
        || !check_image(slot0_addr, get_product_id())
        || !accept_confirmed_security_version(slot0_addr)
        || !boot_ctrl_clear()) {
        return false;
    }
    (void)invalidate_staging(staging_addr);
    return true;
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
        (void)boot_ctrl_clear();
        (void)loader_upgrade_clear();
        enter_dfu();
        return;
    }

    const uint32_t slot0_addr = flash_area_addr(FLASH_AREA_SLOT0);
#if defined(CONFIG_BOOT_MODE_STAGED_COPY)
    const uint32_t upg_addr = flash_area_addr(FLASH_AREA_UPGRADE);
    const bool slot0_valid = check_image(slot0_addr, get_product_id());
    const bool staging_valid = check_image(upg_addr, get_product_id());
    const bool install_requested = flag == BOOT_CTRL_UPGRADE_APP;
    const bool recover_staging = staging_valid
        && (!slot0_valid || staging_is_newer(slot0_addr, upg_addr));
    if (install_requested || recover_staging) {
        if (!staging_valid) {
            enter_dfu();
            return;
        }
        if (install_staged_image(slot0_addr, upg_addr)) {
            jump_to_app(slot0_addr);
        }
        enter_dfu();
        return;
    }
#else
    if (flag == BOOT_CTRL_UPGRADE_APP) {
        if (!check_image(slot0_addr, get_product_id())
            || !accept_confirmed_security_version(slot0_addr)
            || !boot_ctrl_clear()) {
            enter_dfu();
            return;
        }
        jump_to_app(slot0_addr);
    }
#endif

    if (check_image(slot0_addr, get_product_id())
        && accept_confirmed_security_version(slot0_addr)) {
        jump_to_app(slot0_addr);
    }

    enter_dfu();
}

} // namespace boot
