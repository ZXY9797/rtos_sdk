# 架构边界说明

本 SDK 是一个“描述驱动”的嵌入式框架。设备树和 Kconfig 描述产品与固件，
CMake 负责组织构建，生成的 DeviceTrait 特化在编译期完成硬件实例绑定。

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

组件层负责可复用的领域逻辑或服务逻辑。组件可以按职责依赖更底层的驱动抽象
和 OSAL，但不能依赖具体产品应用，也不能依赖 bootloader 的私有实现。

驱动层负责硬件抽象契约。公开驱动头文件不应暴露厂商 HAL 类型。SoC 适配和
厂商 SDK 调用应放在 `embedded/soc` 或驱动实现文件中。

## 配置职责

DTS 描述硬件拓扑：启用的设备、MMIO 基地址、中断、引脚、时钟、Flash 分区
以及外设静态参数。

Kconfig 描述构建期选择：功能开关、驱动选择、组件容量上限以及编译期策略。

产品配置描述业务策略：控制默认值、产品 ID、固件身份以及产品级行为。

除非是由单一事实源生成出来的派生文件，否则不要在多个配置层重复维护同一个
事实。

## 启动架构

Boot 代码分为公共代码和固件私有代码。

`boot_common` 放置 app、loader、preloader、upgrade 共享的公开类型和辅助
能力：镜像元数据、产品元数据、Flash 分区接口、镜像确认、SHA-256、DFU
协议 CRC 等。

各固件目录只保留自己的职责：

- `bootloader/loader`：启动决策、DFU 协议处理、跳转应用。
- `bootloader/preloader`：loader 自升级拷贝和早期交接。
- `bootloader/upgrade`：写入新的 loader 镜像。
- `app/product/*`：产品固件编排和产品元数据。

应用固件可以调用 `boot::confirm_image()`，也可以发布 `boot::ProductInfo`，
但不能编译 `bootloader/loader/src` 下的私有文件。

## 组件依赖等级

`component/algo` 是纯算法层。它不能依赖 OSAL、DTS、driver、bootloader
或产品代码。

`component/foc` 是电机控制领域组件。它可以依赖 `algo` 和驱动抽象，但产品
CLI、产品启动流程、boot 状态和板级策略应放在 `app/product`。

`component/link` 和 `component/nvs` 是服务组件。它们可以依赖 OSAL 和驱动
抽象，但公开 API 必须保持产品无关。

`component/ble/goodix` 是厂商适配组件。厂商 SDK 细节应限制在组件内部，
产品代码只使用公开的 `ble::` API。

## 设备模型

本项目继续保留当前 DeviceTrait 模型，不采用 Zephyr 的运行时 `struct device`
模型。设备实例来自 DTS 和生成代码，C++ 模板负责保留编译期分发能力，避免
不必要的运行时间接调用。
