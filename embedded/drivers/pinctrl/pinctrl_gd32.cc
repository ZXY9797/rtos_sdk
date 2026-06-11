#include <pinctrl.h>

#include "gd32_regs.h"

namespace hal {
namespace pinctrl {
namespace {

constexpr uint32_t GPIO_PORT_STRIDE = 0x400U;
constexpr uint32_t GPIO_MAX_PORT = 8U;
constexpr uint32_t GPIO_MAX_PIN = 15U;

constexpr uint32_t PINMUX_PORT_SHIFT = 8U;
constexpr uint32_t PINMUX_PIN_SHIFT = 4U;
constexpr uint32_t PINMUX_AF_MASK = 0x0FU;

constexpr uint32_t CTL_MODE_INPUT = 0x0U;
constexpr uint32_t CTL_MODE_OUTPUT = 0x1U;
constexpr uint32_t CTL_MODE_ALTERNATE = 0x2U;
constexpr uint32_t CTL_MODE_ANALOG = 0x3U;

constexpr uint32_t PUD_NONE = 0x0U;
constexpr uint32_t PUD_PULLUP = 0x1U;
constexpr uint32_t PUD_PULLDOWN = 0x2U;

void gpio_clock_enable(uint32_t port) {
    if (port <= GPIO_MAX_PORT) {
        gd32::rcu_ahben() |= (1U << port);
    }
}

uint32_t ctl_mode(PinMode mode) {
    switch (mode) {
        case PinMode::Input:     return CTL_MODE_INPUT;
        case PinMode::Output:    return CTL_MODE_OUTPUT;
        case PinMode::Alternate: return CTL_MODE_ALTERNATE;
        case PinMode::Analog:    return CTL_MODE_ANALOG;
    }
    return CTL_MODE_ANALOG;
}

uint32_t pud_mode(Pull pull) {
    switch (pull) {
        case Pull::None: return PUD_NONE;
        case Pull::Up:   return PUD_PULLUP;
        case Pull::Down: return PUD_PULLDOWN;
    }
    return PUD_NONE;
}

Status apply_one(const PinConfig &cfg) {
    uint32_t port = (cfg.pinmux >> PINMUX_PORT_SHIFT) & 0xFFU;
    uint32_t pin = (cfg.pinmux >> PINMUX_PIN_SHIFT) & 0x0FU;
    uint32_t af = cfg.pinmux & PINMUX_AF_MASK;
    if (port > GPIO_MAX_PORT || pin > GPIO_MAX_PIN) {
        return Status::InvalidArgument;
    }

    gpio_clock_enable(port);

    auto *regs = reinterpret_cast<gd32::GpioRegs *>(
        GPIO_BASE + port * GPIO_PORT_STRIDE);
    uint32_t mask = 1U << pin;
    uint32_t shift = pin * 2U;

    if (cfg.output == OutputState::High) {
        regs->BOP = mask;
    } else if (cfg.output == OutputState::Low) {
        regs->BOP = mask << 16U;
    }

    regs->CTL &= ~(0x3U << shift);
    regs->CTL |= ctl_mode(cfg.mode) << shift;

    regs->PUD &= ~(0x3U << shift);
    regs->PUD |= pud_mode(cfg.pull) << shift;

    regs->OMODE &= ~mask;
    if (cfg.drive == Drive::OpenDrain) {
        regs->OMODE |= mask;
    }

    regs->OSPD &= ~(0x3U << shift);
    regs->OSPD |= (static_cast<uint32_t>(cfg.slew_rate) & 0x3U) << shift;

    if (cfg.mode == PinMode::Alternate) {
        volatile uint32_t &afsel = (pin < 8U) ? regs->AFSEL0 : regs->AFSEL1;
        uint32_t af_shift = (pin & 0x7U) * 4U;
        afsel &= ~(0xFU << af_shift);
        afsel |= af << af_shift;
    }

    return Status::Ok;
}

} // namespace

Status apply(const PinConfig *pins, std::size_t count) {
    if (!pins && count != 0U) {
        return Status::InvalidArgument;
    }

    for (std::size_t i = 0; i < count; ++i) {
        Status status = apply_one(pins[i]);
        if (status != Status::Ok) {
            return status;
        }
    }

    return Status::Ok;
}

} // namespace pinctrl
} // namespace hal
