#include <boot/handoff.h>

#include <cstdint>

namespace boot {
namespace {

constexpr uint32_t kSysTickControl = 0xE000E010U;
constexpr uint32_t kInterruptControlState = 0xE000ED04U;
constexpr uint32_t kVectorTableOffset = 0xE000ED08U;
constexpr uint32_t kNvicDisableBase = 0xE000E180U;
constexpr uint32_t kNvicClearPendingBase = 0xE000E280U;
constexpr uint32_t kNvicBankCount = 8U;
constexpr uint32_t kClearPendSv = 1U << 27U;
constexpr uint32_t kClearSysTick = 1U << 25U;

[[noreturn]] __attribute__((naked))
void branch_to_image(uint32_t, uint32_t) {
    asm volatile(
        "movs r2, #0\n"
        "msr control, r2\n"
        "msr basepri, r2\n"
        "msr faultmask, r2\n"
        "msr msp, r0\n"
        "isb\n"
        "cpsie i\n"
        "bx r1\n");
}

} // namespace

[[noreturn]] void handoff_to_image(uint32_t vector_addr,
                                   uint32_t stack_pointer,
                                   uint32_t reset_handler) {
    asm volatile("cpsid i" ::: "memory");
    *reinterpret_cast<volatile uint32_t *>(kSysTickControl) = 0U;

    for (uint32_t bank = 0U; bank < kNvicBankCount; ++bank) {
        const uint32_t offset = bank * sizeof(uint32_t);
        *reinterpret_cast<volatile uint32_t *>(
            kNvicDisableBase + offset) = UINT32_MAX;
        *reinterpret_cast<volatile uint32_t *>(
            kNvicClearPendingBase + offset) = UINT32_MAX;
    }

    *reinterpret_cast<volatile uint32_t *>(kInterruptControlState) =
        kClearPendSv | kClearSysTick;
    *reinterpret_cast<volatile uint32_t *>(kVectorTableOffset) = vector_addr;
    asm volatile("dsb 0xF" ::: "memory");
    asm volatile("isb 0xF" ::: "memory");
    branch_to_image(stack_pointer, reset_handler);
}

} // namespace boot
