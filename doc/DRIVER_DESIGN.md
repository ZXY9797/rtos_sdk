# 驱动设计与扩展指南

## 头文件纯净设计

驱动头文件只使用标准 C++ 类型，不包含任何 MCU 特定头文件。MCU 特定实现在 `.cc` 文件中。

```
include/drivers/
├── gpio.h          ← 纯 C++，无 #include <stm32_*.h>
├── clock.h         ← 纯 C++，无 #ifdef CONFIG_xxx
├── exti.h          ← 纯 C++，无 GPIO_TypeDef
└── gpio_intc.h     ← 纯 C++，无板级信息
```

### 示例

```cpp
// gpio.h — 头文件只用标准类型
class GpioPortBase {
    uintptr_t base_;  // 通用指针，无 MCU 类型
public:
    int configure(int pin, uint32_t flags);
    void set(int pin);
    void clear(int pin);
};

// gpio_stm32.cc — 实现文件才包含 MCU 头文件
#include <stm32_ll_gpio.h>  // MCU 特定，只在 .cc 中
int GpioPortBase::configure(int pin, uint32_t flags) {
    auto* gpio = reinterpret_cast<GPIO_TypeDef*>(base_);
    LL_GPIO_SetPinMode(gpio, 1U << pin, LL_GPIO_MODE_OUTPUT);
}
```

### 设计优势

- 头文件可跨 MCU 复用
- 编译依赖最小化
- 接口与实现解耦

## 扩展新驱动

驱动头文件零额外代码，只需两步：写驱动 + 在 binding YAML 中声明映射。

### 步骤 1：创建驱动头文件 + 实现文件

```cpp
// include/drivers/pwm.h — 纯 C++，无额外宏
#pragma once
#include <cstdint>

namespace hal {

class PwmBase {
public:
    explicit PwmBase(uintptr_t base) : base_(base) {}
    [[nodiscard]] int setDuty(uint32_t duty);
protected:
    uintptr_t base_;
};

template <uintptr_t Base, uint32_t Channel>
class PwmChannel : public PwmBase {
public:
    PwmChannel() : PwmBase(Base) {}
    [[nodiscard]] int setDuty(uint32_t duty) {
        return PwmBase::setDuty(duty);
    }
};
} // namespace hal
```

```cpp
// drivers/pwm_stm32.cc — MCU 特定实现
#include <drivers/pwm.h>
#include <stm32_ll_tim.h>

int hal::PwmBase::setDuty(uint32_t duty) {
    auto* tim = reinterpret_cast<TIM_TypeDef*>(base_);
    // ...
}
```

### 步骤 2：在 binding YAML 声明 cxx-driver + init-cfg

在对应的 DT 绑定 YAML 中添加 `cxx-driver` 节：

```yaml
# pwm-controller.yaml 末尾添加：
cxx-driver:
  template: PwmChannel
  header: drivers/pwm.h
  args:
    - phandle-reg: pwms
    - field: pwms/channel
  init-cfg:                             # 可选：自动初始化配置
    type: PwmConfig
    fields:
      frequency: { prop: pwm-frequency, default: 1000 }
      duty:      { default: 0 }
```

### 步骤 3：用户代码

```cpp
#include <device.h>
#include <drivers_generated.h>

int main() {
    auto &motor = device_get(motor0);   // 已自动初始化
    motor.setDuty(50);
}
```

## YAML 字段说明

| 字段 | 说明 |
|:---|:---|
| `template` | C++ 模板类名 |
| `header` | 头文件路径（相对于 include/） |
| `args` | 模板参数列表 |
| `init-cfg` | init() 配置结构体（可选） |
| `init-cfg.type` | 配置结构体类型名 |
| `init-cfg.fields` | 字段映射（prop 绑定 DTS 属性） |
| `init-cfg.fields.*.default` | 默认值 |
| `init-cfg.fields.*.cast` | 类型转换（枚举等） |
| `init-cfg.fields.*.prop` | 绑定的 DTS 属性名 |
