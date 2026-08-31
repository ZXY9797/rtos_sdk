# RTOS SDK

> 产品化能力、必须提供的硬件适配和发布验收边界见
> [产品级 RTOS 架构基线](doc/PRODUCT_RTOS.md)。

<p align="center">
  <b>轻量级嵌入式开发框架 | 基于 Zephyr 精简 · 设备树驱动 · 编译期零开销 · 产品级 Bootloader</b>
</p>

---

## 为什么选择 RTOS SDK？

> Zephyr 是一个优秀的工业级 RTOS，但对于中小型项目，它的复杂度往往超出需求。
> RTOS SDK 保留了 Zephyr 最精华的设计（设备树、Kconfig、驱动模型），去掉了不必要的抽象层。

### 对比一览

| 特性 | Zephyr | RTOS SDK |
|:---|:---:|:---:|
| 构建系统 | CMake + Python + DTS/Kconfig | **CMake + 可测试的 Python 生成器 + DTS/Kconfig** |
| 设备树层级 | 5+ 级 `.dtsi` 继承 | **2 级**（SoC + Board） |
| 链接脚本 | 片段拼接 | **单一 `.ld` 文件** |
| 环境搭建 | West + Python + CMake + Ninja + DTC | **CMake + GCC + Ninja** |
| 驱动模型 | 运行时 `struct device` + vtable | **编译期模板 + initcall + 依赖校验** |
| 内存管理 | 动态分配 + Slab + Heap | **实时/故障路径固定容量，初始化分配显式检查** |
| RTOS 绑定 | 仅支持 Zephyr 内核 | **OSAL 多内核**（FreeRTOS、RT-Thread） |
| SoC 支持 | 内置 400+ | **STM32、GD32、Goodix GR5525** |
| BLE 支持 | 内置 | **GR5525 BLE（HID + UART 透传）** |
| 启动升级 | MCUboot + Zephyr 配置体系 | **preloader/loader/upgrade + 三合一固件** |
| 代码规模 | 大型通用生态 | **面向项目裁剪，按 target 选择性链接** |

---

## 架构总览

```
┌─────────────────────────────────────────────────────────────┐
│                        Application                          │
│  auto &uart = device_get(uart0);  // 已自动初始化，直接用     │
│  router.set_routes(...);  // 只配路由表                       │
├─────────────────────────────────────────────────────────────┤
│                    Link 通信协议栈                            │
│  Router · FrameCodec · Fragmenter · ACK · 重传               │
│  ┌──────────┬──────────┬──────────┐                         │
│  │ UartLink │ CanLink  │ BleLink  │  ← SYS_INIT 自动注册    │
│  └──────────┴──────────┴──────────┘                         │
├─────────────────────────────────────────────────────────────┤
│                      OSAL (OS 抽象层)                        │
│  Thread · Mutex · Semaphore · StreamBuffer                  │
│  ┌──────────┬───────────┬──────────┐                         │
│  │ FreeRTOS │ RT-Thread │ Baremetal│                         │
│  └──────────┴───────────┴──────────┘                         │
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
初始化函数返回非 0 时，启动流程会停在错误点，避免失败设备继续进入业务层。

> **改引脚/波特率/时钟频率？只改 DTS，业务代码零修改。**

详见 [doc/INITCALL.md](doc/INITCALL.md)

### 2. 编译期分发 — DeviceTrait + 自动生成

使用编译期模板特化实现设备分发，相比 Zephyr 的运行时 vtable 方案，实现零开销抽象。
生成器会解析 parent/phandle 依赖并在配置阶段校验初始化顺序；子设备通过 `parent-ord` 获取父总线对象，不再把父节点寄存器地址误当成 C++ 对象指针。

| | 运行时多态（Zephyr） | 编译期多态（RTOS SDK） |
|:---|:---:|:---:|
| 开销 | vtable + 间接调用 | **零** |
| 二进制大小 | 链接所有子类 | **只实例化用到的** |
| 优化 | 间接调用阻碍内联 | **编译器完全优化** |

详见 [doc/DEVICE_TRAIT.md](doc/DEVICE_TRAIT.md)

设备状态使用显式生命周期表达。继承 `DeviceBase` 的驱动只有在 `Initialized`/`Open` 且 `last_error()==Status::Ok` 时才被认为 ready，进入 `Error` 后不会被诊断表误判为可用。

详见 [doc/DRIVER_MODEL.md](doc/DRIVER_MODEL.md)

### 3. 头文件纯净 — 无 MCU 依赖

驱动头文件只使用标准 C++ 类型，不包含任何 MCU 特定头文件。MCU 特定实现在 `.cc` 文件中。

详见 [doc/DRIVER_DESIGN.md](doc/DRIVER_DESIGN.md)

### 4. FOC 电机控制组件 — 内置应用级模块

SDK 提供开箱即用的 FOC（磁场定向控制）电机控制组件，基于 PWM + ADC 驱动构建。

详见 [doc/FOC.md](doc/FOC.md)

三轴非正交手持云台的产品方案见
[app/product/gimbal/doc/ARCHITECTURE.md](app/product/gimbal/doc/ARCHITECTURE.md)。

### 5. BLE 蓝牙组件 — 厂商无关 API

SDK 提供厂商无关的 BLE API（`ble::` 命名空间），当前实现基于 Goodix GR5525。

详见 [doc/BLE.md](doc/BLE.md)

### 6. OSAL — 应用 API 隔离 RTOS 差异

应用只使用 `osal.h` 的统一 C++ API，不直接包含或调用内核接口。构建系统根据 Kconfig
选择一个 backend，并由该 backend 的 `osal_types.h` 提供 native storage、优先级和容量
常量；因此这是源码级接口隔离，不承诺 `osal.h` 可脱离所选 backend 独立编译。

详见 [doc/OSAL.md](doc/OSAL.md)

### 7. IRQ/ISR — 向量表弱别名 + 编译期零开销绑定

中断处理采用向量表弱别名直接覆盖方案，无运行时开销。

详见 [doc/IRQ_ISR.md](doc/IRQ_ISR.md)

### 8. Link 通信协议栈 — 多链路路由 + 自动初始化

支持 UART、CAN、BLE 等物理链路，提供帧封装、路由转发、ACK 应答、超时重传、分片重组。业务层只配路由表，链路注册和协议处理通过 `SYS_INIT` 自动完成。

详见 [doc/LINK.md](doc/LINK.md)

### 9. Bootloader — 分阶段启动 + DFU + loader 自升级

当前 boot 链路分为 `preloader`、`loader`、`upgrade` 和 `app`。`preloader` 是最小信任根，生产配置认证固定 loader；`loader` 负责 DFU、签名/防降级校验以及单执行槽的掉电安全 sector-swap 安装；`upgrade` 仅保留开发配置下的 loader 自升级执行；`app` 只通过公开 boot API 发布产品元数据和确认镜像。boot 控制日志和单 sector scratch 使用独立分区，不与 NVS storage 复用。首次启动未确认时，loader 会把保存在 `upgrade` 的旧应用交换回 `slot0`；这提供单次 trial 自动回滚，但不是两个可独立启动执行槽的 A/B 方案。

`boot_common` 提供 app、loader、preloader、upgrade 共享的镜像元数据、产品元数据、Flash 分区接口、镜像确认、SHA-256 和 DFU CRC。产品相关配置放在 `app/product/<product>` 和 `bootloader/product/<product>`，公共 bootloader 目录只保留跨产品源码。

支持通过 `-DFIRMWARE_TYPE=app|preloader|loader|upgrade` 单独构建固件，也支持使用 `bootloader/scripts/build_3in1.py` 生成 `<product>_3in1.bin`。

详见 [doc/BOOT_FLOW.md](doc/BOOT_FLOW.md) 和 [doc/BOOTLOADER.md](doc/BOOTLOADER.md)

### 10. 架构边界 — 产品编排与可复用组件分离

`app/product` 只负责产品编排和产品策略；`component` 保持产品无关，承载可复用算法、FOC、Link、NVS、BLE 适配等能力；应用固件可使用公开 boot API，但不能直接编译 bootloader 私有源文件。

详见 [doc/ARCHITECTURE.md](doc/ARCHITECTURE.md) 和 [doc/APP_LAYOUT.md](doc/APP_LAYOUT.md)

### 11. 异常处理框架 — 可插拔后端 + noinit 故障记录

统一处理 HardFault/MemManage/BusFault/UsageFault，支持帧指针回溯、栈快照、noinit 故障记录持久化。

详见 [doc/EXCEPTION_DESIGN.md](doc/EXCEPTION_DESIGN.md)

### 12. 分层命名空间

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
│   └── product/                  # 产品固件
│       ├── demo/                 # GD32 FOC 电机控制 Demo
│       └── demo_ble/             # GR5525 BLE Demo（HID 键盘 + UART 透传）
│
├── bootloader/                   # 启动与升级固件
│   ├── include/boot/             # 公开 boot API
│   ├── common/                   # app/loader/preloader/upgrade 共享实现
│   ├── loader/                   # 启动决策、DFU、app 校验与 sector-swap
│   ├── preloader/                # 第一阶段信任根与 loader 认证
│   ├── upgrade/                  # loader 自升级执行固件
│   ├── product/                  # 产品 boot 配置、linker 和分区实现
│   └── scripts/                  # 三合一固件构建与合并脚本
│
├── component/                    # 应用级组件
│   ├── algo/                     # 纯算法组件
│   ├── algo/                     # 数学、DSP 与单轴轨迹算法原语
│   ├── control_contracts/        # 定长控制领域数据契约
│   ├── motion/                   # 非正交运动学、动力学和三轴轨迹规划
│   ├── attitude/                 # 姿态 EKF
│   ├── control/                  # 频率整形反馈控制
│   ├── position_sensor/          # 连续 Hall 与位置标定
│   ├── thermal/                  # 恒温控制
│   ├── safety/                   # 安全监督与故障锁存
│   ├── foc/                      # 电压/电流 FOC 与统一 SVPWM
│   ├── ipc/                      # 固定容量共享内存原语
│   ├── nvs/                      # 非易失键值存储
│   ├── link/                     # Link 通信协议栈
│   └── ble/goodix/               # Goodix BLE 适配
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
│   ├── APP_LAYOUT.md             # app/product 产品目录约定
│   ├── ARCHITECTURE.md           # 架构边界说明
│   ├── BUILD_SYSTEM.md           # DTS/Kconfig/CMake 与严格链接约定
│   ├── BOOT_FLOW.md              # Boot 启动与升级关系
│   ├── BOOTLOADER.md             # Bootloader 构建与目录约定
│   ├── INITCALL.md               # initcall 自动初始化
│   ├── DEVICE_TRAIT.md           # DeviceTrait 编译期分发
│   ├── DRIVER_MODEL.md           # 设备驱动模型与依赖约束
│   ├── DRIVER_DESIGN.md          # 驱动设计与扩展指南
│   ├── FOC.md                    # FOC 电机控制组件
│   ├── BLE.md                    # BLE 蓝牙组件
│   ├── LINK.md                   # Link 通信协议栈
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
# 配置（未指定 -Dt 时默认 armgcc9）
cmake -B out -GNinja -Dp=demo

# 编译
ninja -C out

# 产物
ls out/demo.elf  out/demo.bin  out/demo.hex
```

Bootloader 和三合一固件：

```bash
# 单独构建 loader
cmake -S . -B out/demo_3in1/loader -GNinja -Dp=demo -DFIRMWARE_TYPE=loader
ninja -C out/demo_3in1/loader

# 构建 preloader + loader + app 并合并三合一固件
python bootloader/scripts/build_3in1.py demo --out out/demo_3in1
```

详见 [doc/QUICKSTART.md](doc/QUICKSTART.md)
