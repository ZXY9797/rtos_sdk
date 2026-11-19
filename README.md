# RTOS SDK

面向 MCU 实时控制，采用声明式硬件配置、静态资源和类型化组件的 C++ RTOS 开发框架。
仓库中的应用和板级配置用于演示 SDK 的构建、调试和运行。

独立应用、板卡、模块、执行预算、统一诊断、资源报告和验证入口见
[现代 RTOS 框架设计与使用](doc/MODERN_RTOS.md)。

板卡、App/Boot overlay 和各镜像 profile 的目录与加载规则见
[板卡与镜像配置](doc/BOARD_CONFIGURATION.md)。

源码保留范围、精简结果和后续导入规则见
[SDK 源码精简与维护](doc/SOURCE_SUBSET.md)。
Python 构建依赖统一通过 `python -m pip install -r tools/requirements-ci.txt` 安装。

> 产品化能力、必须提供的硬件适配和发布验收边界见
> [产品级 RTOS 架构基线](doc/PRODUCT_RTOS.md)。

<p align="center">
  <b>面向实时控制的 C++ 框架 | 声明式硬件 · 静态设备与资源 · 多内核 OSAL · 产品级 Bootloader</b>
</p>

---

## 为什么选择 RTOS SDK？

RTOS SDK 用 DTS/Kconfig 描述硬件和镜像配置，以编译期设备、类型化组件和固定容量资源
组织实时控制应用。FreeRTOS、RT-Thread 与 Baremetal 通过 OSAL 接入。厂商 SDK 和内核
上游包完整保留，构建按实际依赖选择性编译，并记录来源和内容摘要。

### 对比一览

| 特性 | Zephyr | RTOS SDK |
|:---|:---:|:---:|
| 构建系统 | CMake + Python + DTS/Kconfig | **CMake + 可测试的 Python 生成器 + DTS/Kconfig** |
| 设备树层级 | 5+ 级 `.dtsi` 继承 | **2 级**（SoC + Board） |
| 链接脚本 | 片段拼接 | **单一 `.ld` 文件** |
| 环境搭建 | West + Python + CMake + Ninja + DTC | **Python + CMake + GCC + Ninja** |
| 驱动模型 | 运行时 `struct device` + vtable | **编译期模板 + initcall + 依赖校验** |
| 内存管理 | 动态分配 + Slab + Heap | **实时/故障路径固定容量，初始化分配显式检查** |
| RTOS 绑定 | 仅支持 Zephyr 内核 | **OSAL 多内核**（FreeRTOS、RT-Thread） |
| SoC 支持 | 内置 400+ | **STM32H7、GD32F50x、Goodix GR5525** |
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
│  Parser/Worker · 静态路由 · 分阶段 ACK · 重放防护 · 诊断     │
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
| 二进制大小 | 按配置选择、链接裁剪 | **按配置生成具体设备类型、选择性链接** |
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

### 8. Link 通信协议栈 — 确定性多链路通信服务

支持 UART、CAN、BLE 等物理链路，提供解析/业务线程隔离、最长掩码静态路由、
多本机地址、分阶段 ACK、异步完成、超时重传、重放防护、分端口频率、优先级调度、远程诊断和
分片重组。所有核心队列与任务栈均静态分配，链路注册和协议任务通过 `SYS_INIT`
自动完成；协议版本保持 1，采用 4 位版本字段、CRC8 头校验和标准 CRC16 尾校验。
使用 BCC 和历史 CRC16 的旧 v1 对端需要配套更新。

详见 [doc/LINK.md](doc/LINK.md)

### 9. Bootloader — 分阶段启动 + DFU + loader 自升级

当前 boot 链路分为 `preloader`、`loader`、`upgrade` 和 `app`。`preloader` 是最小信任根，生产配置认证固定 loader；`loader` 负责 DFU、签名/防降级校验以及单执行槽的掉电安全 sector-swap 安装；`upgrade` 仅保留开发配置下的 loader 自升级执行；`app` 只通过公开 boot API 发布产品元数据和确认镜像。boot 控制日志和单 sector scratch 使用独立分区，不与 NVS storage 复用。首次启动未确认时，loader 会把保存在 `upgrade` 的旧应用交换回 `slot0`；这提供单次 trial 自动回滚，但不是两个可独立启动执行槽的 A/B 方案。

`boot_common` 提供 app、loader、preloader、upgrade 共享的镜像元数据、产品元数据、Flash 分区接口、镜像确认、SHA-256 和 DFU CRC。产品相关配置集中在 `app/product/<product>`：`config/layout.json` 定义共享分区，`boot/` 保存各 Boot 镜像的配置、链接脚本和产品适配；公共 bootloader 目录只保留跨产品实现、接口和脚本。

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
├── app/                          # 应用层
│   ├── product/                  # 产品固件和各自配置
│   │   ├── demo_foc/             # GD32 FOC 电机控制 Demo
│   │   ├── demo_ble/             # GR5525 BLE Demo（HID 键盘 + UART 透传）
│   │   └── gimbal/               # GD32 三轴云台
│   └── modules/                  # 应用共享模块
│       ├── motor/                # 电机控制、FOC、标定和位置反馈
│       ├── atti/                 # IMU 姿态估计
│       ├── supervision/          # 安全监督和热管理
│       └── common/               # 数值工具和数据契约
│
├── bootloader/                   # 启动与升级固件
│   ├── include/boot/             # 公开 boot API
│   ├── common/                   # app/loader/preloader/upgrade 共享实现
│   ├── loader/                   # 启动决策、DFU、app 校验与 sector-swap
│   ├── preloader/                # 第一阶段信任根与 loader 认证
│   ├── upgrade/                  # loader 自升级执行固件
│   └── scripts/                  # 三合一固件构建与合并脚本
│
├── component/                    # SDK 复用组件
│   ├── algo/                     # 通用数值、控制、姿态与温控算法
│   ├── motor/                    # 电机、关节、Hall 与 FOC 计算
│   │   └── drive/                # 电压和电流 FOC 的设备适配
│   ├── runtime/                  # 执行描述、时序测量、输出锁存与编码工具
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
│   ├── MOTOR_ALGORITHMS.md       # 电机算法、单位与标定接入
│   ├── MOTOR_CONTROL_DESIGN.md   # 阻抗/导纳/力控实现、坐标契约与验证
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
python tools/sdk.py configure -p demo_foc -B out/demo_foc
python tools/sdk.py build -B out/demo_foc
python tools/sdk.py inspect -B out/demo_foc --kind resources
python tools/sdk.py test --suite host
```

修改驱动、overlay 或 Kconfig 后，开发者与 AI 可用 `inspect --kind devices` 核对生成的设备依赖，
用 `inspect --kind build` 核对实际配置片段与镜像。固件产物位于 `out/demo_foc/`。

编辑器使用 clangd 时，先执行 `python tools/sdk.py inspect -B out/demo_foc --kind clangd`。
在 VS Code 的工作区设置 JSON 中，把输出的 `arguments` 数组填入 `clangd.arguments`，
保留已有的其他参数，再执行命令面板中的 `clangd: Restart language server`。
其他编辑器将同一参数加入 clangd 的启动参数。切换产品或 profile 时选择对应的构建目录，
索引即可使用该镜像的生成头文件与编译宏。

Bootloader 和三合一固件：

```bash
# 单独构建 loader
cmake -S . -B out/demo_foc_3in1/loader -GNinja -Dp=demo_foc -DFIRMWARE_TYPE=loader
ninja -C out/demo_foc_3in1/loader

# 构建 preloader + loader + app 并合并三合一固件
python bootloader/scripts/build_3in1.py demo_foc --out out/demo_foc_3in1
```

详见 [doc/QUICKSTART.md](doc/QUICKSTART.md)
