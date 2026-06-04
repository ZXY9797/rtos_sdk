/**
 * @brief GpioIntc STM32 实现
 *
 * 桥接 C++ 接口与 STM32 GPIO INTC C 函数。
 */

#include <drivers/gpio_intc.h>
#include <drivers/interrupt_controller/gpio_intc_stm32.h>

namespace hal {

gpio_irq_line_t GpioIntc::getPinIrqLine(uint32_t port, uint8_t pin) {
    return stm32_gpio_intc_get_pin_irq_line(port, pin);
}

void GpioIntc::enableLine(gpio_irq_line_t line) {
    stm32_gpio_intc_enable_line(line);
}

void GpioIntc::disableLine(gpio_irq_line_t line) {
    stm32_gpio_intc_disable_line(line);
}

void GpioIntc::selectLineTrigger(gpio_irq_line_t line, uint32_t trg) {
    stm32_gpio_intc_select_line_trigger(line, trg);
}

int GpioIntc::setIrqCallback(gpio_irq_line_t line, gpio_irq_cb_t cb, void *user) {
    return stm32_gpio_intc_set_irq_callback(line, cb, user);
}

void GpioIntc::removeIrqCallback(gpio_irq_line_t line) {
    stm32_gpio_intc_remove_irq_callback(line);
}

} // namespace hal
