# FOC 电机控制组件

## 概述

SDK 提供开箱即用的 FOC（磁场定向控制）电机控制组件，基于 PWM + ADC 驱动构建。

## 快速开始

```cpp
#include <device.h>
#include <drivers_generated.h>
#include "foc/motor_device.h"

void foc_app_init() {
    auto &motor = device_get(motor0);  // 从设备树获取已初始化的电机设备
    motor.start();                      // 启动 FOC 控制
}
```

`device_get(motor0)` 返回的 MotorDevice 已在 initcall 阶段自动完成：
- PWM/ADC 设备绑定（phandle 引用）
- 电机参数配置（从 DTS 属性解析）
- PI 控制器初始化

## 特性

- **空间矢量调制（SVPWM）** — 三相 PWM 输出，20kHz 默认频率
- **电流采样** — ADC 同步采样，支持内嵌（inline）和低侧（low-side）模式
- **CLI 调试** — UART 命令行，实时调节参数
- **可配置** — Kconfig 选择 PWM 频率、电机参数、调试选项

## 设备树配置

```dts
aliases {
    motor0 = &motor0;
    pwm0   = &pwm0;
    adc0   = &adc0;
};

/* PWM 定时器 */
pwm0: pwm@40012C00 {
    compatible = "goodix,gr5525-pwm";
    reg = <0x40012C00 0x400>;
    status = "okay";
};

/* ADC */
adc0: adc@40022800 {
    compatible = "goodix,gr5525-adc";
    reg = <0x40022800 0x400>;
    status = "okay";
};

/* FOC 电机控制器（复合设备） */
motor0: motor {
    compatible = "foc,motor";
    status = "okay";

    /* 引用 PWM 和 ADC 设备 */
    pwm-dev = <&pwm0>;
    adc-dev = <&adc0>;

    /* PWM 通道映射 */
    pwm-ch-u = <0>;               /* U 相 PWM 通道 */
    pwm-ch-v = <1>;               /* V 相 PWM 通道 */
    pwm-ch-w = <2>;               /* W 相 PWM 通道 */

    /* 电机物理参数（工程单位，整数） */
    rs-milliohm = <50>;           /* 定子电阻 (mΩ) */
    ld-microhenry = <100>;        /* D 轴电感 (μH) */
    lq-microhenry = <100>;        /* Q 轴电感 (μH) */
    flux-milliweber = <0>;        /* 磁链 (mWb) */
    pole-pairs = <7>;             /* 极对数 */

    /* FOC 限制参数 */
    max-current-ma = <20000>;     /* 最大相电流 (mA) */
    max-voltage-mv = <48000>;     /* 最大母线电压 (mV) */

    /* FOC 控制参数 */
    pwm-frequency = <20000>;      /* PWM 开关频率 (Hz) */
    current-bandwidth = <1000>;   /* 电流环带宽 (Hz) */

    /* 模式选择 */
    sensor-mode = <0>;            /* 0=无感, 1=Hall, 2=开环, 3=编码器 */
    control-mode = <1>;           /* 0=力矩, 1=速度, 2=占空比, 3=位置 */
};
```

> **改电机参数？只改 DTS，业务代码零修改。**

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
