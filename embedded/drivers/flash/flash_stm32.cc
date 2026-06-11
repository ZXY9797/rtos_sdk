#include <drivers/flash.h>
#include <cstring>

namespace hal {

struct FlashRegs {
    volatile uint32_t ACR;
    volatile uint32_t KEYR1;
    volatile uint32_t OPTKEYR;
    volatile uint32_t CR1;
    volatile uint32_t SR1;
    volatile uint32_t CCR1;
};

constexpr uint32_t FLASH_KEY1 = 0x45670123U;
constexpr uint32_t FLASH_KEY2 = 0xCDEF89ABU;

constexpr uint32_t CR1_PG      = (1U << 1);
constexpr uint32_t CR1_SER     = (1U << 2);
constexpr uint32_t CR1_PSIZE_x64 = (3U << 4);
constexpr uint32_t CR1_START   = (1U << 7);
constexpr uint32_t CR1_LOCK    = (1U << 31);

constexpr uint32_t SR1_BSY     = (1U << 0);
constexpr uint32_t SR1_WRPERR  = (1U << 4);
constexpr uint32_t SR1_PGSERR  = (1U << 5);
constexpr uint32_t SR1_OPERR   = (1U << 1);
constexpr uint32_t SR1_EOP     = (1U << 16);

constexpr uint32_t STM32_FLASH_BASE  = 0x08000000U;
constexpr uint32_t STM32_WRITE_BLOCK = 8;
constexpr uint32_t STM32_SECTOR_SIZE = 128U * 1024U;

static void wait_bsy(FlashRegs *regs) {
    while (regs->SR1 & SR1_BSY) {}
}

static Status stm32_write_block(void *ctx, uint32_t addr,
                                const void *data, size_t) {
    auto *regs = static_cast<FlashRegs *>(ctx);
    wait_bsy(regs);
    regs->CR1 = CR1_PG | CR1_PSIZE_x64;
    uint64_t word;
    std::memcpy(&word, data, 8);
    *reinterpret_cast<volatile uint64_t *>(addr) = word;
    while (regs->SR1 & SR1_BSY) {}
    if (regs->SR1 & (SR1_WRPERR | SR1_PGSERR | SR1_OPERR)) {
        regs->CCR1 = SR1_WRPERR | SR1_PGSERR | SR1_OPERR;
        return Status::HardwareError;
    }
    regs->CCR1 = SR1_EOP;
    return Status::Ok;
}

static Status stm32_erase_sector(void *ctx, uint32_t addr) {
    auto *regs = static_cast<FlashRegs *>(ctx);
    uint32_t sector =
        (addr - STM32_FLASH_BASE) / STM32_SECTOR_SIZE;
    wait_bsy(regs);
    regs->CR1 = CR1_SER | (sector << 8) | CR1_START;
    while (regs->SR1 & SR1_BSY) {}
    if (regs->SR1 & (SR1_WRPERR | SR1_OPERR)) {
        regs->CCR1 = SR1_WRPERR | SR1_OPERR;
        return Status::HardwareError;
    }
    regs->CCR1 = SR1_EOP;
    return Status::Ok;
}

static void stm32_unlock(void *ctx) {
    auto *regs = static_cast<FlashRegs *>(ctx);
    if (regs->CR1 & CR1_LOCK) {
        regs->KEYR1 = FLASH_KEY1;
        regs->KEYR1 = FLASH_KEY2;
    }
}

static void stm32_lock(void *ctx) {
    auto *regs = static_cast<FlashRegs *>(ctx);
    regs->CR1 |= CR1_LOCK;
}

Flash flash_create_default() {
    static FlashRegs regs{};
    return Flash(STM32_FLASH_BASE, STM32_WRITE_BLOCK,
                    STM32_SECTOR_SIZE, &regs,
                    stm32_write_block, stm32_erase_sector,
                    stm32_lock, stm32_unlock);
}

} // namespace hal
