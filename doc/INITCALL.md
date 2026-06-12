# initcall 自动初始化机制

## 概述

设备树中 `status = "okay"` 的节点，启动时自动实例化并初始化。业务层通过 `device_get()` 获取已初始化的设备引用，无需手动初始化代码。

## 使用方式

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

## 设备树配置

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

## initcall 链路

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
任意 initcall 返回非 0 → 停止后续启动
        ↓
main()：auto &uart = device_get(uart0);  // 直接用
```

## initcall 级别

| 级别 | 值 | 用途 |
|:---|:---:|:---|
| `EARLY` | 0 | 架构最早期初始化 |
| `PRE_KERNEL_1` | 1 | 基础硬件和故障记录等早期组件 |
| `PRE_KERNEL_2` | 2 | 依赖基础服务的驱动 |
| `PRE_KERNEL_3` | 3 | RTOS 启动前的较晚驱动 |
| `POST_KERNEL` | 4 | OSAL 启动后、应用前 |
| `APPLICATION` | 5 | 应用层初始化 |

```cpp
// 注册自定义 initcall
SYS_INIT(my_init, INITCALL_LEVEL_PRE_KERNEL_2, 30);
```

## 优先级

同一级别内，priority 值越小越先执行。驱动默认 priority 为 25，应用可使用 30+ 避免冲突。

## 错误处理

`SYS_INIT` 注册函数返回 `int`，`0` 表示成功，非 0 表示初始化失败。生成的设备初始化包装函数会把
`driver.init(cfg)` 的返回值转换为 `int` 并向上传递。

`EARLY` 到 `PRE_KERNEL_3` 的错误会让 `z_cstart()` 停在错误点，不再启动 OSAL。`POST_KERNEL` 和
`APPLICATION` 的错误会让后台主线程停在错误点，不再进入 `main()`。
