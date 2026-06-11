# 设备驱动模型实施说明

本项目继续采用 `DTS + binding + DeviceTrait + device_get(alias)` 的设备模型。
设备实例由设备树和生成器决定，应用代码通过编译期别名拿到强类型对象，不引入 Zephyr
完整的运行时 `struct device` 分发表。

## 设计目标

1. 硬件事实只写在 DTS 和 binding 中。
2. C++ 驱动类型由 `cxx-driver` 绑定到设备树节点。
3. `device_get(alias)` 保持零运行时查找和强类型返回。
4. 运行期只保留必要的设备注册表，用于日志、CLI 和诊断。
5. 产品代码逐步通过板级门面访问设备，减少 `uart0`、`motor0` 这类硬件别名在业务逻辑中扩散。

## 和其他工程的取舍

Zephyr 的设备模型提供了成熟的依赖、生命周期和诊断能力，但完整的 `struct device`
运行时模型会引入更多间接层。本项目只吸收其中的依赖检查、初始化顺序校验和诊断报告，
不照搬运行时 API 表。

`D:\code\rtos_sdk` 的 HAL/driver 分层清晰，但芯片驱动常通过构造函数手工接收总线指针。
本项目不采用这种手工装配方式，I2C/SPI/ADC/PWM 等依赖应从 DTS phandle 推导并由生成器注入。

`D:\code\zhaoming_emb\zhaoming_embedded` 强调应用层不认识硬件。本项目用 `board_devices`
作为产品级门面，把硬件 alias 集中到一个文件内，应用逻辑只使用 `console()`、
`main_motor()`、`status_led()` 等产品语义接口。

## 设备状态契约

设备就绪状态按三类处理：

1. 继承 `hal::DeviceBase` 的设备，使用 `is_ready()`。
2. 未继承 `DeviceBase` 但提供 `is_initialized()` 的设备，使用 `is_initialized()`。
3. GPIO pin 这类轻量编译期门面没有运行期状态，默认视为 always-ready。

新增驱动时优先遵守以下规则：

- 有 `init-cfg` 或显式初始化动作的驱动，应提供 `init()`、`deinit()` 和 `is_initialized()`；
- 需要统一状态、名称、错误态的驱动，应继承 `DeviceBase`；
- 无状态门面必须在 binding 或文档中说明其就绪语义；
- 所有驱动公共接口优先返回 `hal::Status`，避免混用裸 `int` 表达硬件错误。

## 设备依赖

binding 的 `cxx-driver.requires` 描述初始化依赖。生成器支持以下写法：

```yaml
cxx-driver:
  requires:
    - parent
    - phandle: clocks
    - phandle-array: dmas
    - phandle-array: gpios
```

生成器会把依赖解析为设备 ordinal，并检查依赖设备是否早于当前设备初始化。当前阶段只做
校验和报告，不自动重排初始化顺序，避免隐藏行为变化。

## 诊断报告

配置阶段会在构建输出目录生成 `devices.json`。报告包含：

- DTS ordinal
- alias
- node id
- compatible
- C++ 类型
- init level
- init priority
- readiness 策略
- 解析到的依赖列表

该文件是生成产物，不提交到仓库。它用于 review 设备模型、定位初始化顺序问题和支持后续
CLI/测试工具扩展。

## 产品门面

产品目录可以提供 `board_devices.h/.cc`，集中管理设备别名：

```cpp
namespace demo::board {

decltype(device_get(uart0)) console();
decltype(device_get(motor0)) main_motor();
decltype(device_get(led0)) status_led();

} // namespace demo::board
```

应用代码优先使用产品语义接口。只有板级门面和极少数底层适配文件应该直接调用
`device_get(alias)`。
