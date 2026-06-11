# pinctrl 模型说明

本项目新增轻量级 pinctrl 模型，用于把板级 IO 初始化从外设驱动和业务代码中移出，统一放到 DTS 描述和 initcall 流程中。

## 设计目标

1. 板级引脚事实只写在 DTS 中，包括端口、引脚、复用功能、上下拉、输出类型和默认输出电平。
2. `gen_device_traits.py` 解析所有启用节点的 `pinctrl-0`，生成独立的 `pinctrl_generated.cc`。
3. `pinctrl_generated.cc` 注册 `PRE_KERNEL_1`、优先级 5 的 initcall，确保 IO 初始化早于 UART/SPI/PWM/ADC 等设备驱动。
4. app、loader、upgrade 都可以只编译 pinctrl 生成源；loader/upgrade 不需要把完整设备实例生成源拉入启动链路。
5. pinctrl 运行时只暴露 `hal::pinctrl::apply()`，芯片相关的 pinmux 解码留在具体 SoC 驱动中。

## GD32 DTS 写法

GD32 使用 `GD32_PINMUX_GPIOx(pin, af)` 编码端口、引脚和 AF 号。例如 USART0：

```dts
pinctrl: pinctrl {
    compatible = "gd,gd32-pinctrl";

    usart0_tx_pa9: usart0_tx_pa9 {
        pinmux = <GD32_PINMUX_GPIOA(9, 7)>;
        drive-push-pull;
        slew-rate = <3>;
    };

    usart0_rx_pa10: usart0_rx_pa10 {
        pinmux = <GD32_PINMUX_GPIOA(10, 7)>;
        bias-pull-up;
        slew-rate = <3>;
    };
};

&usart0 {
    pinctrl-0 = <&usart0_tx_pa9 &usart0_rx_pa10>;
    pinctrl-names = "default";
};
```

GPIO 默认状态也可以由 pinctrl 完成，例如 LED 输出低、按键输入上拉。这样应用侧可以直接使用 `device_get(led0)` 后的 GPIO 门面，不再额外写一次板级 IO 初始化。

## 生成和构建

配置阶段会生成三个文件：

- `drivers_generated.h`：设备类型特化和 `device_get(alias)`。
- `drivers_generated.cc`：设备实例和设备 initcall。
- `pinctrl_generated.cc`：默认 pinctrl 配置表和 pinctrl initcall。

普通 app 同时编译 `drivers_generated.cc` 和 `pinctrl_generated.cc`。loader/upgrade 只编译 `pinctrl_generated.cc`，避免因为只需要 IO 初始化而引入完整 UART 设备实例、OSAL 缓冲区和设备 initcall。

## 当前接入范围

demo app 当前接入 USART0、LED PA5、KEY PA0。loader 和 upgrade 当前接入 USART0 PA9/PA10。SPI、PWM、ADC 等引脚需要以实际板级走线为准；当前 DTS 中 LED PA5 与常见 SPI0 SCK PA5 存在潜在冲突，所以本次没有强行给 SPI0 增加默认 pinctrl。
