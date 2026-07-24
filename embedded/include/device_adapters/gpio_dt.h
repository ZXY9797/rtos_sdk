#pragma once

#include <device.h>
#include <devicetree/gpio.h>
#include <drivers/gpio.h>

#define HAL_GPIO_DT_ADAPT(node_id)                                 \
    template <>                                                    \
    struct DeviceTrait<DT_ORD(node_id)> {                          \
        using type = GpioPort<                                    \
            DT_REG_ADDR(DT_GPIO_CTLR(node_id, gpios)),             \
            DT_GPIO_PIN(node_id, gpios),                           \
            DT_GPIO_FLAGS(node_id, gpios)>;                        \
        static type instance;                                      \
    };
