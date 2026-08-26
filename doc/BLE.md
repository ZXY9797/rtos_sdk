# BLE 蓝牙组件

## 概述

SDK 提供厂商无关的 BLE API（`ble::` 命名空间），当前实现基于 Goodix GR5525。

## 快速开始

```cpp
#include <device.h>
#include <drivers_generated.h>
#include "ble/ble_device.h"
#include "ble/ble_hid.h"

static ble::BleStack *s_ble = nullptr;
static ble::BleHidService s_hid;

static void on_event(const ble::Event &evt, void *) {
    if (evt.id == ble::EventId::StackInit) {
        s_hid.init_keyboard({report_map, sizeof(report_map)});
        s_ble->adv_start();
    }
}

void ble_app_init() {
    auto &ble_dev = device_get(ble0);       // 从设备树获取 BleDevice
    s_ble = &ble_dev.stack();
    (void)s_ble->set_adv_data(adv_data, sizeof(adv_data));
    ble_dev.init(on_event, nullptr);         // 用 DTS 配置 + 应用回调启动协议栈
}
```

## 两阶段初始化

BLE 设备采用两阶段初始化，区别于普通驱动的自动初始化：

| 阶段 | 调用者 | 行为 |
|:---|:---|:---|
| initcall（自动） | `SYS_INIT` | 存储 DTS 配置到 `StackConfig`，不启动协议栈 |
| 应用层 | `init(callback)` | 用存储的配置 + 应用回调启动 BLE 协议栈 |

这是因为 BLE 需要应用层事件回调（连接/断开/数据），无法完全自动初始化。

## API 模块

| 模块 | 说明 |
|:---|:---|
| `ble::BleDevice` | 设备树包装类，两阶段 init |
| `ble::BleStack` | 协议栈初始化、广播、连接管理 |
| `ble::BleHidService` | HID over GATT（键盘/消费者控制） |
| `ble::BleUartService` | UART 透传（Goodix UART Service，GUS） |
| `ble::BleBattService` | 电池电量服务 |
| `ble::BleDisService` | 设备信息服务 |

## 设备树配置

```dts
aliases {
    ble0 = &ble0;
};

ble0: ble {
    compatible = "goodix,gr5525-ble";
    status = "okay";

    /* 广播参数 */
    device-name = "GR5525_BLE";     /* 广播名称 */
    adv-interval-min = <48>;        /* 最小广播间隔，0.625ms 单位（30ms） */
    adv-interval-max = <80>;        /* 最大广播间隔，0.625ms 单位（50ms） */

    /* 连接参数 */
    conn-interval-min = <320>;      /* 最小连接间隔，1.25ms 单位（400ms） */
    conn-interval-max = <520>;      /* 最大连接间隔，1.25ms 单位（650ms） */
    slave-latency = <0>;            /* 从机延迟 */
    sup-timeout = <400>;            /* 监督超时，10ms 单位（4s） */

    /* 安全参数 */
    security-level = <2>;           /* 0=None, 1=Open, 2=Mitm, 3=SecureConn */
    bonding = <1>;                  /* 0=禁用绑定, 1=启用绑定 */
};
```

## Kconfig 配置

```kconfig
# BLE 协议栈编译时参数（映射到 GR5525 SDK 的 CFG_* 宏）
CONFIG_BLE_MAX_CONNECTIONS=1        # 最大连接数
CONFIG_BLE_MAX_ADVS=1               # 最大广播实例数
CONFIG_BLE_MAX_SCAN=0               # 最大扫描实例数
CONFIG_BLE_MAX_BOND_DEVS=1          # 最大绑定设备数
CONFIG_BLE_MAX_PRFS=8               # 最大 Profile 数
CONFIG_BLE_CONTROLLER_ONLY=n        # 仅控制器模式

# BLE 服务选择
CONFIG_BLE_HID=y                    # HID 服务
CONFIG_BLE_UART_SERVICE=y           # UART 透传服务
CONFIG_BLE_BATTERY_SERVICE=y        # 电池服务
CONFIG_BLE_DEVICE_INFO=y            # 设备信息服务

# demo_ble 命令 owner
CONFIG_BLE_TASK_STACK_SIZE=2048      # BLE owner 静态栈，字节
CONFIG_BLE_EVENT_QUEUE_DEPTH=8       # ISR 到 owner 的延迟事件深度
CONFIG_BLE_COMMAND_QUEUE_DEPTH=8     # 多生产者命令队列深度
CONFIG_BLE_COMMAND_TIMEOUT_MS=250    # Busy 重试总时限

# GR5525 启动/安全
CONFIG_GR5525_BOOT_CHECK_IMAGE=y    # 启动时校验镜像
CONFIG_GR5525_SECURITY_CFG=0        # 安全配置级别
```

Kconfig 选项通过 `custom_config.h` 桥接到 GR5525 SDK 期望的 `CFG_*` 宏名。服务选项还会
控制对应 wrapper 和 profile 源文件是否进入目标；关闭服务不会留下空实现或伪配置。
Goodix BLE 内存由匹配版本的厂商协议栈和连接/Profile 上限共同决定，框架不暴露无法
兑现的独立 `BLE_HEAP_SIZE` 选项。

## RTOS 并发模型

- Goodix 协议栈由厂商软件中断驱动；RTOS 应用不得轮询仅适用于非 RTOS 主循环的
  `pwr_mgmt_schedule()`。
- HID、UART 等业务线程只向固定容量命令队列提交请求，由唯一 BLE owner 调用 GATT
  service API；`Busy` 只在配置的总时限内重试，超时、队列满和非法参数都会计入统计。
- BLE 软件中断只执行厂商要求的协议栈/Profile 操作；连接状态、GUS RX、日志和镜像确认
  通过固定容量 SPSC 事件环延迟到 BLE owner。关键生命周期事件溢出会进入 fatal，普通
  RX/配对通知溢出会记录丢弃计数。
- 按键 ISR 只确认边沿并释放信号量，消抖和 HID 报告在任务上下文完成；UART RX 任务
  阻塞等待流缓冲区，不做毫秒级轮询。
- 量产诊断周期读取 OS heap 最低余量、关键线程最小剩余栈、BLE 队列高水位和失败计数。
  这些接口用于板级长稳定容，构建成功不能替代射频、IRQ、并发和栈水位实测。

## 架构

```
app/product/demo_ble/       应用层（ISR 事件环 + 静态命令队列 + 单 BLE owner）
    ↓ device_get(ble0).init(callback)
component/ble/include       BleDevice 包装 + ble:: API
    ↓
component/ble/goodix        GR5525 适配层 + libble_sdk.a
    ↓
embedded/soc                GR5525 HAL（BLE 控制器 ROM + 射频）
```

## 目录结构

```
component/ble/
├── include/
│   └── ble/
│       ├── ble_device.h        # BleDevice 包装类（两阶段 init）
│       ├── ble_stack.h         # 协议栈接口
│       ├── ble_hid.h           # HID 服务接口
│       ├── ble_uart.h          # UART 透传接口
│       ├── ble_batt.h          # 电池服务接口
│       └── ble_dis.h           # 设备信息服务接口
└── goodix/
    ├── src/
    │   ├── ble_stack_impl.cc   # GR5525 协议栈适配
    │   ├── ble_hid_impl.cc     # HID 服务实现
    │   └── ble_uart_impl.cc    # UART 透传实现
    ├── Kconfig                 # BLE 编译时配置
    ├── profiles/               # GATT Profile 实现
    ├── libraries/              # BLE SDK 辅助库
    └── lib/libble_sdk.a        # Goodix 预编译 BLE 协议栈原件

embedded/dts/bindings/ble/
└── goodix,gr5525-ble.yaml      # BLE 设备树绑定

embedded/soc/goodix/gr5525x/include/
└── custom_config.h             # Kconfig → GR5525 SDK 宏桥接
```

配置阶段会校验 `libble_sdk.a` 与 SDK `v1.0.3_patch_2` ROM symbol 表的
SHA-256。构建系统仅在输出目录创建 archive 副本并移除厂商示例 `main.o`，不修改
仓库中的原始库。所有 ROM 调用必须由匹配的 symbol 表解析，最终 ELF 不允许残留
未解析符号。

## 移植指南

要将 BLE API 移植到其他芯片：

1. 实现 `ble::BleStack` 接口（协议栈初始化、广播、连接）
2. 实现各服务接口（HID、UART、BATT、DIS）
3. 创建 binding YAML 声明 BLE 控制器

> **BLE 组件位于 `component/ble/`，API 接口可移植到其他 BLE 芯片。**
