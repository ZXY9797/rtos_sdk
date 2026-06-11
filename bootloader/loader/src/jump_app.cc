#include <cstdint>

namespace boot {

using AppEntry = void(*)();

void jump_to_app(uint32_t app_addr) {
    uint32_t sp   = *reinterpret_cast<uint32_t *>(app_addr);
    uint32_t entry = *reinterpret_cast<uint32_t *>(app_addr + 4);

    asm volatile("cpsid i" ::: "memory");
    asm volatile("msr msp, %0" ::"r"(sp) : "memory");
    *reinterpret_cast<volatile uint32_t *>(0xE000ED08) = app_addr; // SCB->VTOR

    reinterpret_cast<AppEntry>(entry)();
    while (1) {}
}

} // namespace boot
