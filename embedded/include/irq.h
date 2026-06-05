#pragma once

#include <arch/arm/irq.h>

#ifdef __cplusplus
#include <cstdint>

namespace hal {

class Irq {
public:
    Irq() = delete;

    static void enable(int irq) {
        arm_irq_enable(irq);
    }

    static void disable(int irq) {
        arm_irq_disable(irq);
    }

    static void setPriority(int irq, uint32_t preempt, uint32_t sub) {
        arm_irq_priority_set(irq, preempt, sub);
    }
};

} // namespace hal
#endif
