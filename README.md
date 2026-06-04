# RTOS SDK

<p align="center">
  <b>轻量级嵌入式开发框架 | 基于 Zephyr 精简 · 设备树驱动 · 编译期零开销</b>
</p>

---

## 为什么选择 RTOS SDK？

> Zephyr 是一个优秀的工业级 RTOS，但对于中小型项目，它的复杂度往往超出需求。
> RTOS SDK 保留了 Zephyr 最精华的设计（设备树、Kconfig、驱动模型），去掉了不必要的抽象层。

### 对比一览

| 特性 | Zephyr | RTOS SDK |
|:---|:---:|:---:|
| 构建系统 | CMake + 自定义 Python 脚本 + DTS 编译器 | **原生 CMake** |
| 设备树层级 | 5+ 级 `.dtsi` 继承 | **2 级**（SoC + Board） |
| 链接脚本 | 片段拼接 | **单一 `.ld` 文件** |
| 环境搭建 | West + Python + CMake + Ninja + DTC | **CMake + GCC + Ninja** |
| 驱动模型 | 运行时 `struct device` + vtable | **编译期模板，零开销** |
| 内存管理 | 动态分配 + Slab + Heap | **全部静态分配** |
| RTOS 绑定 | 仅支持 Zephyr 内核 | **OSAL 多内核**（FreeRTOS、RT-Thread、ThreadX…） |
| 代码规模 | ~500K 行 | **~5K 行**（不含 SOC HAL） |

---

## 架构总览

```
┌─────────────────────────────────────────────────────────────┐
│                        Application                          │
│  using Led = hal::Device<DT_ORD(DT_ALIAS(led0))>;           │
│  led.on();  // 零开销，编译期解析                              │
├─────────────────────────────────────────────────────────────┤
│                      OSAL (OS 抽象层)                        │
│  Thread · Mutex · Semaphore · Timer                         │
│  ┌──────────┬──────────┬──────────┬──────────┐              │
│  │ FreeRTOS │ RT-Thread│  ThreadX │  Zephyr  │              │
│  └──────────┴──────────┴──────────┴──────────┘              │
├─────────────────────────────────────────────────────────────┤
│                    HAL (硬件抽象层)                           │
│  GpioPort<Base,Pin,Flags> · ClockCtrl<Base>                 │
│  ┌─────────────────────────────────────────┐                │
│  │     DeviceTrait<Ord> 编译期自动分发       │                │
│  │     由 gen_device_traits.py 自动生成      │                │
│  └─────────────────────────────────────────┘                │
├─────────────────────────────────────────────────────────────┤
│                    BSP (板级支持包)                           │
│  SOC HAL/LL 库 · 时钟配置 · 引脚复用                         │
│  ┌──────────┬──────────┬──────────┐                         │
│  │  STM32   │  GD32    │  CH32    │                         │
│  └──────────┴──────────┴──────────┘                         │
├─────────────────────────────────────────────────────────────┤
│                    设备树 + Kconfig                          │
│  编译期硬件描述 · 引脚/时钟/中断 配置                          │
└─────────────────────────────────────────────────────────────┘
```

---

## 核心设计

### 1. 统一设备模板 — 一行代码绑定硬件

```cpp
#include <devicetree.h>
#include <device.h>
#include <drivers_generated.h>  // 自动生成的驱动特化

// DT_ORD 从设备树节点获取 ordinal，Device 通过 ordinal 查找驱动类型
using Led    = hal::Device<DT_ORD(DT_ALIAS(led0))>;
using Button = hal::Device<DT_ORD(DT_ALIAS(sw0))>;

static Led led;
static Button button;

int main() {
    led.configure(GPIO_OUTPUT_HIGH | GPIO_PULL_UP);
    button.configure(GPIO_INPUT | GPIO_PULL_DOWN);

    led.on();           // 自动处理 ACTIVE_HIGH / ACTIVE_LOW
    button.is_on();     // 编译期绑定，零运行时开销
}
```

**设备树配置：**
```dts
/ {
    aliases {
        led0 = &green_led;
        sw0  = &key_pc13;
    };

    leds {
        green_led: led_0 {
            gpios = <&gpioe 3 GPIO_ACTIVE_HIGH>;  // 改引脚？只改这里
        };
        key_pc13: button_0 {
            gpios = <&gpioc 13 GPIO_ACTIVE_LOW>;
        };
    };
};
```

> **改硬件引脚？只改 DTS，代码零修改。**

### 2. 编译期分发 — DeviceTrait + 自动生成

```
                    DT_ORD(DT_ALIAS(led0))
                            │
                            ▼
                    Device<43>  ──────────── DeviceTrait<43>::type
                                               │
                                               ▼
                                         GpioPort<0x58021000, 3, 0>
                                               │
                                     ┌─────────┴─────────┐
                                     │   GpioPortBase     │  ← 非模板，MCU 实现
                                     │   base_ = 0x58021000 │
                                     └───────────────────┘
```

- **`DeviceTrait<Ord>`** — 主模板，由 `gen_device_traits.py` 自动生成特化
- **`GpioPort<Base, Pin, Flags>`** — 值参数模板，编译期绑定基地址/引脚/标志
- **`GpioPortBase`** — 非模板基类，MCU 特定实现在 `.cc` 文件中

| | 运行时多态（Zephyr） | 编译期多态（RTOS SDK） |
|:---|:---:|:---:|
| 开销 | vtable + 间接调用 | **零** |
| 二进制大小 | 链接所有子类 | **只实例化用到的** |
| 优化 | 间接调用阻碍内联 | **编译器完全优化** |

### 3. 头文件纯净 — 无 MCU 依赖

```
include/drivers/
├── gpio.h          ← 纯 C++，无 #include <stm32_*.h>
├── clock.h         ← 纯 C++，无 #ifdef CONFIG_xxx
├── exti.h          ← 纯 C++，无 GPIO_TypeDef
└── gpio_intc.h     ← 纯 C++，无板级信息
```

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

### 4. 代码生成器 — YAML 驱动

`gen_device_traits.py` 解析 DT 绑定 YAML 中的 `cxx-driver` 节，自动生成 `DeviceTrait<Ord>` 特化：

```yaml
# binding YAML (gpio-leds.yaml)
cxx-driver:
  template: GpioPort
  header: drivers/gpio.h
  args:
    - phandle-reg: gpios
    - field: gpios/pin
    - field: gpios/flags
```

```cpp
// 自动生成的 drivers_generated.h
template <> struct DeviceTrait<43> {  // led0
    using type = GpioPort<DT_REG_ADDR(...), 3, 0>;
};
```

- **非侵入**：不修改 Zephyr 的 `gen_defines.py`
- **声明式**：驱动映射在 binding YAML 中声明，与硬件描述放在一起
- **HAL 纯净**：驱动头文件无任何额外代码

### 5. 分层命名空间

```cpp
namespace hal {      // 硬件抽象层：驱动、寄存器访问
    class GpioPortBase;
    template <uintptr_t Base, int Pin, uint32_t Flags> class GpioPort;
    template <int Ord> struct DeviceTrait;
    template <int Ord> using Device = typename DeviceTrait<Ord>::type;
}

namespace sys {      // 系统层：初始化、中断管理
    struct InitEntry;
    class Irq;
}

namespace osal {     // OS 抽象层：线程、同步原语
    class Thread;
    class Mutex;
    class Semaphore;
}
```

---

## 目录结构

```
rtos_sdk/
├── app/                          # 应用项目
│   └── demo/
│       ├── config/board.dts      # 板级设备树
│       ├── config/Kconfig        # 板级配置
│       └── main.cc               # 应用代码
│
├── embedded/                     # SDK 核心
│   ├── include/                  # 公共头文件
│   │   ├── device.h            # DeviceTrait + Device + DT_ORD
│   │   ├── drivers/              # 纯净驱动接口
│   │   ├── osal/                 # OS 抽象接口
│   │   ├── arch/                 # 架构抽象
│   │   └── devicetree/           # 设备树解析
│   │
│   ├── drivers/                  # MCU 驱动实现
│   │   ├── gpio/gpio_stm32.cc
│   │   ├── clock_control/clock_stm32.cc
│   │   └── interrupt_controller/
│   │
│   ├── soc/st/                   # STM32 SOC 支持
│   │   ├── stm32cube/            # ST HAL/LL 库
│   │   └── include/              # SOC 特定头文件
│   │
│   ├── arch/                     # 架构实现（ARM Cortex-M）
│   ├── osal/                     # RTOS 适配层
│   ├── dts/                      # 设备树源文件
│   └── linkscript/               # 链接脚本
│
└── tools/                        # 构建工具
    ├── cmake/                    # CMake 模块
    └── scripts/
        ├── gen_device_traits.py  # DeviceTrait 特化生成器
        └── dts/                  # DTS 相关脚本
```

---

## 快速开始

### 环境要求

```bash
# 仅需 4 个工具
python3           # 设备树解析 + 驱动特化生成
cmake             # 构建系统
gcc-arm-none-eabi # ARM 交叉编译器
ninja             # 构建后端
```

### 构建 Demo

```bash
# 配置
cmake -B out -GNinja -Dp=demo

# 编译
ninja -C out

# 产物
ls out/demo.elf  out/demo.bin  out/demo.hex
```

Demo 源码在 `app/demo/main.cc`，展示了 LED 控制 + 按键检测 + FreeRTOS 线程。

---

## 扩展新驱动

驱动头文件零额外代码，只需两步：写驱动 + 在 binding YAML 中声明映射。

### 1. 创建驱动头文件 + 实现文件

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

### 2. 在 binding YAML 声明 cxx-driver

在对应的 DT 绑定 YAML 中添加 `cxx-driver` 节：

```yaml
# pwm-controller.yaml 末尾添加：
cxx-driver:
  template: PwmChannel
  header: drivers/pwm.h
  args:
    - phandle-reg: pwms
    - field: pwms/channel
```

### 3. 用户代码

```cpp
#include <device.h>
#include <drivers_generated.h>

using Motor = hal::Device<DT_ORD(DT_ALIAS(motor0))>;
Motor motor;
motor.setDuty(50);
```

---

## 许可证

Apache-2.0
