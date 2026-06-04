/**
 * @brief GpioPortBase STM32 实现
 *
 * 通过 LL 库直接操作 GPIO 寄存器。
 * CMake 根据 CONFIG_GPIO_STM32 选择编译此文件。
 */

#include <drivers/gpio.h>
#include <stm32_ll_gpio.h>

namespace hal {

int GpioPortBase::configure(int pin, uint32_t flags) {
    auto* gpio = reinterpret_cast<GPIO_TypeDef*>(base_);
    uint32_t mask = pin_mask(pin);

    // 设置模式
    if (flags & GPIO_INPUT) {
        LL_GPIO_SetPinMode(gpio, mask, LL_GPIO_MODE_INPUT);
    } else if (flags & GPIO_OUTPUT) {
        LL_GPIO_SetPinMode(gpio, mask, LL_GPIO_MODE_OUTPUT);
    } else {
        LL_GPIO_SetPinMode(gpio, mask, LL_GPIO_MODE_ANALOG);
    }

    // 设置上下拉
    if (flags & GPIO_PULL_UP) {
        LL_GPIO_SetPinPull(gpio, mask, LL_GPIO_PULL_UP);
    } else if (flags & GPIO_PULL_DOWN) {
        LL_GPIO_SetPinPull(gpio, mask, LL_GPIO_PULL_DOWN);
    } else {
        LL_GPIO_SetPinPull(gpio, mask, LL_GPIO_PULL_NO);
    }

    // 设置输出类型
    if (flags & GPIO_OUTPUT) {
        LL_GPIO_SetPinSpeed(gpio, mask, LL_GPIO_SPEED_FREQ_HIGH);
        LL_GPIO_SetPinOutputType(gpio, mask, LL_GPIO_OUTPUT_PUSHPULL);
    }

    // 初始输出电平
    if (flags & GPIO_OUTPUT_HIGH) {
        set(pin);
    } else if (flags & GPIO_OUTPUT_LOW) {
        clear(pin);
    }

    return 0;
}

void GpioPortBase::set(int pin) {
    LL_GPIO_SetOutputPin(reinterpret_cast<GPIO_TypeDef*>(base_), pin_mask(pin));
}

void GpioPortBase::clear(int pin) {
    LL_GPIO_ResetOutputPin(reinterpret_cast<GPIO_TypeDef*>(base_), pin_mask(pin));
}

void GpioPortBase::toggle(int pin) {
    LL_GPIO_TogglePin(reinterpret_cast<GPIO_TypeDef*>(base_), pin_mask(pin));
}

int GpioPortBase::get(int pin) const {
    return LL_GPIO_IsInputPinSet(reinterpret_cast<GPIO_TypeDef*>(base_), pin_mask(pin)) ? 1 : 0;
}

} // namespace hal
