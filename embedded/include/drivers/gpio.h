#pragma once

#include <dt-bindings/gpio/gpio.h>
#include <cstdint>

namespace hal {

/**
 * @brief GPIO 端口基类 — 非模板，纯声明
 *
 * 实现由 MCU 特定的 .cc 文件提供。
 * 使用 uintptr_t 存储基地址，头文件无 MCU 类型依赖。
 */
class GpioPortBase {
public:
    explicit GpioPortBase(uintptr_t base) : base_(base) {}

    [[nodiscard]] int configure(int pin, uint32_t flags);
    void set(int pin);
    void clear(int pin);
    void toggle(int pin);
    [[nodiscard]] int get(int pin) const;

protected:
    uintptr_t base_;
    static constexpr uint32_t pin_mask(int pin) { return 1U << pin; }
};

/**
 * @brief 编译期绑定的 GPIO 引脚 — 值参数模板
 *
 * 基地址、引脚号、标志位均为编译期常量，由 DeviceTrait 特化传入。
 * 自动处理 ACTIVE_HIGH / ACTIVE_LOW 电平。
 *
 * 用法 (通常通过 Device<> 间接使用):
 *   using Led = Device<DT_ORD(DT_ALIAS(led0))>;
 *   Led led;
 *   led.configure(GPIO_OUTPUT_HIGH);
 *   led.on();
 *
 * @tparam Base  GPIO 控制器基地址
 * @tparam Pin   引脚号
 * @tparam Flags GPIO 标志位 (GPIO_ACTIVE_LOW 等)
 */
template <uintptr_t Base, int Pin, uint32_t Flags = 0>
class GpioPort : public GpioPortBase {
    static constexpr bool ACTIVE_LOW = (Flags & GPIO_ACTIVE_LOW) != 0;

public:
    GpioPort() : GpioPortBase(Base) {}

    [[nodiscard]] int configure(uint32_t flags) { return GpioPortBase::configure(Pin, flags); }

    void set()    { GpioPortBase::set(Pin); }
    void clear()  { GpioPortBase::clear(Pin); }
    void toggle() { GpioPortBase::toggle(Pin); }
    [[nodiscard]] int get() const { return GpioPortBase::get(Pin); }

    void on()  { ACTIVE_LOW ? clear() : set(); }
    void off() { ACTIVE_LOW ? set() : clear(); }
    [[nodiscard]] bool is_on() const {
        return ACTIVE_LOW ? !GpioPortBase::get(Pin) : GpioPortBase::get(Pin);
    }
};

} // namespace hal
