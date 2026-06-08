#pragma once

#include <arch/arm/irq.h>
#include <arch/cpu.h>

#ifdef __cplusplus
#include <cstdint>

/// 生成 extern "C" void IRQn_Handler()，直接覆盖向量表弱别名。
/// 零 RAM 开销，硬件直接跳转。
#define HAL_ISR_CONNECT(irq_n, type, obj_p)                                    \
    extern "C" void IRQ##irq_n##_Handler(void) {                               \
        static_cast<type *>(const_cast<void *>(                                \
            static_cast<const void *>(obj_p)))                                  \
            ->isr_handler();                                                   \
    }

namespace hal {

class Irq {
public:
    Irq() = delete;

    static void enable(int irq) {
        arm_irq_enable(static_cast<unsigned int>(irq));
    }

    static void disable(int irq) {
        arm_irq_disable(static_cast<unsigned int>(irq));
    }

    [[nodiscard]] static bool isEnabled(int irq) {
        return arm_irq_is_enabled(static_cast<unsigned int>(irq)) != 0;
    }

    static void setPriority(int irq, unsigned int prio, uint32_t flags = 0) {
        arm_irq_priority_set(static_cast<unsigned int>(irq), prio, flags);
    }
};

class IrqGuard {
public:
    IrqGuard() : m_key(arch_irq_lock()) {}
    ~IrqGuard() { arch_irq_unlock(m_key); }

    IrqGuard(const IrqGuard &) = delete;
    IrqGuard &operator=(const IrqGuard &) = delete;
    IrqGuard(IrqGuard &&) = delete;
    IrqGuard &operator=(IrqGuard &&) = delete;

    [[nodiscard]] bool was_unlocked() const {
        return arch_irq_unlocked(m_key);
    }

private:
    unsigned int m_key;
};

} // namespace hal
#endif
