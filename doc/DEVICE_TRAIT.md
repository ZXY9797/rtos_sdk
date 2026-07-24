# DeviceTrait 编译期分发

## 概述

RTOS SDK 使用编译期模板特化实现设备分发，相比 Zephyr 的运行时 vtable 方案，实现零开销抽象。

## 分发链路

```
                    device_get(uart0)
                            │
                            ▼
                    DT_ORD(DT_ALIAS(uart0)) = 14
                            │
                            ▼
                    DeviceTrait<14>::instance  ← 全局静态实例（已自动 init）
                            │
                            ▼
                    Uart<0x40013800, 37>
                            │
                  ┌─────────┴─────────┐
                  │    UartBase        │  ← 非模板，MCU 实现
                  │    base_ = 0x40013800 │
                  └───────────────────┘
```

## 核心组件

| 组件 | 作用 |
|:---|:---|
| `DeviceTrait<Ord>` | 主模板，由 `gen_device_traits.py` 自动生成特化 + 静态实例 |
| `device_get(alias)` | 编译期获取已初始化的设备引用 |
| `Uart<Base, Irq>` | 值参数模板，编译期绑定基地址/中断号 |
| `UartBase` | 非模板基类，MCU 特定实现在 `.cc` 文件中 |

## 与运行时多态对比

| | 运行时多态（Zephyr） | 编译期多态（RTOS SDK） |
|:---|:---:|:---:|
| 开销 | vtable + 间接调用 | **零** |
| 二进制大小 | 链接所有子类 | **只实例化用到的** |
| 优化 | 间接调用阻碍内联 | **编译器完全优化** |

## 代码生成器

`gen_device_traits.py` 直接读取 `gen_edt.py` 生成的 `edt.pickle`，并解析
DT 绑定 YAML 中稳定的 `cxx-driver` adapter 契约。它从 EDT 节点对象取得
binding、ordinal、alias、status、interrupt 和依赖，再展开 C++ adapter
宏并生成静态实例、initcall、ISR 入口和 `devices.json`。
设备属性到 C++ 类型、配置结构体的映射由 adapter 负责，新增属性不需要修改
Python。生成器不解析 `devicetree_generated.h`，只负责节点枚举、契约校验、
依赖检查和公共代码生成。

生成步骤通过 CMake `add_custom_command()` 接入 build DAG。EDT pickle、
binding YAML 和生成脚本都是显式依赖；实际生成文件采用内容不变不重写策略，
避免无意义的 C++ 重编译。

### Binding YAML 示例

```yaml
# binding YAML (gd,gd32-spi.yaml)
cxx-driver:
  adapter:
    header: device_adapters/spi_dt.h
    macro: HAL_SPI_DT_ADAPT
  type-name: Spi
  init: true
  readiness: device-base
  device-base: true
  init-level: pre-kernel-2
  init-priority: 25
  requires:
    - parent
```

`type-name` 只用于 `devices.json` 和诊断表，不参与 C++ 代码拼接。
`init` 和 `isr` 声明 adapter 是否提供对应静态入口。

### Adapter 示例

```cpp
// include/device_adapters/spi_dt.h
#pragma once

#include <device.h>
#include <drivers/spi.h>

#define HAL_SPI_DT_ADAPT(node_id)                                  \
    template <>                                                    \
    struct DeviceTrait<DT_ORD(node_id)> {                          \
        static constexpr uint32_t kClockHz =                       \
            DT_PROP_OR(node_id, spi_max_frequency, 1000000U);      \
        static_assert(kClockHz > 0U);                              \
        using type = Spi<DT_REG_ADDR(node_id)>;                    \
        static type instance;                                      \
        static int init()                                          \
        {                                                          \
            SpiConfig config{};                                    \
            config.mode = SpiMode::Mode0;                          \
            config.clock_hz = kClockHz;                            \
            config.data_bits = 8U;                                 \
            return static_cast<int>(instance.init(config));        \
        }                                                          \
    };
```

### Adapter 契约

| 字段 | 用途 |
|:---|:---|
| `adapter.header` | adapter 头文件路径 |
| `adapter.macro` | 接收一个 `node_id` 的 C++ 宏 |
| `type-name` | 诊断报告中的类型名 |
| `init` | adapter 是否提供 `static int init()` |
| `isr` | adapter 是否提供 `static void isr()` |
| `scope` | `node` 或 `children` |
| `requires` | parent/phandle 初始化依赖 |
| `init-level` | initcall 级别 |
| `init-priority` | 同级 initcall 优先级 |
| `readiness` | 诊断表的就绪策略 |
| `device-base` | 类型是否继承 `DeviceBase` |

adapter 内直接使用项目已有的 devicetree 宏，例如 `DT_REG_ADDR`、
`DT_IRQN`、`DT_PROP_OR`、`DT_PARENT`、`DT_PHANDLE` 和
`DT_PHA_BY_IDX`。复杂类型转换、缩放和 `static_assert` 都留在 C++，
能够由编译器做类型检查。

### 生成的代码

```cpp
// 自动生成的 drivers_generated.h
HAL_UART_DT_ADAPT(DT_N_S_soc_S_usart_40013800)
inline auto &device_get() { return DeviceTrait<14>::instance; }
#define device_get(alias) hal::device_get<DT_ORD(DT_ALIAS(alias))>()

// 自动生成的 drivers_generated.cc
DeviceTrait<14>::type DeviceTrait<14>::instance{};
static int _init_uart0() {
    return DeviceTrait<14>::init();
}
SYS_INIT(hal::_init_uart0, INITCALL_LEVEL_PRE_KERNEL_2, 25);
```

## 设计优势

- **非侵入**：不修改 Zephyr 的 `gen_defines.py`
- **稳定生成器**：新增驱动和属性只增加 YAML 元数据与 C++ adapter
- **结构化数据源**：直接使用 EDT API，不从 C 宏反推设备树
- **可靠增量构建**：生成产物和所有输入都进入 CMake build DAG
- **类型安全**：DT 属性转换由 C++ 编译器和 `static_assert` 检查
- **自动初始化**：DTS `status = "okay"` → 启动时自动 init，业务层零配置
- **依赖安全**：初始化顺序在配置阶段校验，错误顺序直接构建失败
