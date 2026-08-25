#include <boot/boot_ctrl.h>
#include <boot/flash_map.h>
#include <boot/flash_ops.h>
#include <boot/sha256.h>
#include <boot_layout.h>
#include <upgrade/upgrade_pkg.h>

#include <algorithm>
#include <cstdint>
#include <cstring>

namespace upgrade {
namespace {

struct VerifiedLoader {
    const uint8_t *data;
    uint32_t size;
    uint8_t digest[32];
};

[[noreturn]] void halt() {
    asm volatile("cpsid i" ::: "memory");
    while (1) {
    }
}

[[noreturn]] void system_reset() {
    auto *aircr = reinterpret_cast<volatile uint32_t *>(0xE000ED0CU);
    *aircr = 0x05FA0004U;
    while (1) {
    }
}

bool valid_loader_vectors(const uint8_t *data, uint32_t size,
                          uint32_t destination) {
    if (data == nullptr || size < 2U * sizeof(uint32_t)) return false;

    uint32_t stack_pointer = 0U;
    uint32_t entry = 0U;
    std::memcpy(&stack_pointer, data, sizeof(stack_pointer));
    std::memcpy(&entry, data + sizeof(stack_pointer), sizeof(entry));
    const uint64_t ram_end = static_cast<uint64_t>(boot::layout::kRamBase)
        + boot::layout::kRamSize;
    const uint64_t image_end = static_cast<uint64_t>(destination) + size;
    const uint32_t entry_address = entry & ~1U;
    return stack_pointer >= boot::layout::kRamBase
        && stack_pointer <= ram_end
        && (stack_pointer & 0x7U) == 0U
        && (entry & 1U) != 0U
        && entry_address >= destination
        && entry_address < image_end;
}

bool verify_manifest(const LoaderPayload &payload,
                     uint32_t destination, VerifiedLoader &verified) {
    if (payload.blob == nullptr
        || payload.blob_size < sizeof(LoaderPayloadHeader)) {
        return false;
    }

    LoaderPayloadHeader header {};
    std::memcpy(&header, payload.blob, sizeof(header));
    if (header.magic != LOADER_PAYLOAD_MAGIC
        || header.header_size != sizeof(header)
        || header.version != LOADER_PAYLOAD_VERSION
        || header.payload_size == 0U
        || header.payload_size
            != payload.blob_size - sizeof(LoaderPayloadHeader)) {
        return false;
    }

    const uint8_t *data = payload.blob + sizeof(LoaderPayloadHeader);
    if (!valid_loader_vectors(data, header.payload_size, destination)) {
        return false;
    }

    uint8_t digest[32];
    boot::sha256(data, header.payload_size, digest);
    if (std::memcmp(digest, header.sha256, sizeof(digest)) != 0) {
        return false;
    }

    verified.data = data;
    verified.size = header.payload_size;
    std::memcpy(verified.digest, digest, sizeof(verified.digest));
    return true;
}

bool verify_written_loader(uint32_t address, uint32_t size,
                           const uint8_t expected[32]) {
    boot::Sha256Ctx context {};
    boot::sha256_init(context);
    uint8_t buffer[256];
    uint32_t offset = 0U;
    while (offset < size) {
        const uint32_t chunk = std::min<uint32_t>(
            sizeof(buffer), size - offset);
        if (!boot::flash_read(address + offset, buffer, chunk)) return false;
        boot::sha256_update(context, buffer, chunk);
        offset += chunk;
    }

    uint8_t digest[32];
    boot::sha256_final(context, digest);
    return std::memcmp(digest, expected, sizeof(digest)) == 0;
}

bool install_loader(const VerifiedLoader &payload) {
    const uint32_t address =
        boot::flash_area_addr(boot::FLASH_AREA_BOOTLOADER);
    const uint32_t capacity =
        boot::flash_area_get(boot::FLASH_AREA_BOOTLOADER).size;
    const uint32_t sector_size = boot::flash_erase_sector_size();
    if (sector_size == 0U || payload.size > capacity
        || (capacity % sector_size) != 0U) {
        return false;
    }

    if (!boot::flash_erase(address, capacity)) return false;
    for (uint32_t offset = 0U; offset < payload.size;) {
        const uint32_t chunk = std::min<uint32_t>(
            sector_size, payload.size - offset);
        if (!boot::flash_write(address + offset,
                               payload.data + offset, chunk)) {
            return false;
        }
        offset += chunk;
    }
    return verify_written_loader(
        address, payload.size, payload.digest);
}

} // namespace
} // namespace upgrade

extern "C" int main() {
    uint8_t pending = 0U;
    if (!boot::loader_upgrade_read(pending) || pending == 0U) {
        upgrade::halt();
    }

    const upgrade::LoaderPayload blob = upgrade::get_loader_payload();
    upgrade::VerifiedLoader payload {};
    const uint32_t destination =
        boot::flash_area_addr(boot::FLASH_AREA_BOOTLOADER);
    if (!upgrade::verify_manifest(blob, destination, payload)
        || !upgrade::install_loader(payload)
        || !boot::loader_upgrade_clear()) {
        upgrade::halt();
    }
    upgrade::system_reset();
}
