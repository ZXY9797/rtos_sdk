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
生成器还会解析 parent/phandle 依赖，检查依赖设备是否早于当前设备初始化；顺序错误会在配置阶段失败。

### Binding YAML 示例

```yaml
# binding YAML (gd,gd32-usart.yaml) — 值参数模板
cxx-driver:
  template: Uart
  header: drivers/uart.h
  args: [node-reg, irq]
  init-cfg:                           # ← init() 参数映射
    type: UartConfig
    fields:
      baudrate:  { prop: current-speed, default: 115200 }
      data_bits: { default: 8, cast: DataBits }
      stop_bits: { default: 0, cast: StopBits }
      parity:    { default: 0, cast: Parity }
```

```yaml
# binding YAML (foc,motor.yaml) — phandle 参数 + 命名空间类型
cxx-driver:
  template: foc::MotorDevice           # 命名空间限定
  header: foc/motor_device.h
  args:
    - phandle-ord: pwm-dev             # 引用其他设备的 ordinal
    - phandle-ord: adc-dev
    - prop: pwm-ch-u                   # 从 DTS 属性取值
    - prop: pwm-ch-v
    - prop: pwm-ch-w
  init-cfg:
    type: foc::MotorConfig
    fields:
      rs:    { prop: rs-milliohm, default: 50, scale: 0.001 }
      imax:  { prop: max-current-ma, default: 20000, scale: 0.001 }
      sensor: { prop: sensor-mode, default: 0, cast: foc::SensorMode }
```

```yaml
# binding YAML (goodix,gr5525-ble.yaml) — 非模板类 + string 属性
cxx-driver:
  template: ble::BleDevice             # 非模板类，args 为空
  header: ble/ble_device.h
  args: []
  init-cfg:
    type: ble::StackConfig
    fields:
      device_name: { type: string, prop: device-name, default: "BLE" }
      "conn_param.interval_min": { prop: conn-interval-min, default: 320 }
      "sec_param.level": { prop: security-level, default: 2, cast: "ble::SecParam::Level" }
```

```yaml
# binding YAML (invensense,icm40609d.yaml) — SPI 子设备 + 父总线 ordinal
cxx-driver:
  template: imu::Icm40609d
  header: imu/icm40609d.h
  args:
    - parent-ord                      # 父 SPI 设备 ordinal
    - phandle-reg: cs-gpios           # CS GPIO port base
    - field: cs-gpios/pin             # CS pin
  requires:
    - parent
    - phandle-array: cs-gpios
```

### 常用参数

| 参数 | 用途 |
|:---|:---|
| `node-reg` | 当前节点 `reg` 基地址 |
| `irq` | 当前节点中断号 |
| `prop: <name>` | DTS 属性值 |
| `phandle-ord: <prop>` | 属性引用设备的 ordinal |
| `parent-ord` | 父节点设备的 ordinal，用于 `device_get<BusOrd>()` |
| `parent-reg` | 父节点寄存器基地址，只用于真实寄存器访问 |
| `phandle-reg: <prop>` | phandle-array 引用设备的 `reg` 基地址 |
| `field: <prop>/<cell>` | phandle-array cell 值 |

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
- **依赖安全**：初始化顺序在配置阶段校验，错误顺序直接构建失败
