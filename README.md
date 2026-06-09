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
| 驱动模型 | 运行时 `struct device` + vtable | **编译期模板 + initcall 自动初始化** |
| 内存管理 | 动态分配 + Slab + Heap | **全部静态分配** |
| RTOS 绑定 | 仅支持 Zephyr 内核 | **OSAL 多内核**（FreeRTOS、RT-Thread） |
| SoC 支持 | 内置 400+ | **STM32、GD32、Goodix GR5525** |
| 代码规模 | ~500K 行 | **~5K 行**（不含 SOC HAL） |

---

## 架构总览

```
┌─────────────────────────────────────────────────────────────┐
│                        Application                          │
│  auto &uart = device_get(uart0);  // 已自动初始化，直接用     │
│  uart.send(data, len);                                       │
├─────────────────────────────────────────────────────────────┤
│                      OSAL (OS 抽象层)                        │
│  Thread · Mutex · Semaphore                                 │
│  ┌──────────┬──────────┐                                    │
│  │ FreeRTOS │ RT-Thread│                                    │
│  └──────────┴──────────┘                                    │
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
│  │  STM32   │  GD32    │  GR5525  │                         │
│  └──────────┴──────────┴──────────┘                         │
├─────────────────────────────────────────────────────────────┤
│                    设备树 + Kconfig                          │
│  编译期硬件描述 · 引脚/时钟/中断 配置                          │
└─────────────────────────────────────────────────────────────┘
```

---

## 核心设计

### 1. initcall 自动初始化 — DTS okay 即用

设备树中 `status = "okay"` 的节点，启动时自动实例化并初始化。业务层通过 `device_get()` 获取已初始化的设备引用：

```cpp
#include <device.h>
#include <drivers_generated.h>

int main() {
    // 设备已在 initcall 阶段自动初始化（baudrate 等参数从 DTS 解析）
    auto &uart = device_get(uart0);
    auto &spi  = device_get(spi0);

    uart.send(data, len);    // 直接用
    spi.sync_send(tx, rx, 4, 1000);
}
```

**设备树配置：**
```dts
aliases {
    uart0 = &usart0;
    spi0  = &spi0;
};

usart0: usart@40013800 {
    compatible = "gd,gd32-usart";
    reg = <0x40013800 0x400>;
    interrupts = <37 0>;
    status = "okay";            // okay → 自动初始化
    current-speed = <115200>;   // init 参数从 DTS 解析
};

spi0: spi@40013000 {
    compatible = "gd,gd32-spi";
    reg = <0x40013000 0x400>;
    status = "okay";
    spi-max-frequency = <1000000>;
};
```

> **改引脚/波特率/时钟频率？只改 DTS，业务代码零修改。**

**initcall 链路：**
```
DTS status="okay" + 属性
        ↓
gen_device_traits.py 读取 YAML init-cfg + DTS 属性
        ↓
生成 drivers_generated.h（DeviceTrait + instance 声明 + device_get）
生成 drivers_generated.cc（实例定义 + initcall 注册）
        ↓
启动：z_cstart() → run_initcalls() → 自动 init 所有设备
        ↓
main()：auto &uart = device_get(uart0);  // 直接用
```

### 2. 编译期分发 — DeviceTrait + 自动生成

```
                    device_get(uart0)
                            │
                            ▼
                    DT_ORD(DT_ALIAS(uart0)) = 14
                            │
                            ▼
                    DeviceTrait<14>::instance  ← 全局静态实例（已自动 init）
                            │
                            ▼
                    Uart<0x40013800, 37>
                            │
                  ┌─────────┴─────────┐
                  │    UartBase        │  ← 非模板，MCU 实现
                  │    base_ = 0x40013800 │
                  └───────────────────┘
```

- **`DeviceTrait<Ord>`** — 主模板，由 `gen_device_traits.py` 自动生成特化 + 静态实例
- **`device_get(alias)`** — 编译期获取已初始化的设备引用
- **`Uart<Base, Irq>`** — 值参数模板，编译期绑定基地址/中断号
- **`UartBase`** — 非模板基类，MCU 特定实现在 `.cc` 文件中

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

`gen_device_traits.py` 解析 DT 绑定 YAML 中的 `cxx-driver` 节，自动生成 `DeviceTrait<Ord>` 特化、静态实例和 initcall 注册：

```yaml
# binding YAML (gd,gd32-usart.yaml)
cxx-driver:
  template: Uart
  header: drivers/uart.h
  args: [node-reg, irq]
  init-cfg:                           # ← init() 参数映射
    type: UartConfig
    rx-buffer-size:
      default: 256
    fields:
      baudrate:  { prop: current-speed, default: 115200 }
      data_bits: { default: 8, cast: DataBits }
      stop_bits: { default: 0, cast: StopBits }
      parity:    { default: 0, cast: Parity }
```

```cpp
// 自动生成的 drivers_generated.h
template <> struct DeviceTrait<14> {  // uart0
    using type = Uart<DT_REG_ADDR(...), 37>;
    static type instance;              // 全局静态实例
};
inline auto &device_get() { return DeviceTrait<14>::instance; }
#define device_get(alias) hal::device_get<DT_ORD(DT_ALIAS(alias))>()

// 自动生成的 drivers_generated.cc
DeviceTrait<14>::type DeviceTrait<14>::instance{};
static int _init_uart0() {
    UartConfig cfg {};
    cfg.baudrate = 115200;             // 从 DTS current-speed 解析
    cfg.rx_buffer = _dev_uart0_rx_buf; // 自动生成的静态 buffer
    return static_cast<int>(DeviceTrait<14>::instance.init(cfg));
}
SYS_INIT(hal::_init_uart0, INITCALL_LEVEL_PRE_KERNEL_2, 25);
```

- **非侵入**：不修改 Zephyr 的 `gen_defines.py`
- **声明式**：驱动映射 + init 参数在 binding YAML 中声明
- **自动初始化**：DTS `status = "okay"` → 启动时自动 init，业务层零配置

### 5. FOC 电机控制组件 — 内置应用级模块

SDK 提供开箱即用的 FOC（磁场定向控制）电机控制组件，基于 PWM + ADC 驱动构建：

```cpp
#include "foc_app.h"

int main(void) {
    foc_app::start();  // 启动 FOC 系统（PWM 20kHz + ADC 采样 + CLI 调试）
}
```

**特性：**
- **空间矢量调制（SVPWM）** — 三相 PWM 输出，20kHz 默认频率
- **电流采样** — ADC 同步采样，支持内嵌（inline）和低侧（low-side）模式
- **CLI 调试** — UART 命令行，实时调节参数
- **可配置** — Kconfig 选择 PWM 频率、电机参数、调试选项

**设备树配置：**
```dts
aliases {
    pwm_u = &timer0_ch0;
    pwm_v = &timer0_ch1;
    pwm_w = &timer0_ch2;
    adc0  = &adc0;
};
```

> **FOC 组件位于 `component/foc/`，可独立于应用复用。**

### 6. ADC 驱动 — GD32 支持

新增 GD32 ADC 驱动，支持多通道采样、DMA 传输：

```dts
adc0: adc@40022800 {
    compatible = "gd,gd32-adc";
    reg = <0x40022800 0x400>;
    interrupts = <18 0>;
    status = "okay";
    #io-channel-cells = <1>;
};
```

### 7. PWM 驱动增强 — 多通道 + 输出控制

PWM 驱动新增多通道支持、输出使能控制和更新中断回调：

```cpp
auto &pwm = device_get(pwm_u);

pwm.enable_output();                    // 使能 MOE（高级定时器）
pwm.set_pulse(PwmChannel::Ch1, 500);   // 多通道独立设置占空比
pwm.set_update_callback(my_isr, arg);   // 更新中断回调（FOC 电流环）
pwm.start();
```

### 8. OSAL — 头文件零 RTOS 依赖

`osal.h` 是纯 C++ 接口，不包含任何 RTOS 特定宏。所有 OS 相关的类型定义和常量集中在各 RTOS 的 `osal_types.h` 中：

```
include/osal/osal.h                  ← 纯 C++，无 #ifdef configMAX_PRIORITIES
osal/freertos/osal_types.h           ← FreeRTOS 类型 + 常量
osal/rt-thread/osal_types.h          ← RT-Thread 类型 + 常量
```

```cpp
// osal_types.h (freertos) — OS 特定常量在此定义
namespace osal {
inline constexpr uint32_t kSemaphoreMaxCount = 0xFFFFU;
inline constexpr uint8_t  kPriorityMax =
    static_cast<uint8_t>((configMAX_PRIORITIES > 0) ? (configMAX_PRIORITIES - 1) : 0);
inline constexpr size_t   kDefaultThreadStackBytes = 1024U;
}  // namespace osal

// osal.h — 直接使用，无条件编译
namespace osal {
inline constexpr Priority kDefaultThreadPriority =
    static_cast<Priority>(kPriorityMax / 3U);  // 来自 osal_types.h
}
```

### 9. IRQ/ISR — 向量表弱别名 + 编译期零开销绑定

中断处理采用向量表弱别名直接覆盖方案，无运行时开销：

```
vector_table.S                    驱动代码
┌──────────────────────┐          ┌──────────────────────────────┐
│ .weak IRQ37_Handler  │          │ HAL_ISR_CONNECT(37, UartBase, │
│ .thumb_set → Default │  ──────→│     &DeviceTrait<14>::instance)│
│                      │  覆盖   │                              │
│ .word IRQ37_Handler  │         │ extern "C" void IRQ37_Handler │
│   ↑ 向量表入口        │         │   { obj->isr_handler(); }     │
└──────────────────────┘          └──────────────────────────────┘
                                         │
                                         ▼
                                  void UartBase::isr_handler() {
                                      // 读寄存器、写 ring buffer
                                  }
```

**`HAL_ISR_CONNECT` 宏** — 生成 `extern "C"` 函数，直接覆盖向量表弱别名：

```cpp
// irq.h
#define HAL_ISR_CONNECT(irq_n, type, obj_p)                         \
    extern "C" void IRQ##irq_n##_Handler(void) {                    \
        static_cast<type *>(const_cast<void *>(                     \
            static_cast<const void *>(obj_p)))                       \
            ->isr_handler();                                         \
    }
```

**驱动示例：**

```cpp
class UartBase {
public:
    void isr_handler();  // 中断处理逻辑
};

void UartBase::isr_handler() {
    auto *regs = reinterpret_cast<UsartRegs *>(m_base);
    if (regs->STAT & STAT_RBNE) {
        uint8_t ch = static_cast<uint8_t>(regs->DATA);
        m_rx_ring.write(&ch, 1);
        m_rx_sem.release();
    }
}

// 由 gen_device_traits.py 自动生成：
HAL_ISR_CONNECT(37, UartBase, &DeviceTrait<14>::instance)
```

| | 运行时查表（Zephyr） | 直接覆盖（RTOS SDK） |
|:---|:---:|:---:|
| 中断延迟 | sw_isr_table 间接跳转 | **硬件直接跳转** |
| RAM 开销 | isr_table_entry 数组 | **零** |
| 代码生成 | irq_connect() 运行时注册 | **编译期宏展开** |

**配套工具：**
- `hal::Irq` — 静态方法：enable / disable / setPriority / isEnabled
- `hal::IrqGuard` — RAII 中断锁（构造锁中断，析构恢复）
- `gen_irq_entries` / `gen_irq_Weaks` — `.altmacro` 宏，根据 `CONFIG_NUM_IRQS` 自动生成向量表条目和弱别名

### 10. 异常处理框架 — 可插拔后端 + noinit 故障记录

统一处理 HardFault/MemManage/BusFault/UsageFault，支持帧指针回溯、栈快照、noinit 故障记录持久化：

```cpp
#include <arch/arm/cortex_m/fault.h>

int main() {
    hal::fault::bootCheck();  // 检查 noinit 记录，有历史 fault 则通过 LOG 输出
    // ...
}
```

**特性：**
- **可插拔后端**：`IBackend` 接口，支持 noinit RAM、UART、Flash 等多种记录方式
- **noinit 故障记录**：热复位后 RAM 保留，启动时自动输出历史 fault
- **帧指针回溯**：16 级调用栈地址
- **栈内存快照**：128 字节原始栈数据，可离线 GDB 解析
- **双路径输出**：异常上下文用 `putc`（安全），正常线程用 `log_write`（统一日志通道）
- **FreeRTOS 钩子**：栈溢出 / malloc 失败自动捕获

```
异常发生 → fault.S/naked 入口 → buildRecord() → 遍历后端 onFault()
                                       ↓
                              NoinitBackend: 写 .noinit RAM
                              UartBackend:   putc 输出诊断
                                       ↓
                                  死循环 (cpsid i + wfi)
                                       ↓
                              热复位 → bootCheck() → log_write 输出历史记录
```

> 详细设计见 [doc/EXCEPTION_DESIGN.md](doc/EXCEPTION_DESIGN.md)

### 11. 分层命名空间

```cpp
namespace hal {      // 硬件抽象层：驱动、寄存器访问
    class GpioPortBase;
    template <uintptr_t Base, int Pin, uint32_t Flags> class GpioPort;
    template <int Ord> struct DeviceTrait;
    template <int Ord> using Device = typename DeviceTrait<Ord>::type;
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
│       ├── main.cc               # 应用代码
│       ├── foc_app.cc            # FOC 电机控制应用
│       └── foc_app.h
│
├── component/                    # 应用级组件
│   ├── foc/                      # FOC 电机控制库
│   │   ├── include/              # 公共接口
│   │   ├── src/                  # 实现（SVPWM、电流采样、CLI）
│   │   └── Kconfig               # 配置选项
│   └── ble/                      # BLE 蓝牙组件
│       ├── include/
│       └── goodix/               # Goodix BLE 实现
│
├── embedded/                     # SDK 核心
│   ├── include/                  # 公共头文件
│   │   ├── device.h            # DeviceTrait + Device + DT_ORD
│   │   ├── init.h              # init level + SYS_INIT
│   │   ├── drivers/              # 纯净驱动接口
│   │   ├── osal/                 # OS 抽象接口
│   │   ├── arch/                 # 架构抽象
│   │   └── devicetree/           # 设备树解析
│   │
│   ├── drivers/                  # MCU 驱动实现
│   │   ├── gpio/                 # GPIO（STM32、GD32、GR5525）
│   │   ├── uart/                 # UART（STM32、GD32、GR5525）
│   │   ├── spi/                  # SPI（STM32、GD32、GR5525）
│   │   ├── pwm/                  # PWM（STM32、GD32）
│   │   ├── adc/                  # ADC（GD32）
│   │   ├── dma/                  # DMA（GD32）
│   │   ├── clock_control/
│   │   └── interrupt_controller/
│   │
│   ├── soc/                      # SoC 支持
│   │   ├── st/                   # STM32
│   │   │   ├── stm32cube/        # ST HAL/LL 库
│   │   │   └── include/
│   │   ├── gd/                   # GD32
│   │   │   └── gd32f50x/
│   │   └── goodix/               # Goodix GR5525
│   │       └── gr5525x/
│   │
│   ├── arch/                     # 架构实现（ARM Cortex-M）
│   ├── osal/                     # RTOS 适配层
│   ├── dts/                      # 设备树源文件
│   │   ├── arm/st/               # STM32 DTS
│   │   ├── arm/gd/               # GD32 DTS
│   │   ├── arm/goodix/           # Goodix DTS
│   │   └── bindings/             # DT 绑定 YAML
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

Demo 源码在 `app/demo/main.cc`，面向 GD32F503，展示 FOC 电机控制（PWM + ADC + CLI 调试）。

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

### 2. 在 binding YAML 声明 cxx-driver + init-cfg

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

### 3. 用户代码

```cpp
#include <device.h>
#include <drivers_generated.h>

int main() {
    auto &motor = device_get(motor0);   // 已自动初始化
    motor.setDuty(50);
}
```

---

## 许可证

Apache-2.0
