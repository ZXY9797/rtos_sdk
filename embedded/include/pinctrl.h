#pragma once

#include <drivers/status.h>
#include <cstddef>
#include <cstdint>

namespace hal {
namespace pinctrl {

enum class OperationType : uint8_t {
    Rmw = 0,
    Write = 1,
};

/**
 * @brief DTS 生成的通用 MMIO 操作。
 *
 * SoC 差异由 dt-bindings/pinctrl 下的宏展开为 MMIO 操作流；
 * 运行时只按操作类型执行寄存器读改写或直接写。
 */
struct Operation {
    OperationType type;
    uintptr_t address;
    uint32_t clear_mask;
    uint32_t set_value;
};

/**
 * @brief 批量应用一组 pinctrl 操作。
 */
Status apply(const Operation *ops, std::size_t count);

} // namespace pinctrl
} // namespace hal
