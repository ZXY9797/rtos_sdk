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
| BLE 支持 | 内置 | **GR5525 BLE（HID + UART 透传）** |
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

设备树中 `status = "okay"` 的节点，启动时自动实例化并初始化。业务层通过 `device_get()` 获取已初始化的设备引用。

> **改引脚/波特率/时钟频率？只改 DTS，业务代码零修改。**

详见 [doc/INITCALL.md](doc/INITCALL.md)

### 2. 编译期分发 — DeviceTrait + 自动生成

使用编译期模板特化实现设备分发，相比 Zephyr 的运行时 vtable 方案，实现零开销抽象。

| | 运行时多态（Zephyr） | 编译期多态（RTOS SDK） |
|:---|:---:|:---:|
| 开销 | vtable + 间接调用 | **零** |
| 二进制大小 | 链接所有子类 | **只实例化用到的** |
| 优化 | 间接调用阻碍内联 | **编译器完全优化** |

详见 [doc/DEVICE_TRAIT.md](doc/DEVICE_TRAIT.md)

### 3. 头文件纯净 — 无 MCU 依赖

驱动头文件只使用标准 C++ 类型，不包含任何 MCU 特定头文件。MCU 特定实现在 `.cc` 文件中。

详见 [doc/DRIVER_DESIGN.md](doc/DRIVER_DESIGN.md)

### 4. FOC 电机控制组件 — 内置应用级模块

SDK 提供开箱即用的 FOC（磁场定向控制）电机控制组件，基于 PWM + ADC 驱动构建。

详见 [doc/FOC.md](doc/FOC.md)

### 5. BLE 蓝牙组件 — 厂商无关 API

SDK 提供厂商无关的 BLE API（`ble::` 命名空间），当前实现基于 Goodix GR5525。

详见 [doc/BLE.md](doc/BLE.md)

### 6. OSAL — 头文件零 RTOS 依赖

`osal.h` 是纯 C++ 接口，不包含任何 RTOS 特定宏。所有 OS 相关的类型定义和常量集中在各 RTOS 的 `osal_types.h` 中。

详见 [doc/OSAL.md](doc/OSAL.md)

### 7. IRQ/ISR — 向量表弱别名 + 编译期零开销绑定

中断处理采用向量表弱别名直接覆盖方案，无运行时开销。

详见 [doc/IRQ_ISR.md](doc/IRQ_ISR.md)

### 8. 异常处理框架 — 可插拔后端 + noinit 故障记录

统一处理 HardFault/MemManage/BusFault/UsageFault，支持帧指针回溯、栈快照、noinit 故障记录持久化。

详见 [doc/EXCEPTION_DESIGN.md](doc/EXCEPTION_DESIGN.md)

### 9. 分层命名空间

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
│   ├── demo/                     # GD32 FOC 电机控制 Demo
│   └── demo_ble/                 # GR5525 BLE Demo（HID 键盘 + UART 透传）
│
├── component/                    # 应用级组件
│   ├── foc/                      # FOC 电机控制库
│   └── ble/                      # BLE 蓝牙组件
│
├── embedded/                     # SDK 核心
│   ├── include/                  # 公共头文件
│   ├── drivers/                  # MCU 驱动实现
│   ├── soc/                      # SoC 支持
│   ├── arch/                     # 架构实现（ARM Cortex-M）
│   ├── osal/                     # RTOS 适配层
│   ├── dts/                      # 设备树源文件
│   └── linkscript/               # 链接脚本
│
├── doc/                          # 文档
│   ├── INITCALL.md               # initcall 自动初始化
│   ├── DEVICE_TRAIT.md           # DeviceTrait 编译期分发
│   ├── DRIVER_DESIGN.md          # 驱动设计与扩展指南
│   ├── FOC.md                    # FOC 电机控制组件
│   ├── BLE.md                    # BLE 蓝牙组件
│   ├── OSAL.md                   # OSAL 抽象层
│   ├── IRQ_ISR.md                # IRQ/ISR 中断处理框架
│   └── EXCEPTION_DESIGN.md       # 异常处理框架
│
└── tools/                        # 构建工具
    ├── cmake/                    # CMake 模块
    └── scripts/                  # 生成脚本
```

---

## 快速开始

```bash
# 配置（默认 armgcc 工具链）
cmake -B out -GNinja -Dp=demo

# 编译
ninja -C out

# 产物
ls out/demo.elf  out/demo.bin  out/demo.hex
```

详见 [doc/QUICKSTART.md](doc/QUICKSTART.md)

---

## 许可证

Apache-2.0
