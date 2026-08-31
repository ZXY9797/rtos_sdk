# 架构边界说明

本 SDK 是一个“描述驱动”的嵌入式框架。设备树和 Kconfig 描述产品与固件，
CMake 负责组织构建，生成的 DeviceTrait 特化在编译期完成硬件实例绑定。

构建输入按以下单向流水线收敛，禁止从后一级反向补写硬件事实：

```text
产品 + 固件类型
    -> board.dts + binding
    -> EDT / devicetree_generated.h / Kconfig.dts
    -> 根 Kconfig + 产品 Kconfig + prj.conf
    -> CMake 目标与源码依赖闭包
    -> 严格链接 ELF
    -> bin/hex/三合一固件
```

## 分层规则

依赖方向必须保持向下：

```text
app/product
component
embedded/drivers
embedded/soc
厂商 HAL / RTOS 移植层
```

应用层只负责产品编排。应用层可以使用组件 API、驱动 API、OSAL API、生成
的设备别名以及公开 boot API；不能包含 SoC HAL 头文件，也不能直接编译
bootloader 私有源文件。

`app/product/<product>` 的入口文件固定为根目录下的 `main.cc`。产品内部按职责拆分为 `board/`、`services/`、`comm/`、`control/`、`tasks/` 等目录，统一使用 `app` 命名空间和 `app::board` 板级设备门面。详细约定见 [APP_LAYOUT.md](APP_LAYOUT.md)。

组件层负责可复用的领域逻辑或服务逻辑。组件可以按职责依赖更底层的驱动抽象
和 OSAL，但不能依赖具体产品应用，也不能依赖 bootloader 的私有实现。

驱动层负责硬件抽象契约。公开驱动头文件不应暴露厂商 HAL 类型。SoC 适配和
厂商 SDK 调用应放在 `embedded/soc` 或驱动实现文件中。

## 配置职责

| 配置层 | 唯一职责 | 典型内容 | 禁止内容 |
|:---|:---|:---|:---|
| DTS/binding | 板级物理事实和静态硬件拓扑 | MMIO、IRQ、DMA、引脚、总线依赖、Flash 几何、电机电气参数、采样与 PWM 参数 | 产品策略、运行时标定值、驱动源码选择 |
| Kconfig/prj.conf | 编译期功能和产品策略 | 组件开关、容量上限、算法策略、保护阈值、产品默认行为 | 已在 DTS 中存在的寄存器地址、电机极对数、PWM/ADC 硬件参数 |
| NVS | 经校验的运行时状态和标定覆盖 | 电流零点、控制环标定、用户设置 | 未校验的数据直接覆盖安全边界、硬件拓扑 |
| CMake | 构建图和依赖闭包 | 源码、include、库依赖、生成器输入、链接脚本、产物规则 | 设备地址、IRQ、业务阈值和隐式驱动选择 |

同一个事实只能有一个维护入口。派生文件必须由该入口生成；例如 boot Flash
布局来自 `bootloader/product/<product>/layout.json`，C++ 映射和打包参数均由
生成器得到，不再手写多份分区表。

驱动后端使用双重闭合：Kconfig 选项必须依赖对应的
`DT_HAS_<COMPATIBLE>_ENABLED`，CMake 只在该配置为 `y` 时编译后端。这样 DTS
不存在对应设备时，手写 `CONFIG_...=y` 会在配置阶段失败，而不是生成一个链接
后才暴露问题的空目标。

Kconfig 根入口只解析 `embedded`、`component` 和当前产品 Kconfig 各一次。配置
缓存校验覆盖全部 Kconfig/配置文件的路径和内容；输入改变后自动重新生成，输入
未改变时复用 `.config`。完整构建约定见 [BUILD_SYSTEM.md](BUILD_SYSTEM.md)。

## 启动架构

Boot 代码分为公共代码和固件私有代码。

`boot_common` 放置 app、loader、preloader、upgrade 共享的公开类型和辅助
能力：镜像元数据、产品元数据、Flash 分区接口、镜像确认、SHA-256、DFU
协议 CRC 等。

各固件目录只保留自己的职责：

- `bootloader/preloader`：第一阶段信任根；生产配置认证固定 loader，开发配置可进入 loader-upgrade。
- `bootloader/loader`：第二阶段启动决策、DFU 协议处理、签名/防降级校验、单执行槽 sector-swap、trial 回滚和应用 handoff；当前不是双执行槽 A/B。
- `bootloader/upgrade`：loader 自升级执行，处理 `loader_upgrade` 路径并写入新的 loader 镜像。
- `app/product/*`：产品固件编排和产品元数据。

应用固件可以调用 `boot::confirm_image()`，也可以发布 `boot::ProductInfo`，
但不能编译 `bootloader/loader/src` 下的私有文件。

完整启动和升级关系见 [BOOT_FLOW.md](BOOT_FLOW.md)。

## 组件依赖等级

`component/algo` 是纯算法层，承载定长空间数学、DSP 和单轴运动轮廓等
无产品语义的计算原语。它不能依赖 OSAL、DTS、driver、bootloader 或产品代码。

`component/control_contracts` 是头文件契约层，定义固定布局的领域数据并复用
`algo` 的数值类型。功能组件不再为取得 `gimbal/types.h` 而伪依赖 `motion`。

`component/foc` 是电机控制领域组件。它可以依赖 `algo` 和驱动抽象，但产品
CLI、产品启动流程、boot 状态和板级策略应放在 `app/product`。

三轴控制能力按职责拆为 `control_contracts`、`motion`、`attitude`、`control`、
`position_sensor`、`thermal`、`safety` 和 `ipc`。组件之间只通过定长值类型
和显式接口通信；产品参数 schema、共享主题集合、任务频率与设备映射留在
`app/product/gimbal`。禁止用单一 `gimbal_core` 静态库重新聚合这些依赖。

`component/link` 和 `component/nvs` 是服务组件。它们可以依赖 OSAL 和驱动
抽象，但公开 API 必须保持产品无关。

`component/ble/goodix` 是厂商适配组件。厂商 SDK 细节应限制在组件内部，
产品代码只使用公开的 `ble::` API。

## 设备模型

本项目继续保留当前 DeviceTrait 模型，不采用 Zephyr 的运行时 `struct device`
模型。设备实例来自 DTS 和生成代码，C++ 模板负责保留编译期分发能力，避免
不必要的运行时间接调用。

设备驱动模型的细化规则见 [DRIVER_MODEL.md](DRIVER_MODEL.md)。后续驱动接入、设备依赖、
初始化顺序和产品级设备门面都以该文档为准。

## 链接与启动约束

- 所有固件执行严格链接，不允许 `--unresolved-symbols=ignore-in-object-files`、
  `--allow-multiple-definition` 或 semihosting `rdimon.specs` 掩盖依赖错误。
- Cortex-M 启动对象和最小故障处理对象直接进入每个最终 ELF；应用运行时系统对象
  仅进入需要它的固件，避免静态库提取顺序决定启动行为。
- GR5525 的 BLE 预编译库与 ROM symbol 表是一组 ABI。CMake 在配置阶段校验两者
  的 SHA-256，版本不匹配立即失败。
- pre-kernel 初始化失败时按逆序回滚已完成的阶段；OSAL 初始化/启动失败不进入
  应用。调度器正常返回同样视为致命错误。

构建通过只证明源码、配置和链接关系闭合，不等于板级验收通过。CAN 引脚复用和
收发器、PWM/ADC 触发时序与电流零点、Flash 擦写、BLE 射频连接以及故障输出通道
仍必须在目标板上验证。
