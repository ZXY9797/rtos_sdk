# IRQ/ISR 中断处理框架

## 数据源与职责

中断配置以 DTS 和 EDT 模型为唯一事实来源：

1. DTS 提供 IRQ 编号、优先级和 `interrupt-names`。
2. binding 的 `cxx-driver.interrupts` 声明 C++ 分发方法和 ISR 能力。
3. `gen_device_traits.py` 直接读取 `edt.pickle`，生成 IRQ 包装器和初始化代码。
4. adapter 把包装器分发到具体驱动实例。
5. 驱动只处理寄存器、状态和 ISR 安全同步，不定义设备树 IRQ 入口。

生成器不解析 `devicetree_generated.h`，也不假定节点只有一个中断。

## Binding 契约

UART 示例：

```yaml
cxx-driver:
  adapter:
    header: device_adapters/uart_dt.h
    macro: HAL_UART_DT_ADAPT
  type-name: Uart
  init: true
  interrupts:
    - name: global
      method: isr_global
      uses-osal: true
      enable-on-init: true
  init-level: post-kernel
  init-priority: 25
```

字段含义：

| 字段 | 含义 |
|:---|:---|
| `name` | 对应 DTS 的 `interrupt-names` 项 |
| `method` | `DeviceTrait<Ord>` 中的静态分发方法 |
| `uses-osal` | ISR 是否调用 OSAL ISR API |
| `shared` | 是否允许多个设备共享同一 IRQ，默认 false |
| `enable-on-init` | 设备初始化成功后是否由生成器使能 IRQ，默认 false |
| `source` | 可选的间接 IRQ 来源，例如通过 `dmas` 定位 DMA 通道 IRQ |

单中断节点缺少 `interrupt-names` 时，生成器允许唯一声明匹配唯一 IRQ。多中断节点必须使用
名称明确匹配，名称缺失、IRQ cell 缺失、priority cell 缺失和非共享 IRQ 冲突都会使构建失败。

## DMA 间接中断

SPI、UART 等外设的 DMA 完成中断属于 DMA 控制器，不属于外设节点本身。binding 用
`source` 描述从外设节点到中断源的解析路径：

```yaml
cxx-driver:
  interrupts:
    - name: dma-tx
      method: isr_dma_tx
      uses-osal: true
      enable-on-init: true
      source:
        phandle-array: dmas
        entry: tx
        interrupt-index-cell: channel
  requires:
    - phandle-array: dmas
```

板级 DTS 只描述硬件连接：

```dts
spi0: spi@40013000 {
    dmas = <&dma0 3 GD32_DMA_REQUEST_SPI0_TX>,
           <&dma0 4 GD32_DMA_REQUEST_SPI0_RX>;
    dma-names = "tx", "rx";
};
```

生成器从 `dmas` 中按 `dma-names` 找到 `tx`/`rx` 项，读取 `channel` cell，再选择 DMA
控制器对应索引的 `interrupts` 项。IRQ 编号和优先级仍来自 EDT，binding 和 C++ 驱动都
不保存 IRQ 常量。属性缺失、名称不匹配、通道不是整数或索引越界都会立即终止构建。

## DTS 示例

```dts
usart0: usart@40013800 {
    compatible = "gd,gd32-usart";
    reg = <0x40013800 0x400>;
    interrupts = <37 6>;
    interrupt-names = "global";
    status = "okay";
};
```

使用 FreeRTOS ISR API 的中断优先级必须不高于内核可管理范围。Cortex-M 数值越小，硬件优先级
越高；本项目 FreeRTOS 配置要求数值至少为
`configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY`。生成代码用
`osal::kMinRtosCallableIrqPriority` 做 `static_assert`，违规配置在编译期失败。

## 生成代码

生成器为每个启用 IRQ 生成初始化和包装器：

```cpp
extern "C" void IRQ37_Handler(void);

static int _init_uart0()
{
    static_assert(6U >= osal::kMinRtosCallableIrqPriority);
    hal::Irq::disable(37);
    hal::Irq::connect(37, IRQ37_Handler);
    hal::Irq::setPriority(37, 6U);
    hal::Irq::clearPending(37);

    const int result = hal::DeviceTrait<30>::init();
    if (result != 0) {
        hal::Irq::disable(37);
        return result;
    }
    hal::Irq::enable(37);
    return result;
}

extern "C" void IRQ37_Handler(void)
{
    osal::IsrContext context;
    hal::DeviceTrait<30>::isr_global(context);
}
```

静态向量表平台由 `IRQn_Handler` 强符号覆盖弱别名，`Irq::connect()` 是空操作。GR5525 在启动
时把向量表复制到 RAM，`Irq::connect()` 通过 `soc_register_nvic()` 更新 RAM 向量。两类平台
共享同一份生成代码。

## Adapter 与驱动

adapter 只做静态分发：

```cpp
static void isr_global(osal::IsrContext& context)
{
    instance.isr_handler(context);
}
```

驱动 ISR 不再创建自己的调度标志：

```cpp
void UartBase::isr_handler(osal::IsrContext& context)
{
    const uint8_t byte = read_data_register();
    (void)m_rx_stream.send_from_isr(&byte, 1U, context);
    (void)m_tx_sem.release_from_isr(context);
}
```

驱动目录不得定义 `IRQn_Handler` 或芯片厂商命名的 `*_IRQHandler`。生成器测试会扫描
`embedded/drivers` 并拒绝这类入口；驱动仅实现 `isr_handler()`、`dma_tx_isr()` 等实例
方法。DMA controller、channel、request 和 DMAMUX channel 由 adapter 从 DTS 注入配置。

`osal::IsrContext` 负责 RTOS 中断进入/退出记账，并聚合 ISR API 的唤醒请求。包装器返回前只进行
一次调度判断。

## ISR 规则

- ISR 中只能调用明确带 `_from_isr` 的 OSAL API。
- 不得在 ISR 中分配内存、阻塞、获取普通互斥锁或执行同步总线传输。
- ISR 先读取和清除硬件状态，再发布数据或唤醒线程。
- 共享 IRQ 的每个分发方法都必须自行判断本设备中断标志。
- 线程和 ISR 共享的多字段统计快照使用 `hal::IrqGuard` 保护。
- ISR 失败路径必须保持中断源可恢复，禁止静默吞掉配置或分发错误。

`SensorCore` 的定时器 ISR 只做分频计数和线程通知。传感器同步读取和用户周期入口在线程
上下文执行，避免 SPI/I2C 操作进入硬件 ISR。

## 平台接口

`hal::Irq` 提供：

- `connect(irq, handler)`
- `enable(irq)` / `disable(irq)`
- `isEnabled(irq)`
- `setPriority(irq, priority)`
- `clearPending(irq)`

`hal::IrqGuard` 是线程与 ISR 共享短临界区使用的 RAII 全局中断锁，不用于长耗时操作。
