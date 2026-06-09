# FOC 电机控制组件

## 概述

SDK 提供开箱即用的 FOC（磁场定向控制）电机控制组件，基于 PWM + ADC 驱动构建。

## 快速开始

```cpp
#include "foc_app.h"

int main(void) {
    foc_app::start();  // 启动 FOC 系统（PWM 20kHz + ADC 采样 + CLI 调试）
}
```

## 特性

- **空间矢量调制（SVPWM）** — 三相 PWM 输出，20kHz 默认频率
- **电流采样** — ADC 同步采样，支持内嵌（inline）和低侧（low-side）模式
- **CLI 调试** — UART 命令行，实时调节参数
- **可配置** — Kconfig 选择 PWM 频率、电机参数、调试选项

## 设备树配置

```dts
aliases {
    pwm_u = &timer0_ch0;
    pwm_v = &timer0_ch1;
    pwm_w = &timer0_ch2;
    adc0  = &adc0;
};
```

## Kconfig 配置

```kconfig
# FOC 组件配置
CONFIG_FOC_PWM_FREQUENCY=20000    # PWM 频率（Hz）
CONFIG_FOC_ADC_INLINE=y           # 内嵌电流采样
CONFIG_FOC_CLI_ENABLE=y           # 启用 CLI 调试
```

## 目录结构

```
component/foc/
├── include/
│   ├── foc_app.h           # 应用层接口
│   ├── foc_core.h          # FOC 核心算法
│   ├── foc_svpwm.h         # SVPWM 调制
│   └── foc_adc.h           # 电流采样
├── src/
│   ├── foc_app.cc          # 应用实现
│   ├── foc_core.cc         # Clark/Park 变换
│   ├── foc_svpwm.cc        # SVPWM 实现
│   ├── foc_adc.cc          # ADC 采样实现
│   └── foc_cli.cc          # CLI 命令处理
└── Kconfig                 # 配置选项
```

## CLI 命令

通过 UART 连接后可使用以下命令：

| 命令 | 说明 |
|:---|:---|
| `start` | 启动电机 |
| `stop` | 停止电机 |
| `speed <rpm>` | 设置目标转速 |
| `kp <value>` | 设置 Kp 参数 |
| `ki <value>` | 设置 Ki 参数 |
| `status` | 显示当前状态 |

## 架构

```
foc_app（应用层）
    ↓
foc_core（FOC 算法：Clark/Park/PI）
    ↓
foc_svpwm（SVPWM 调制）  ←→  PWM 驱动
    ↓
foc_adc（电流采样）       ←→  ADC 驱动
```

> **FOC 组件位于 `component/foc/`，可独立于应用复用。**
