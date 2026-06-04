/**
 * @brief ClockCtrlBase STM32 实现
 *
 * 直接操作 RCC 寄存器使能/禁用外设时钟。
 * CMake 根据 CONFIG_CLOCK_CONTROL_STM32_CUBE 选择编译此文件。
 */

#include <drivers/clock.h>

namespace hal {

// STM32 RCC 外设时钟使能寄存器偏移
// 不同 STM32 系列的寄存器布局不同，此处以 STM32H7 为基准
static volatile uint32_t* bus_enr_reg(uintptr_t rcc_base, uint32_t bus) {
    constexpr uint32_t AHB1ENR  = 0x0D8;
    constexpr uint32_t AHB2ENR  = 0x0DC;
    constexpr uint32_t AHB3ENR  = 0x0D4;
    constexpr uint32_t AHB4ENR  = 0x0E0;
    constexpr uint32_t APB1LENR = 0x0E8;
    constexpr uint32_t APB1HENR = 0x0EC;
    constexpr uint32_t APB2ENR  = 0x0F0;
    constexpr uint32_t APB3ENR  = 0x0E4;
    constexpr uint32_t APB4ENR  = 0x0F4;

    uint32_t offset;
    switch (bus) {
    case 0:  offset = AHB1ENR;  break;
    case 1:  offset = AHB2ENR;  break;
    case 2:  offset = AHB3ENR;  break;
    case 3:  offset = AHB4ENR;  break;
    case 4:  offset = APB1LENR; break;
    case 5:  offset = APB1HENR; break;
    case 6:  offset = APB2ENR;  break;
    case 7:  offset = APB3ENR;  break;
    case 8:  offset = APB4ENR;  break;
    default: return nullptr;
    }
    return reinterpret_cast<volatile uint32_t*>(rcc_base + offset);
}

int ClockCtrlBase::enable(const ClockConfig& cfg) {
    auto* enr = bus_enr_reg(base_, cfg.bus);
    if (enr == nullptr) {
        return -EINVAL;
    }
    *enr |= cfg.enr;
    return 0;
}

int ClockCtrlBase::disable(const ClockConfig& cfg) {
    auto* enr = bus_enr_reg(base_, cfg.bus);
    if (enr == nullptr) {
        return -EINVAL;
    }
    *enr &= ~cfg.enr;
    return 0;
}

} // namespace hal
