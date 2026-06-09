#include <drivers/gpio.h>
#include <arch/arch_interface.h>
#include <irq.h>

namespace hal {
namespace {

/*
 * GR5525 GPIO register layout (CMSDK / PL061 style)
 * Matches gpio_regs_t in gr5525_a0.h
 */
struct Gr5525GpioRegs {
    volatile uint32_t DATA;         /* 0x000 — read: pin state; write: masked by addr[9:2] */
    volatile uint32_t DATAOUT;      /* 0x004 — read: output latch; write: masked by addr[9:2] */
    volatile uint32_t RESERVED0[2]; /* 0x008 */
    volatile uint32_t OUTENSET;     /* 0x010 — write 1 to set output enable */
    volatile uint32_t OUTENCLR;     /* 0x014 — write 1 to clear output enable (input) */
};

} // anonymous namespace

int GpioPortBase::configure(int pin, uint32_t flags) {
    if (pin < 0 || pin > 31) return -1;
    auto *regs = reinterpret_cast<Gr5525GpioRegs *>(base_);
    uint32_t mask = pin_mask(pin);

    if (flags & GPIO_OUTPUT) {
        regs->OUTENSET = mask;
        unsigned int key = arch_irq_lock();
        if (flags & GPIO_OUTPUT_HIGH) {
            regs->DATAOUT |= mask;
        } else if (flags & GPIO_OUTPUT_LOW) {
            regs->DATAOUT &= ~mask;
        }
        arch_irq_unlock(key);
    } else if (flags & GPIO_INPUT) {
        regs->OUTENCLR = mask;
    }

    return 0;
}

void GpioPortBase::set(int pin) {
    auto *regs = reinterpret_cast<Gr5525GpioRegs *>(base_);
    unsigned int key = arch_irq_lock();
    regs->DATAOUT |= pin_mask(pin);
    arch_irq_unlock(key);
}

void GpioPortBase::clear(int pin) {
    auto *regs = reinterpret_cast<Gr5525GpioRegs *>(base_);
    unsigned int key = arch_irq_lock();
    regs->DATAOUT &= ~pin_mask(pin);
    arch_irq_unlock(key);
}

void GpioPortBase::toggle(int pin) {
    auto *regs = reinterpret_cast<Gr5525GpioRegs *>(base_);
    unsigned int key = arch_irq_lock();
    regs->DATAOUT ^= pin_mask(pin);
    arch_irq_unlock(key);
}

int GpioPortBase::get(int pin) const {
    auto *regs = reinterpret_cast<const Gr5525GpioRegs *>(base_);
    return (regs->DATA & pin_mask(pin)) ? 1 : 0;
}

} // namespace hal
