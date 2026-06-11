#pragma once

#include <drivers/status.h>
#include <cstddef>
#include <cstdint>

namespace hal {
namespace pinctrl {

enum class PinMode : uint8_t {
    Input,
    Output,
    Alternate,
    Analog,
};

enum class Pull : uint8_t {
    None,
    Up,
    Down,
};

enum class Drive : uint8_t {
    PushPull,
    OpenDrain,
};

enum class OutputState : uint8_t {
    Unchanged,
    Low,
    High,
};

/**
 * @brief DTS 生成的引脚配置项。
 *
 * pinmux 的编码由 SoC dt-binding 定义，公共生成器只负责搬运配置，
 * 具体端口、引脚和复用功能的解码由各芯片 pinctrl 驱动完成。
 */
struct PinConfig {
    uint32_t pinmux;
    PinMode mode;
    Pull pull;
    Drive drive;
    OutputState output;
    uint8_t slew_rate;
};

/**
 * @brief 批量应用一组引脚配置。
 */
Status apply(const PinConfig *pins, std::size_t count);

} // namespace pinctrl
} // namespace hal
