/*
 * Copyright (c) 2025 Alexander Kozhinov <ak.alexander.kozhinov@gmail.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @brief STM32 EXTI 类型定义
 *
 * 仅保留类型/枚举定义，API 接口见 hal/exti.h
 */

#ifndef ZEPHYR_DRIVERS_INTERRUPT_CONTROLLER_INTC_EXTI_STM32_H_
#define ZEPHYR_DRIVERS_INTERRUPT_CONTROLLER_INTC_EXTI_STM32_H_

#include <stdint.h>

typedef uint8_t gpio_pin_t;

/**
 * @brief EXTI interrupt trigger type
 */
typedef enum {
    STM32_EXTI_TRIG_NONE    = 0x0,
    STM32_EXTI_TRIG_RISING  = 0x1,
    STM32_EXTI_TRIG_FALLING = 0x2,
    STM32_EXTI_TRIG_BOTH    = 0x3,
} stm32_exti_trigger_type;

/**
 * @brief EXTI line mode
 */
typedef enum {
    STM32_EXTI_MODE_IT    = 0x0,
    STM32_EXTI_MODE_EVENT = 0x1,
    STM32_EXTI_MODE_BOTH  = 0x2,
    STM32_EXTI_MODE_NONE  = 0x3,
} stm32_exti_mode;

#ifdef __cplusplus
extern "C" {
#endif

bool stm32_exti_is_pending(uint32_t line_num);
int stm32_exti_clear_pending(uint32_t line_num);
int stm32_exti_enable(uint32_t line_num, stm32_exti_trigger_type trigger, stm32_exti_mode mode);
int stm32_exti_disable(uint32_t line_num);
int stm32_exti_sw_interrupt(uint32_t line_num);
void stm32_exti_set_line_src_port(gpio_pin_t line, uint32_t port);
uint32_t stm32_exti_get_line_src_port(gpio_pin_t line);

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_DRIVERS_INTERRUPT_CONTROLLER_INTC_EXTI_STM32_H_ */
