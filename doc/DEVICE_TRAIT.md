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

`gen_device_traits.py` 解析 DT 绑定 YAML 中的 `cxx-driver` 节，自动生成 `DeviceTrait<Ord>` 特化、静态实例和 initcall 注册。

### Binding YAML 示例

```yaml
# binding YAML (gd,gd32-usart.yaml)
cxx-driver:
  template: Uart
  header: drivers/uart.h
  args: [node-reg, irq]
  init-cfg:                           # ← init() 参数映射
    type: UartConfig
    rx-buffer-size:
      default: 256
    fields:
      baudrate:  { prop: current-speed, default: 115200 }
      data_bits: { default: 8, cast: DataBits }
      stop_bits: { default: 0, cast: StopBits }
      parity:    { default: 0, cast: Parity }
```

### 生成的代码

```cpp
// 自动生成的 drivers_generated.h
template <> struct DeviceTrait<14> {  // uart0
    using type = Uart<DT_REG_ADDR(...), 37>;
    static type instance;              // 全局静态实例
};
inline auto &device_get() { return DeviceTrait<14>::instance; }
#define device_get(alias) hal::device_get<DT_ORD(DT_ALIAS(alias))>()

// 自动生成的 drivers_generated.cc
DeviceTrait<14>::type DeviceTrait<14>::instance{};
static int _init_uart0() {
    UartConfig cfg {};
    cfg.baudrate = 115200;             // 从 DTS current-speed 解析
    cfg.rx_buffer = _dev_uart0_rx_buf; // 自动生成的静态 buffer
    return static_cast<int>(DeviceTrait<14>::instance.init(cfg));
}
SYS_INIT(hal::_init_uart0, INITCALL_LEVEL_PRE_KERNEL_2, 25);
```

## 设计优势

- **非侵入**：不修改 Zephyr 的 `gen_defines.py`
- **声明式**：驱动映射 + init 参数在 binding YAML 中声明
- **自动初始化**：DTS `status = "okay"` → 启动时自动 init，业务层零配置
