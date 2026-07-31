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

## 和 Zephyr 的取舍

Zephyr 的设备模型提供了成熟的依赖、生命周期和诊断能力，但完整的 `struct device`
运行时模型会引入更多间接层。本项目只吸收其中的依赖检查、初始化顺序校验和诊断报告，
不照搬运行时 API 表。

和 Zephyr 一样，设备依赖来自 DTS/binding，而不是业务代码手工装配总线指针。不同的是，
RTOS SDK 通过 `DeviceTrait<Ord>` 在编译期生成强类型设备实例；I2C/SPI/ADC/PWM 等依赖
从 DTS phandle 推导并由生成器注入。

应用层仍应避免扩散硬件 alias。产品可以用 `board_devices` 作为板级门面，把 `uart0`、
`motor0`、`led0` 等别名集中到少数文件内，对业务层暴露 `console()`、`main_motor()`、
`status_led()` 等产品语义接口。

## 设备状态契约

设备就绪状态按三类处理：

1. 继承 `hal::DeviceBase` 的设备，使用 `is_ready()`。
2. 未继承 `DeviceBase` 但提供 `is_initialized()` 的设备，使用 `is_initialized()`。
3. GPIO pin 这类轻量编译期门面没有运行期状态，默认视为 always-ready。

`DeviceBase` 的状态语义是显式的：`is_initialized()` 只在 `Initialized` 或 `Open` 时为真；
`is_ready()` 还要求 `last_error() == Status::Ok`。驱动调用 `set_error()` 后会进入 `Error`
状态，诊断表和上层代码都不能把它当成可用设备。

新增驱动时优先遵守以下规则：

- 有显式初始化动作的驱动，应提供 `init()`、`deinit()` 和
  `is_initialized()`；
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

生成器会把依赖解析为设备 ordinal，并检查依赖设备是否早于当前设备初始化。生成器负责
校验，但不自动重排初始化顺序。依赖顺序错误会在配置阶段作为构建错误退出，需要显式调整
binding 或 DTS 中的 `init-level` / `init-priority`。

C++ adapter 使用 devicetree 宏取得依赖对象：

- `DT_ORD(DT_PHANDLE(node_id, prop))`：取得 phandle 设备 ordinal。
- `DT_ORD(DT_PARENT(node_id))`：取得父总线 ordinal。
- `DT_REG_ADDR(DT_PARENT(node_id))`：仅在确实需要父节点寄存器地址时使用。

`requires` 负责初始化顺序校验，adapter 负责 C++ 类型装配，两者应描述同一组
运行期依赖。

## 诊断报告

构建阶段会从 `edt.pickle` 在构建输出目录生成 `devices.json`。报告包含：

- DTS ordinal
- alias
- node id
- compatible
- C++ 类型
- init level
- init priority
- readiness 策略
- 解析到的依赖列表
- 每个中断的名称、索引、IRQ、优先级、控制器和分发方法
- 间接中断的 `source` 路径以及初始化后是否使能

该文件是生成产物，不提交到仓库。它用于 review 设备模型、定位初始化顺序
问题和支持后续 CLI/测试工具扩展。报告不再混入 pinctrl 数据；pinctrl 由
独立的 `gen_pinctrl.py` 负责。

## 产品门面

产品目录可以提供 `board_devices.h/.cc`，集中管理设备别名：

```cpp
namespace demo::board {

decltype(device_get(uart0)) console();
decltype(device_get(motor0)) main_motor();
decltype(device_get(led0)) status_led();

} // namespace demo::board
```

## 当前模型约束

- `DeviceBase::is_ready()` 对 `Error` 状态恒为 false，设备失败后必须通过错误态暴露。
- `gen_device_traits.py` 会把初始化依赖顺序问题视为构建错误，避免问题推迟到运行期。
- 中断必须通过 `cxx-driver.interrupts` 声明。IRQ 和优先级来自 EDT，不允许 adapter
  或生成器硬编码。
- 外设使用 DMA 中断时，binding 通过 `source.phandle-array`、`entry` 和
  `interrupt-index-cell` 跟随 `dmas` 到 DMA 控制器；驱动只接收解析后的 DMA
  controller/channel/request 配置。
- 使用 OSAL ISR API 的中断会对
  `osal::kMinRtosCallableIrqPriority` 做编译期优先级检查。
- 生成器统一负责 IRQ 包装器、平台连接和失败回滚，驱动只实现实例 ISR 方法。
- 驱动源码禁止声明 `IRQn_Handler`/`*_IRQHandler`，该约束由生成器单元测试扫描。
- 总线子设备通过 `DT_ORD(DT_PARENT(node_id))` 和
  `device_get<BusOrd>()` 访问父总线实例。
- `DT_REG_ADDR(DT_PARENT(node_id))` 只表示父节点寄存器基地址，不能替代
  父设备对象。

应用代码优先使用产品语义接口。只有板级门面和极少数底层适配文件应该直接调用
`device_get(alias)`。
