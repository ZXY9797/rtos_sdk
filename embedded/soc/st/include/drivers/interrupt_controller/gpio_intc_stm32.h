/*
 * Copyright (c) 2016 Open-RnD Sp. z o.o.
 * Copyright (c) 2024 STMicroelectronics
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @brief STM32 GPIO 中断控制器类型定义
 *
 * 仅保留类型/枚举定义，API 接口见 hal/gpio_intc.h
 */

#ifndef ZEPHYR_DRIVERS_INTERRUPT_CONTROLLER_GPIO_INTC_STM32_H_
#define ZEPHYR_DRIVERS_INTERRUPT_CONTROLLER_GPIO_INTC_STM32_H_

#include <stdint.h>

typedef uint8_t gpio_pin_t;
typedef uint32_t gpio_port_pins_t;

/**
 * @brief Opaque type representing a GPIO interrupt line
 */
typedef uint32_t stm32_gpio_irq_line_t;

/**
 * @brief GPIO interrupt trigger flags
 */
enum stm32_gpio_irq_trigger {
    STM32_GPIO_IRQ_TRIG_NONE        = 0x0,
    STM32_GPIO_IRQ_TRIG_RISING      = 0x1,
    STM32_GPIO_IRQ_TRIG_FALLING     = 0x2,
    STM32_GPIO_IRQ_TRIG_BOTH        = 0x3,
    STM32_GPIO_IRQ_TRIG_HIGH_LEVEL  = 0x4,
    STM32_GPIO_IRQ_TRIG_LOW_LEVEL   = 0x5
};

/**
 * @brief GPIO interrupt callback function signature
 *
 * @param pin   GPIO pin on which interrupt occurred
 * @param user  data provided to setIrqCallback
 */
typedef void (*stm32_gpio_irq_cb_t)(gpio_port_pins_t pin, void *user);

#ifdef __cplusplus
extern "C" {
#endif

stm32_gpio_irq_line_t stm32_gpio_intc_get_pin_irq_line(uint32_t port, gpio_pin_t pin);
void stm32_gpio_intc_enable_line(stm32_gpio_irq_line_t line);
void stm32_gpio_intc_disable_line(stm32_gpio_irq_line_t line);
void stm32_gpio_intc_select_line_trigger(stm32_gpio_irq_line_t line, uint32_t trg);
int stm32_gpio_intc_set_irq_callback(stm32_gpio_irq_line_t line, stm32_gpio_irq_cb_t cb, void *user);
void stm32_gpio_intc_remove_irq_callback(stm32_gpio_irq_line_t line);

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_DRIVERS_INTERRUPT_CONTROLLER_GPIO_INTC_STM32_H_ */
