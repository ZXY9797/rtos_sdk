#pragma once

#include <cstdint>

namespace hal {

/**
 * @brief GPIO 中断控制器 - 静态方法类
 *
 * 封装 GPIO 引脚中断管理 (EXTI + NVIC 联动)。
 * 实现由 MCU 特定的 .cc 文件提供。
 *
 * 用法:
 *   auto line = GpioIntc::getPinIrqLine(4, 3);
 *   GpioIntc::selectLineTrigger(line, 1);  // rising
 *   GpioIntc::setIrqCallback(line, my_cb, nullptr);
 *   GpioIntc::enableLine(line);
 */
using gpio_irq_line_t = uint32_t;
using gpio_irq_cb_t = void (*)(uint32_t, void*);

class GpioIntc {
public:
    GpioIntc() = delete;

    [[nodiscard]] static gpio_irq_line_t getPinIrqLine(uint32_t port, uint8_t pin);
    static void enableLine(gpio_irq_line_t line);
    static void disableLine(gpio_irq_line_t line);
    static void selectLineTrigger(gpio_irq_line_t line, uint32_t trg);
    [[nodiscard]] static int setIrqCallback(gpio_irq_line_t line, gpio_irq_cb_t cb, void *user);
    static void removeIrqCallback(gpio_irq_line_t line);
};

} // namespace hal
