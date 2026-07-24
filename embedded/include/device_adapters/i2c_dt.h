#pragma once

#include <device.h>
#include <drivers/i2c.h>

#define HAL_I2C_DT_ADAPT(node_id)                                  \
    template <>                                                    \
    struct DeviceTrait<DT_ORD(node_id)> {                          \
        using type = I2c<DT_REG_ADDR(node_id)>;                    \
        static type instance;                                      \
    };
