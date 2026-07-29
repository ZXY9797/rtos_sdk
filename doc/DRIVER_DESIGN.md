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

驱动实现保持与设备树解耦。新增驱动需要三步：写驱动、写一个轻量 C++
adapter、在 binding YAML 中注册 adapter。

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

### 步骤 2：创建 DeviceTrait adapter

adapter 负责把 DTS 宏转换为具体驱动类型和初始化配置：

```cpp
// include/device_adapters/pwm_dt.h
#pragma once

#include <device.h>
#include <drivers/pwm.h>

#define HAL_PWM_DT_ADAPT(node_id)                                  \
    template <>                                                    \
    struct DeviceTrait<DT_ORD(node_id)> {                          \
        using type = PwmChannel<DT_REG_ADDR(node_id),              \
                                DT_PROP(node_id, channel)>;         \
        static type instance;                                      \
        static int init()                                          \
        {                                                          \
            PwmConfig config{};                                    \
            config.frequency =                                    \
                DT_PROP_OR(node_id, pwm_frequency, 1000U);         \
            config.duty = 0U;                                      \
            return static_cast<int>(instance.init(config));        \
        }                                                          \
    };
```

adapter 应只做编译期装配和轻量配置构造，不直接访问寄存器。属性范围使用
`static_assert` 校验，避免无效配置推迟到运行期。

### 步骤 3：在 binding YAML 注册 adapter

在对应的 DT 绑定 YAML 中添加稳定的 `cxx-driver` 元数据：

```yaml
# pwm-controller.yaml 末尾添加：
cxx-driver:
  adapter:
    header: device_adapters/pwm_dt.h
    macro: HAL_PWM_DT_ADAPT
  type-name: PwmChannel
  init: true
  interrupts:
    - name: update
      method: isr_update
      uses-osal: true
  init-level: pre-kernel-2
  init-priority: 25
```

增加或调整 `pwm-frequency` 等设备属性时只修改 binding、DTS 和 adapter，
不修改 `gen_device_traits.py`。

### 步骤 4：用户代码

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
| `adapter.header` | adapter 头文件路径（相对于 include/） |
| `adapter.macro` | 接收 `node_id` 的宏名 |
| `type-name` | 诊断报告类型名 |
| `init` | 是否生成 initcall |
| `interrupts` | IRQ 名称、adapter 分发方法、OSAL 使用和共享属性 |
| `scope` | 匹配当前节点或其子节点 |
| `requires` | parent/phandle 初始化依赖 |
| `init-level` | initcall 级别 |
| `init-priority` | 同级初始化优先级 |
| `readiness` | 运行期诊断的就绪策略 |
| `device-base` | 是否继承 `DeviceBase` |
