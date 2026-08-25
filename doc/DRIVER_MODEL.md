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

## 驱动分层

```text
service / application       产品行为、控制和通信编排
           ↓
board facade                main_can()、main_motor() 等产品语义接口
           ↓
DT adapter + DeviceTrait    DTS 属性到强类型 Config/实例的唯一转换点
           ↓
public HAL contract         drivers/*.h 中的厂商无关接口和状态语义
           ↓
SoC backend / vendor HAL    寄存器、厂商 HAL/LL、ROM/SDK ABI
```

依赖只能向下。公共 HAL 头文件不得包含厂商 HAL 类型；业务代码不得按寄存器地址
推导控制器编号，也不得绕过板级门面重新构造同一个外设对象。需要访问厂商原生
对象的 IRQ 适配代码，应从对应 DTS 实例的 `native()` 取得同一个底层对象。

后端启用条件必须同时满足：DTS 中存在 `status = "okay"` 的 compatible 节点，
并且 Kconfig 选择了该后端。后端 Kconfig 必须 `depends on
DT_HAS_<COMPATIBLE>_ENABLED`；CMake 只能在配置成立时创建/链接目标，不生成裸
`-l<driver>` 依赖。

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

生成的运行期注册表保存类型擦除的就绪检查，以及仅供统一电源管理使用的
`suspend/resume` 回调。普通诊断仍只读；只有同时实现成对 `suspend()`/`resume()` 的
设备才标记为 PM capable。`device_get()` 仍返回编译期强类型引用；注册表不参与正常
数据路径，也不能成为新的 service locator。

注册表按 init level、priority、ordinal 排序。`hal::suspend_devices()` 逆初始化顺序
挂起，失败时正向恢复已经挂起的设备；`hal::resume_devices()` 按初始化顺序恢复并记录
失败 ordinal。PM 回调必须有界、不可阻塞且可幂等重试；`suspend()` 还必须具备失败
原子性，返回失败时该设备仍处于 Active，不能把半配置状态交给框架回滚。tickless
provider 负责在真实睡眠前后调用这两个入口。

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
      lifecycle: external
    - phandle-array: gpios
      lifecycle: external
```

依赖默认 `lifecycle: generated`：目标必须有生成的 C++ 设备实例，并且初始化顺序早于
当前设备。只有当当前 adapter/厂商 HAL 明确拥有目标的初始化与释放时，才能标为
`external`。生成器会拒绝“缺少 generated owner 却未声明 external”，也会拒绝“目标已有
generated owner 仍声明 external”，防止双初始化或无人初始化。

生成器负责校验但不自动重排顺序。依赖顺序错误会在配置阶段退出，需要显式调整 binding
或 DTS 中的 `init-level` / `init-priority`。`devices.json` 会记录每条依赖的 lifecycle，
评审时必须能说明 external owner 位于哪个 adapter/HAL。

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
namespace app::board {

decltype(device_get(uart0)) console();
decltype(device_get(motor0)) main_motor();
decltype(device_get(led0)) status_led();

} // namespace app::board
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

## 并发、超时与资源失败

- UART/SPI/I2C 的 `timeout_ms` 是一次 API 调用的总预算，包含取锁、DMA/轮询和完成等待，
  每个阶段不得重新获得一份完整 timeout；
- 异步 UART TX 必须由每个实例的 mutex 串行化，ISR 只提交完成信号，不得抢占新的 owner；
- 驱动初始化必须检查 OSAL 队列、stream、mutex/semaphore 和 DMA 资源创建结果，失败进入
  `DeviceBase::Error`，不能返回“已初始化”；
- bare-metal OSAL 不支持的 thread、queue、event-group、software-timer API 必须明确失败，
  不能用 no-op 伪装成功；能力由 `OSAL_HAS_THREADS` 在 Kconfig 阶段约束；
- ISR 接收缓存满时必须计数丢包；UART 统计至少包括 TX timeout、RX drop 和硬件错误。

统计快照必须无数据竞争：UART/I2C 使用原子计数，SPI 在 bus mutex 下读取和清零；
`read_stats()` 默认非阻塞并显式报告总线正忙，诊断路径不能无限等待业务传输。
CAN FD 长度必须通过统一 DLC 映射；接收 API 携带目标容量，backend 先接收到私有 64 B
缓冲再校验和复制，禁止 HAL 直接写入未知容量的调用方缓冲。STM32 FDCAN 的 kernel
clock、仲裁/数据 bitrate 和 timing 必须在 DTS/Config 中闭环并精确校验；不再接受
忽略 bitrate 的硬编码时序或非法 port 取模回绕。GD32 classic CAN 同样从 DTS 读取
kernel clock、prescaler、SEG1/SEG2/SJW 并验证精确 bitrate，不保留板级硬编码时钟。
每个 CAN controller 的 init/start/stop/send/receive/deinit 由实例 mutex 串行化；接收
同时返回标准/扩展帧类型，不能仅比较数值 ID。STM32 FDCAN 为每个 controller 分配互不
重叠的 message RAM 窗口和有界 RX/TX FIFO，并在硬件过滤与软件接收两层拒绝 remote 帧。
组件只能管理 start/stop；设备的 init/deinit 归生成 registry 生命周期所有，禁止组件
绕过设备模型重复初始化或拆除驱动。
CAN backend 负责控制器外设时钟生命周期；共享时钟域内的多个 controller 通过全局锁和
初始化位图协调，最后一个 controller 释放时才关时钟。板级 DTS 仍必须提供经原理图确认
的 `pinctrl-0`，并完成收发器使能、终端电阻和总线电气验证。框架不会根据 MCU 的可选
复用脚猜测 PCB 走线；缺少这些板级信息时只能证明软件构建和控制器寄存器路径，不能
宣称 CAN 已通过实板验收。

STM32 FDCAN Kconfig 会自动选择对应 HAL 模块，当前只接受 CCU reset divider 1，其他
`clk-divider` 在 DTS 校验阶段拒绝，避免静默忽略共享时钟配置。STM32 UART/SPI/PWM/I2C/
Flash 直寄存器 backend 统一受 `CONFIG_STM32_DIRECT_REGISTER_DRIVERS` experimental
门禁保护；在产品实现 clock/reset owner 并加入 STM32 全镜像 CI 前不得用于量产配置。
I2C V2 还要求 DTS 提供非零 `timing`，生成设备注册表负责调用 `init(timing)`，不再生成
“有设备实例但从不初始化”的伪就绪设备。

异步日志只在 `OSAL_HAS_THREADS=y` 时可用。它采用静态有界队列和单消费者后端所有权；
生产者在 ISR/队列满时丢弃并计数，不允许阻塞实时线程。`log_flush()` 只用于正常线程的
有界收尾，并通过同一 FIFO 中的 barrier 确认此前消息已完成后端发送；fault handler
禁止调用日志系统。

应用代码优先使用产品语义接口。只有板级门面和极少数底层适配文件应该直接调用
`device_get(alias)`。
