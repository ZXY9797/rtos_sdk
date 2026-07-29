#pragma once

#include <device.h>
#include <drivers/pwm.h>

#define HAL_GD32_PWM_DT_ADAPT(node_id)                             \
    template <>                                                    \
    struct DeviceTrait<DT_ORD(node_id)> {                          \
        static constexpr uint32_t kChannel =                       \
            DT_PROP_OR(node_id, channel, 0U);                      \
                                                                   \
        static_assert(kChannel <= 3U, "PWM channel is invalid");   \
                                                                   \
        using type = Pwm<                                         \
            DT_REG_ADDR(node_id),                                 \
            static_cast<PwmChannel>(kChannel),                     \
            DT_IRQN(node_id)>;                                    \
        static type instance;                                      \
                                                                   \
        static int init()                                          \
        {                                                          \
            PwmConfig config{};                                    \
            config.prescaler =                                    \
                DT_PROP_OR(node_id, prescaler, 0U);                \
            config.period = DT_PROP_OR(node_id, period, 0U);       \
            return static_cast<int>(instance.init(config));        \
        }                                                          \
                                                                   \
        static void isr_update(osal::IsrContext& context)          \
        {                                                          \
            instance.isr_handler(context);                         \
        }                                                          \
    };

#define HAL_STM32_PWM_DT_ADAPT(node_id)                            \
    template <>                                                    \
    struct DeviceTrait<DT_ORD(node_id)> {                          \
        using type = Pwm<DT_REG_ADDR(DT_PARENT(node_id))>;         \
        static type instance;                                      \
    };
