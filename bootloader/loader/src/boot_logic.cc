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

static uint32_t completed_swap_length()
{
    uint32_t progress = 0U;
    uint32_t length = 0U;
    BootSwapPhase phase = BOOT_SWAP_SAVE_OLD;
    return boot_swap_state_read(progress, phase, length)
        && progress == length && phase == BOOT_SWAP_SAVE_OLD
        ? length : 0U;
}

enum class InstallState : uint8_t {
    NewImage,
    ResumeSwap,
};

static bool swap_state_prepared()
{
    uint32_t progress = 0U;
    uint32_t length = 0U;
    BootSwapPhase phase = BOOT_SWAP_SAVE_OLD;
    return boot_swap_state_read(progress, phase, length)
        && length > 0U && progress <= length
        && phase <= BOOT_SWAP_STORE_OLD;
}

static bool image_has_flags(uint32_t address, uint16_t expected_flags)
{
    ImageHeader header {};
    return read_image_header(address, header)
        && header.magic == IMAGE_MAGIC
        && header.flags == expected_flags;
}

static bool install_staged_image(uint32_t slot0_addr, uint32_t staging_addr,
                                 InstallState state)
{
    if ((state == InstallState::NewImage
         && (!check_image(staging_addr, get_product_id())
             || !image_has_flags(staging_addr, IMAGE_F_PENDING)))
        || !flash_copy_upgrade_to_slot0()
        || !check_image(slot0_addr, get_product_id())
        || !boot_trial_begin(completed_swap_length())) {
        return false;
    }
    return true;
}

static bool rollback_trial_image(uint32_t slot0_addr, uint32_t staging_addr)
{
    if (!flash_copy_upgrade_to_slot0()
        || !check_image(slot0_addr, get_product_id())
        || !invalidate_staging(staging_addr)
        || !boot_ctrl_clear()) {
        return false;
    }
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

    if (flag == BOOT_CTRL_ROLLBACK_APP) {
        if (rollback_trial_image(slot0_addr, upg_addr)) {
            jump_to_app(slot0_addr);
        }
        enter_dfu();
        return;
    }

    if (flag == BOOT_CTRL_TRIAL_APP) {
        if (check_image(slot0_addr, get_product_id())
            && image_has_flags(slot0_addr, IMAGE_F_CONFIRMED)) {
            if (accept_confirmed_security_version(slot0_addr)
                && boot_ctrl_clear()) {
                (void)invalidate_staging(upg_addr);
                jump_to_app(slot0_addr);
            }
            enter_dfu();
            return;
        }

        if (boot_rollback_begin()
            && rollback_trial_image(slot0_addr, upg_addr)) {
            jump_to_app(slot0_addr);
        }
        enter_dfu();
        return;
    }

    const bool slot0_valid = check_image(slot0_addr, get_product_id());
    const bool staging_valid = check_image(upg_addr, get_product_id());
    const bool install_requested = flag == BOOT_CTRL_UPGRADE_APP;
    const bool resume_swap = install_requested && swap_state_prepared();
    const bool recover_staging = staging_valid
        && image_has_flags(upg_addr, IMAGE_F_PENDING)
        && (!slot0_valid || staging_is_newer(slot0_addr, upg_addr));
    if (install_requested || recover_staging) {
        if (!resume_swap && !staging_valid) {
            enter_dfu();
            return;
        }
        if (!install_requested
            && !boot_ctrl_write(BOOT_CTRL_UPGRADE_APP)) {
            enter_dfu();
            return;
        }
        const InstallState state = resume_swap
            ? InstallState::ResumeSwap : InstallState::NewImage;
        if (install_staged_image(slot0_addr, upg_addr, state)) {
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
#if defined(CONFIG_BOOT_MODE_STAGED_COPY)
        && image_has_flags(slot0_addr, IMAGE_F_CONFIRMED)
#endif
        && accept_confirmed_security_version(slot0_addr)) {
        jump_to_app(slot0_addr);
    }

    enter_dfu();
}

} // namespace boot
