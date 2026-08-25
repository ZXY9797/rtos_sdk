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
    interrupts = <37 6>;
    interrupt-names = "global";
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
gen_device_traits.py 读取 EDT pickle + YAML adapter 元数据
        ↓
展开 C++ adapter（adapter 从 DTS 构造 DeviceTrait 和配置）
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

// 资源型初始化必须提供幂等回滚；失败项本身也会被回滚
SYS_INIT_ROLLBACK(my_init, my_deinit,
                  INITCALL_LEVEL_PRE_KERNEL_2, 30);
```

## 优先级

同一级别内，priority 值越小越先执行。驱动默认 priority 为 25，应用可使用 30+ 避免冲突。
同一级别、同一 priority 的条目由链接脚本按完整 section 名排序；`SYS_INIT` 的 section
后缀使用函数名，因此相同输入在不同构建中顺序稳定。依赖关系仍不得依赖同优先级的字典序，
有真实先后约束时必须配置不同 priority，并由设备依赖检查器验证。

## 错误处理

`SYS_INIT` 注册函数返回 `int`，`0` 表示成功，非 0 表示初始化失败。生成的设备初始化包装函数会把
adapter 的 `DeviceTrait<Ord>::init()` 返回值作为 `int` 向上传递。

`EARLY` 到 `PRE_KERNEL_3` 的错误会让 `z_cstart()` 停在错误点，不再启动 OSAL。`POST_KERNEL` 和
`APPLICATION` 的错误会让后台主线程停在错误点，不再进入 `main()`。

初始化失败时，框架从失败项开始按逆序调用已注册的 rollback；回滚函数必须幂等、
不得假设初始化已经完整成功。首个初始化错误和首个回滚错误分别保存在
`g_init_failed_*` 与 `g_init_rollback_failed_*`，便于调试器无日志读取。

`osal_init()` 或 `osal_start()` 失败时会回滚 pre-kernel 项并停止。调度器成功启动后
按契约不应返回；如果返回，即使返回值为 0 也按致命错误处理，不会继续执行未知状态
的固件。

所有错误停机路径先写诊断变量，再执行 `bkpt` 并保持停机。产品看门狗和安全输出应
在独立硬件/早期初始化策略中保证，不能依赖错误路径继续运行应用代码。

fault/log 等固定容量注册表必须在其 initcall 阶段冻结。冻结后注册返回失败，避免运行期
遍历期间改变回调数组；调用方必须检查注册返回值。
