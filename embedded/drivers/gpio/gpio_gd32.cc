#include <drivers/gpio.h>
#include "gd32_regs.h"

namespace hal {
namespace {

constexpr uint32_t CTL_MODE_OUTPUT = 0x1U;
constexpr uint32_t CTL_MODE_INPUT  = 0x0U;
constexpr uint32_t CTL_MODE_ANALOG = 0x3U;
constexpr uint32_t PUD_PULLUP      = 0x1U;

void gpio_clock_enable(uintptr_t base) {
    uint32_t offset = (base - GPIO_BASE) / 0x400U;
    if (offset <= 8) {
        gd32::rcu_ahben() |= (1U << offset);
    }
}

} // anonymous namespace

int GpioPortBase::configure(int pin, uint32_t flags) {
    auto *regs = reinterpret_cast<gd32::GpioRegs *>(base_);
    gpio_clock_enable(base_);

    uint32_t mask = pin_mask(pin);
    uint32_t shift = static_cast<uint32_t>(pin) * 2U;

    regs->CTL &= ~(0x3U << shift);
    if (flags & GPIO_OUTPUT) {
        regs->CTL |= (CTL_MODE_OUTPUT << shift);
        regs->OMODE &= ~mask;
        regs->OSPD |= (0x3U << shift);
    } else if (flags & GPIO_INPUT) {
        regs->CTL |= (CTL_MODE_INPUT << shift);
    } else {
        regs->CTL |= (CTL_MODE_ANALOG << shift);
    }

    regs->PUD &= ~(0x3U << shift);
    if (flags & GPIO_PULL_UP)        regs->PUD |= (PUD_PULLUP << shift);
    else if (flags & GPIO_PULL_DOWN) regs->PUD |= (0x2U << shift);

    if (flags & GPIO_OUTPUT_HIGH) regs->BOP = mask;
    else if (flags & GPIO_OUTPUT_LOW) regs->BOP = mask << 16U;

    return 0;
}

void GpioPortBase::set(int pin) {
    reinterpret_cast<gd32::GpioRegs *>(base_)->BOP = pin_mask(pin);
}

void GpioPortBase::clear(int pin) {
    reinterpret_cast<gd32::GpioRegs *>(base_)->BOP = pin_mask(pin) << 16U;
}

void GpioPortBase::toggle(int pin) {
    reinterpret_cast<gd32::GpioRegs *>(base_)->OCTL ^= pin_mask(pin);
}

int GpioPortBase::get(int pin) const {
    return (reinterpret_cast<const gd32::GpioRegs *>(base_)->ISTAT & pin_mask(pin)) ? 1 : 0;
}

} // namespace hal
