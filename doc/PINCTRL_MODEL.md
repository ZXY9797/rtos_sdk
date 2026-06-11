# pinctrl 模型说明

本项目的 pinctrl 模型用于把板级 IO 初始化从外设驱动和业务代码中移出，统一放到 DTS 描述和 initcall 流程中。

## 设计目标

1. 板级引脚事实只写在 DTS 中，包括端口、引脚、复用功能、上下拉、输出类型和默认输出电平。
2. SoC 差异只放在 `embedded/include/dt-bindings/pinctrl` 下的宏里。
3. 运行时代码保持通用，只执行 DTS 宏展开后的 MMIO 操作，不感知 GD32、STM32 等具体寄存器布局。
4. `gen_device_traits.py` 解析所有启用节点的 `pinctrl-0`，生成独立的 `pinctrl_generated.cc`。
5. `pinctrl_generated.cc` 注册 `PRE_KERNEL_1`、优先级 5 的 initcall，确保 IO 初始化早于 UART/SPI/PWM/ADC 等设备驱动。

## 通用操作格式

SoC 宏最终展开为 4 个 cell 一组的通用操作：

```dts
<type address clear_mask set_value>
```

含义：

- `PINCTRL_OP_RMW`：执行 `reg = (reg & ~clear_mask) | set_value`
- `PINCTRL_OP_WRITE`：执行 `reg = set_value`

公共运行时只理解这两类操作。新增 SoC 时不需要增加 `embedded/drivers/pinctrl/pinctrl_<soc>.cc`，只需要新增对应的 `dt-bindings/pinctrl/<soc>-pinctrl.h` 宏，把该 SoC 的寄存器地址、位移、mask 和字段值展开成通用操作。

## GD32 DTS 写法

GD32 使用 `GD32_PINMUX_*` 宏生成通用操作流。例如 USART0：

```dts
pinctrl: pinctrl {
    compatible = "gd,gd32-pinctrl";

    usart0_tx_pa9: usart0_tx_pa9 {
        pinmux = <GD32_PINMUX_AF(GD32_PORT_A, 9, 7,
                                  GD32_PULL_NONE,
                                  GD32_DRIVE_PUSH_PULL, 3)>;
    };

    usart0_rx_pa10: usart0_rx_pa10 {
        pinmux = <GD32_PINMUX_AF(GD32_PORT_A, 10, 7,
                                  GD32_PULL_UP,
                                  GD32_DRIVE_PUSH_PULL, 3)>;
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
- `pinctrl_generated.cc`：默认 pinctrl 操作表和 pinctrl initcall。

普通 app 同时编译 `drivers_generated.cc` 和 `pinctrl_generated.cc`。loader/upgrade 的边界按固件实际职责裁剪：

- 只需要把通信管脚切到默认状态时，只编译 `pinctrl_generated.cc`。
- loader 需要 UART/SPI/Flash 等通信设备模型时，可以同时编译 `drivers_generated.cc`；生成脚本只会为当前 loader DTS 中 `status = "okay"` 的节点生成实例。
- 不属于 loader 的 ADC/PWM/Motor/IMU 等外设不要放到 loader DTS，或者保持 `status = "disabled"`，这样不会生成对应设备实例、OSAL 缓冲区和设备 initcall。

因此裁剪边界不是“loader 永远不编译 `drivers_generated.cc`”，而是“每个固件用自己的 DTS/Kconfig 精确描述需要的设备”。Kconfig 控制某类驱动是否进入构建，DTS 控制当前固件生成哪些设备对象。

## 当前接入范围

demo app 当前接入 USART0、LED PA5、KEY PA0。loader 和 upgrade 当前接入 USART0 PA9/PA10。SPI、PWM、ADC 等引脚需要以实际板级走线为准；当前 DTS 中 LED PA5 与常见 SPI0 SCK PA5 存在潜在冲突，所以没有强行给 SPI0 增加默认 pinctrl。
