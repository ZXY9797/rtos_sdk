#pragma once

#include <cstdint>

namespace hal {

/**
 * @brief EXTI 控制器 - 静态方法类
 *
 * 封装外部中断/事件控制器。
 * 实现由 MCU 特定的 .cc 文件提供。
 *
 * 用法:
 *   ExtiController::isPending(5);
 *   ExtiController::clearPending(5);
 */
class ExtiController {
public:
    ExtiController() = delete;

    [[nodiscard]] static bool isPending(uint32_t lineNum);
    [[nodiscard]] static int clearPending(uint32_t lineNum);
    static void setLineSrcPort(uint8_t line, uint32_t port);
    [[nodiscard]] static uint32_t getLineSrcPort(uint8_t line);
    [[nodiscard]] static int enable(uint32_t lineNum, uint32_t trigger, uint32_t mode);
    [[nodiscard]] static int disable(uint32_t lineNum);
    [[nodiscard]] static int swInterrupt(uint32_t lineNum);
};

} // namespace hal
