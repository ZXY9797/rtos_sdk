#ifndef EMBEDDED_INCLUDE_DT_BINDINGS_PINCTRL_GD32_PINCTRL_H_
#define EMBEDDED_INCLUDE_DT_BINDINGS_PINCTRL_GD32_PINCTRL_H_

#include <dt-bindings/pinctrl/pinctrl-common.h>

#define GD32_PORT_A 0U
#define GD32_PORT_B 1U
#define GD32_PORT_C 2U
#define GD32_PORT_D 3U
#define GD32_PORT_E 4U
#define GD32_PORT_F 5U
#define GD32_PORT_G 6U
#define GD32_PORT_H 7U
#define GD32_PORT_I 8U

#define GD32_PULL_NONE 0U
#define GD32_PULL_UP   1U
#define GD32_PULL_DOWN 2U

#define GD32_DRIVE_PUSH_PULL  0U
#define GD32_DRIVE_OPEN_DRAIN 1U

#define GD32_PERIPH_BASE 0x40000000U
#define GD32_AHB1_BASE   (GD32_PERIPH_BASE + 0x00020000U)
#define GD32_RCU_BASE    (GD32_AHB1_BASE + 0x00001000U)
#define GD32_GPIO_BASE   (GD32_AHB1_BASE + 0x00020000U)

#define GD32_RCU_AHBEN_ADDR      (GD32_RCU_BASE + 0x14U)
#define GD32_GPIO_PORT_BASE(port) (GD32_GPIO_BASE + ((port) * 0x400U))
#define GD32_GPIO_CTL_ADDR(port)  (GD32_GPIO_PORT_BASE(port) + 0x00U)
#define GD32_GPIO_OMODE_ADDR(port) (GD32_GPIO_PORT_BASE(port) + 0x04U)
#define GD32_GPIO_OSPD_ADDR(port) (GD32_GPIO_PORT_BASE(port) + 0x08U)
#define GD32_GPIO_PUD_ADDR(port)  (GD32_GPIO_PORT_BASE(port) + 0x0CU)
#define GD32_GPIO_BOP_ADDR(port)  (GD32_GPIO_PORT_BASE(port) + 0x18U)
#define GD32_GPIO_AFSEL_ADDR(port, pin) \
    (GD32_GPIO_PORT_BASE(port) + 0x20U + (((pin) / 8U) * 4U))

#define GD32_GPIO_PIN_MASK(pin) (1U << (pin))
#define GD32_GPIO_MODE_SHIFT(pin) ((pin) * 2U)
#define GD32_GPIO_AF_SHIFT(pin) (((pin) & 0x7U) * 4U)

#define GD32_GPIO_MODE_INPUT     0U
#define GD32_GPIO_MODE_OUTPUT    1U
#define GD32_GPIO_MODE_ALTERNATE 2U
#define GD32_GPIO_MODE_ANALOG    3U

#define GD32_PINCTRL_CLOCK(port) \
    PINCTRL_OP(PINCTRL_OP_RMW, GD32_RCU_AHBEN_ADDR, 0U, (1U << (port)))

#define GD32_PINCTRL_MODE(port, pin, mode) \
    PINCTRL_OP(PINCTRL_OP_RMW, GD32_GPIO_CTL_ADDR(port), \
        (0x3U << GD32_GPIO_MODE_SHIFT(pin)), \
        (((mode) & 0x3U) << GD32_GPIO_MODE_SHIFT(pin)))

#define GD32_PINCTRL_PULL(port, pin, pull) \
    PINCTRL_OP(PINCTRL_OP_RMW, GD32_GPIO_PUD_ADDR(port), \
        (0x3U << GD32_GPIO_MODE_SHIFT(pin)), \
        (((pull) & 0x3U) << GD32_GPIO_MODE_SHIFT(pin)))

#define GD32_PINCTRL_DRIVE(port, pin, drive) \
    PINCTRL_OP(PINCTRL_OP_RMW, GD32_GPIO_OMODE_ADDR(port), \
        GD32_GPIO_PIN_MASK(pin), \
        (((drive) & 0x1U) << (pin)))

#define GD32_PINCTRL_SPEED(port, pin, speed) \
    PINCTRL_OP(PINCTRL_OP_RMW, GD32_GPIO_OSPD_ADDR(port), \
        (0x3U << GD32_GPIO_MODE_SHIFT(pin)), \
        (((speed) & 0x3U) << GD32_GPIO_MODE_SHIFT(pin)))

#define GD32_PINCTRL_AF(port, pin, af) \
    PINCTRL_OP(PINCTRL_OP_RMW, GD32_GPIO_AFSEL_ADDR(port, pin), \
        (0xFU << GD32_GPIO_AF_SHIFT(pin)), \
        (((af) & 0xFU) << GD32_GPIO_AF_SHIFT(pin)))

#define GD32_PINCTRL_OUTPUT_LOW(port, pin) \
    PINCTRL_OP(PINCTRL_OP_WRITE, GD32_GPIO_BOP_ADDR(port), 0U, (1U << ((pin) + 16U)))

#define GD32_PINCTRL_OUTPUT_HIGH(port, pin) \
    PINCTRL_OP(PINCTRL_OP_WRITE, GD32_GPIO_BOP_ADDR(port), 0U, GD32_GPIO_PIN_MASK(pin))

#define GD32_PINMUX_AF(port, pin, af, pull, drive, speed) \
    GD32_PINCTRL_CLOCK(port) \
    GD32_PINCTRL_MODE(port, pin, GD32_GPIO_MODE_ALTERNATE) \
    GD32_PINCTRL_PULL(port, pin, pull) \
    GD32_PINCTRL_DRIVE(port, pin, drive) \
    GD32_PINCTRL_SPEED(port, pin, speed) \
    GD32_PINCTRL_AF(port, pin, af)

#define GD32_PINMUX_INPUT(port, pin, pull, speed) \
    GD32_PINCTRL_CLOCK(port) \
    GD32_PINCTRL_MODE(port, pin, GD32_GPIO_MODE_INPUT) \
    GD32_PINCTRL_PULL(port, pin, pull) \
    GD32_PINCTRL_SPEED(port, pin, speed)

#define GD32_PINMUX_OUTPUT_LOW(port, pin, pull, drive, speed) \
    GD32_PINCTRL_CLOCK(port) \
    GD32_PINCTRL_OUTPUT_LOW(port, pin) \
    GD32_PINCTRL_MODE(port, pin, GD32_GPIO_MODE_OUTPUT) \
    GD32_PINCTRL_PULL(port, pin, pull) \
    GD32_PINCTRL_DRIVE(port, pin, drive) \
    GD32_PINCTRL_SPEED(port, pin, speed)

#define GD32_PINMUX_OUTPUT_HIGH(port, pin, pull, drive, speed) \
    GD32_PINCTRL_CLOCK(port) \
    GD32_PINCTRL_OUTPUT_HIGH(port, pin) \
    GD32_PINCTRL_MODE(port, pin, GD32_GPIO_MODE_OUTPUT) \
    GD32_PINCTRL_PULL(port, pin, pull) \
    GD32_PINCTRL_DRIVE(port, pin, drive) \
    GD32_PINCTRL_SPEED(port, pin, speed)

#define GD32_PINMUX_ANALOG(port, pin) \
    GD32_PINCTRL_CLOCK(port) \
    GD32_PINCTRL_MODE(port, pin, GD32_GPIO_MODE_ANALOG) \
    GD32_PINCTRL_PULL(port, pin, GD32_PULL_NONE)

#endif /* EMBEDDED_INCLUDE_DT_BINDINGS_PINCTRL_GD32_PINCTRL_H_ */
