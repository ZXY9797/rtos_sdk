/**
 * @brief ExtiController STM32 实现
 *
 * 桥接 C++ 接口与 STM32 EXTI C 函数。
 */

#include <drivers/exti.h>
#include <drivers/interrupt_controller/intc_exti_stm32.h>

namespace hal {

bool ExtiController::isPending(uint32_t lineNum) {
    return stm32_exti_is_pending(lineNum);
}

int ExtiController::clearPending(uint32_t lineNum) {
    return stm32_exti_clear_pending(lineNum);
}

void ExtiController::setLineSrcPort(uint8_t line, uint32_t port) {
    stm32_exti_set_line_src_port(line, port);
}

uint32_t ExtiController::getLineSrcPort(uint8_t line) {
    return stm32_exti_get_line_src_port(line);
}

int ExtiController::enable(uint32_t lineNum, uint32_t trigger, uint32_t mode) {
    return stm32_exti_enable(lineNum,
                             static_cast<stm32_exti_trigger_type>(trigger),
                             static_cast<stm32_exti_mode>(mode));
}

int ExtiController::disable(uint32_t lineNum) {
    return stm32_exti_disable(lineNum);
}

int ExtiController::swInterrupt(uint32_t lineNum) {
    return stm32_exti_sw_interrupt(lineNum);
}

} // namespace hal
