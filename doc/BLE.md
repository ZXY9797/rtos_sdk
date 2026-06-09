# BLE 蓝牙组件

## 概述

SDK 提供厂商无关的 BLE API（`ble::` 命名空间），当前实现基于 Goodix GR5525。

## 快速开始

```cpp
#include "ble/ble_stack.h"
#include "ble/ble_hid.h"

static ble::BleStack s_ble;
static ble::BleHidService s_hid;

static void on_event(const ble::Event &evt, void *) {
    if (evt.id == ble::EventId::StackInit) {
        s_hid.init_keyboard({report_map, sizeof(report_map)});
        s_ble.adv_start();
    }
}

void ble_app_init() {
    ble::StackConfig cfg{};
    cfg.device_name = "GR5525_BLE";
    cfg.adv_interval_min = 48;
    s_ble.init(cfg, on_event, nullptr);
}
```

## API 模块

| 模块 | 说明 |
|:---|:---|
| `ble::BleStack` | 协议栈初始化、广播、连接管理 |
| `ble::BleHidService` | HID over GATT（键盘/消费者控制） |
| `ble::BleUartService` | UART 透传（NUS） |
| `ble::BleBattService` | 电池电量服务 |
| `ble::BleDisService` | 设备信息服务 |

## 架构

```
app/demo_ble/          应用层（HID + UART 透传）
    ↓
component/ble/include  厂商无关 ble:: API
    ↓
component/ble/goodix   GR5525 适配层 + libble_sdk.a
    ↓
embedded/soc           GR5525 HAL（BLE 控制器 ROM + 射频）
```

## 目录结构

```
component/ble/
├── include/
│   ├── ble_stack.h         # 协议栈接口
│   ├── ble_hid.h           # HID 服务接口
│   ├── ble_uart.h          # UART 透传接口
│   ├── ble_batt.h          # 电池服务接口
│   └── ble_dis.h           # 设备信息服务接口
└── goodix/
    ├── src/
    │   ├── ble_stack.cc    # GR5525 协议栈适配
    │   ├── ble_hid.cc      # HID 服务实现
    │   └── ble_uart.cc     # UART 透传实现
    ├── profiles/           # GATT Profile 实现
    ├── libraries/          # BLE SDK 辅助库
    └── lib/libble_sdk.a    # Goodix 预编译 BLE 协议栈
```

## 设备树配置

```dts
/ {
    chosen {
        zephyr,ble = &ble0;
    };
};

ble0: ble@00000000 {
    compatible = "goodix,gr5525-ble";
    status = "okay";
};
```

## Kconfig 配置

```kconfig
CONFIG_BLE=y                  # 启用 BLE
CONFIG_BLE_HID=y              # 启用 HID 服务
CONFIG_BLE_UART=y             # 启用 UART 透传
CONFIG_BLE_BATT=y             # 启用电池服务
CONFIG_BLE_DIS=y              # 启用设备信息服务
```

## 移植指南

要将 BLE API 移植到其他芯片：

1. 实现 `ble::BleStack` 接口（协议栈初始化、广播、连接）
2. 实现各服务接口（HID、UART、BATT、DIS）
3. 在 binding YAML 中声明 BLE 控制器

> **BLE 组件位于 `component/ble/`，API 接口可移植到其他 BLE 芯片。**
