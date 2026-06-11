#include <boot/image.h>
#include <boot/flash_map.h>
#include <boot/sha256.h>
#include <cstring>

namespace preloader {

// 由 upgrade_copy.cc 实现
bool check_and_do_loader_upgrade();

static bool verify_boot_image(uint32_t addr) {
    auto *hdr = reinterpret_cast<const boot::ImageHeader *>(addr);
    if (hdr->magic != boot::IMAGE_MAGIC) return false;
    if (hdr->hdr_size != sizeof(boot::ImageHeader)) return false;
    if (hdr->img_size == 0) return false;

    // SHA-256 校验
    boot::Sha256Ctx ctx;
    boot::sha256_init(ctx);
    boot::sha256_update(ctx, reinterpret_cast<const uint8_t *>(addr), 64);
    boot::sha256_update(ctx,
        reinterpret_cast<const uint8_t *>(addr + hdr->hdr_size),
        hdr->img_size);
    uint8_t hash[32];
    boot::sha256_final(ctx, hash);
    return memcmp(hash, hdr->sha256, 32) == 0;
}

static void jump_to(uint32_t addr) {
    uint32_t sp = *reinterpret_cast<uint32_t *>(addr);
    uint32_t entry = *reinterpret_cast<uint32_t *>(addr + 4);

    // 禁中断、设 MSP、重定向向量表、跳转
    asm volatile("cpsid i" ::: "memory");
    asm volatile("msr msp, %0" ::"r"(sp) : "memory");
    *reinterpret_cast<volatile uint32_t *>(0xE000ED08) = addr; // SCB->VTOR
    reinterpret_cast<void(*)()>(entry)();
    while (1) {}
}

void main() {
    // 1. 检查是否需要 loader 自升级
    if (check_and_do_loader_upgrade()) {
        // 升级 app 已拷贝到 slot0 并跳转，不会返回这里
    }

    // 2. 校验 bootloader 区
    uint32_t boot_addr = boot::flash_area_addr(boot::FLASH_AREA_BOOTLOADER);
    if (verify_boot_image(boot_addr)) {
        jump_to(boot_addr);
    }

    // 3. boot 区损坏，死循环 (可通过 JTAG 恢复)
    while (1) {
        // TODO: blink LED 指示错误
    }
}

} // namespace preloader
