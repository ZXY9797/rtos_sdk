#include <drivers/flash.h>
#include <cstring>

namespace hal {

// Flash 寄存器结构体（映射 STM32H7 Flash）
struct FlashRegs {
    volatile uint32_t ACR;        // 0x00 访问控制寄存器
    volatile uint32_t KEYR1;      // 0x04 解锁密钥寄存器 Bank 1
    volatile uint32_t OPTKEYR;    // 0x08 选项解锁密钥寄存器
    volatile uint32_t CR1;        // 0x0C 控制寄存器 Bank 1
    volatile uint32_t SR1;        // 0x10 状态寄存器 Bank 1
    volatile uint32_t CCR1;       // 0x14 清除控制寄存器 Bank 1
};

// KEYR 解锁密钥
constexpr uint32_t FLASH_KEY1 = 0x45670123U;
constexpr uint32_t FLASH_KEY2 = 0xCDEF89ABU;

// CR1 位定义
constexpr uint32_t CR1_PG      = (1U << 1);   // 编程
constexpr uint32_t CR1_SER     = (1U << 2);   // 扇区擦除
constexpr uint32_t CR1_BER     = (1U << 3);   // Bank 擦除
constexpr uint32_t CR1_PSIZE_Pos = 4;         // 编程大小位位置
constexpr uint32_t CR1_PSIZE_x64 = (3U << 4); // 64 位编程
constexpr uint32_t CR1_START   = (1U << 7);   // 开始操作
constexpr uint32_t CR1_SNB_Pos = 8;           // 扇区号位位置
constexpr uint32_t CR1_SNB_Msk = (0xFU << 8);
constexpr uint32_t CR1_LOCK    = (1U << 31);  // 锁定

// SR1 位定义
constexpr uint32_t SR1_BSY     = (1U << 0);   // 忙
constexpr uint32_t SR1_WRPERR  = (1U << 4);   // 写保护错误
constexpr uint32_t SR1_PGSERR  = (1U << 5);   // 编程序列错误
constexpr uint32_t SR1_OPERR   = (1U << 1);   // 操作错误
constexpr uint32_t SR1_EOP     = (1U << 16);  // 操作结束

static void wait_bsy(FlashRegs *regs) {
    while (regs->SR1 & SR1_BSY) {}
}

static bool unlock(FlashRegs *regs) {
    if (!(regs->CR1 & CR1_LOCK)) return true;
    regs->KEYR1 = FLASH_KEY1;
    regs->KEYR1 = FLASH_KEY2;
    return !(regs->CR1 & CR1_LOCK);
}

static void lock(FlashRegs *regs) {
    regs->CR1 |= CR1_LOCK;
}

Status FlashBase::init() {
    m_initialized = true;
    return Status::Ok;
}

Status FlashBase::deinit() {
    m_initialized = false;
    return Status::Ok;
}

Status FlashBase::read(uint32_t offset, void *data, size_t length) const {
    if (!data || length == 0) return Status::InvalidArgument;
    const auto *src = reinterpret_cast<const uint8_t *>(m_flash_base + offset);
    std::memcpy(data, src, length);
    return Status::Ok;
}

Status FlashBase::write(uint32_t offset, const void *data, size_t length) {
    if (!m_initialized || !data || length == 0) return Status::InvalidArgument;
    auto *regs = reinterpret_cast<FlashRegs *>(m_base);

    if (!unlock(regs)) return Status::HardwareError;

    uint32_t addr = m_flash_base + offset;
    const auto *src = static_cast<const uint8_t *>(data);

    size_t i = 0;
    while (i + 7 < length) {
        wait_bsy(regs);
        regs->CR1 = CR1_PG | CR1_PSIZE_x64;
        // 写入 64 位
        uint64_t word;
        std::memcpy(&word, &src[i], 8);
        *reinterpret_cast<volatile uint64_t *>(addr) = word;
        while (regs->SR1 & SR1_BSY) {}
        if (regs->SR1 & (SR1_WRPERR | SR1_PGSERR | SR1_OPERR)) {
            regs->CCR1 = SR1_WRPERR | SR1_PGSERR | SR1_OPERR;
            lock(regs);
            return Status::HardwareError;
        }
        regs->CCR1 = SR1_EOP;
        addr += 8;
        i += 8;
    }

    if (i < length) {
        uint64_t word = 0xFFFFFFFFFFFFFFFFULL;
        std::memcpy(&word, &src[i], length - i);
        wait_bsy(regs);
        regs->CR1 = CR1_PG | CR1_PSIZE_x64;
        *reinterpret_cast<volatile uint64_t *>(addr) = word;
        while (regs->SR1 & SR1_BSY) {}
        if (regs->SR1 & (SR1_WRPERR | SR1_PGSERR | SR1_OPERR)) {
            regs->CCR1 = SR1_WRPERR | SR1_PGSERR | SR1_OPERR;
            lock(regs);
            return Status::HardwareError;
        }
        regs->CCR1 = SR1_EOP;
    }

    lock(regs);
    return Status::Ok;
}

Status FlashBase::erase(uint32_t offset, size_t length) {
    if (!m_initialized) return Status::InvalidArgument;
    auto *regs = reinterpret_cast<FlashRegs *>(m_base);

    if (!unlock(regs)) return Status::HardwareError;

    uint32_t start_sector = offset / (128U * 1024U);
    uint32_t num_sectors = (length + 128U * 1024U - 1) / (128U * 1024U);

    for (uint32_t s = 0; s < num_sectors; s++) {
        wait_bsy(regs);
        regs->CR1 = CR1_SER | ((start_sector + s) << CR1_SNB_Pos) | CR1_START;
        while (regs->SR1 & SR1_BSY) {}
        if (regs->SR1 & (SR1_WRPERR | SR1_OPERR)) {
            regs->CCR1 = SR1_WRPERR | SR1_OPERR;
            lock(regs);
            return Status::HardwareError;
        }
        regs->CCR1 = SR1_EOP;
    }

    lock(regs);
    return Status::Ok;
}

// STM32H7 Flash 实例（无 cxx-driver 绑定，手动实例化）
template class Flash<0x52002000, 0x08000000>;  // Flash Bank 1
template class Flash<0x52002100, 0x08100000>;  // Flash Bank 2

} // namespace hal
