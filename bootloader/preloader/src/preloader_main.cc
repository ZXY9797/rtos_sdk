#include <boot/flash_map.h>
#include <boot/flash_ops.h>
#include <boot/image.h>
#include <boot/sha256.h>

#include <algorithm>
#include <cstdint>
#include <cstring>

namespace preloader {

bool check_and_do_loader_upgrade();

static bool hash_flash_region(boot::Sha256Ctx &ctx,
                              uint32_t addr, uint32_t len) {
    uint8_t buf[256];
    uint32_t done = 0;

    while (done < len) {
        const uint32_t chunk = std::min<uint32_t>(
            sizeof(buf), len - done);
        if (!boot::flash_read(addr + done, buf, chunk)) return false;
        boot::sha256_update(ctx, buf, chunk);
        done += chunk;
    }

    return true;
}

static bool verify_boot_image(uint32_t addr) {
    boot::ImageHeader hdr{};
    if (!boot::flash_read(addr, &hdr, sizeof(hdr))) return false;
    if (hdr.magic != boot::IMAGE_MAGIC) return false;
    if (hdr.hdr_size != sizeof(boot::ImageHeader)) return false;
    if (hdr.img_size == 0) return false;
    if (hdr.hdr_size > UINT32_MAX - addr ||
        hdr.img_size > UINT32_MAX - addr - hdr.hdr_size) {
        return false;
    }

    boot::Sha256Ctx ctx;
    boot::sha256_init(ctx);
    if (!hash_flash_region(ctx, addr, 64)) return false;
    if (!hash_flash_region(ctx, addr + hdr.hdr_size, hdr.img_size)) {
        return false;
    }

    uint8_t hash[32];
    boot::sha256_final(ctx, hash);
    return std::memcmp(hash, hdr.sha256, 32) == 0;
}

[[noreturn]] static void jump_to(uint32_t addr) {
    const uint32_t sp = *reinterpret_cast<uint32_t *>(addr);
    const uint32_t entry = *reinterpret_cast<uint32_t *>(addr + 4);

    asm volatile("cpsid i" ::: "memory");
    asm volatile("msr msp, %0" ::"r"(sp) : "memory");
    *reinterpret_cast<volatile uint32_t *>(0xE000ED08U) = addr;
    reinterpret_cast<void (*)()>(entry)();
    while (1) {}
}

[[noreturn]] static void halt() {
    while (1) {
        asm volatile("wfi");
    }
}

void main() {
    (void)check_and_do_loader_upgrade();

    const uint32_t boot_addr =
        boot::flash_area_addr(boot::FLASH_AREA_BOOTLOADER);
    if (verify_boot_image(boot_addr)) {
        jump_to(boot_addr);
    }

    halt();
}

} // namespace preloader
