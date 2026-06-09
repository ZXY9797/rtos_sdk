# IRQ/ISR 中断处理框架

## 概述

中断处理采用向量表弱别名直接覆盖方案，无运行时开销。

## 工作原理

```
vector_table.S                    驱动代码
┌──────────────────────┐          ┌──────────────────────────────┐
│ .weak IRQ37_Handler  │          │ HAL_ISR_CONNECT(37, UartBase, │
│ .thumb_set → Default │  ──────→│     &DeviceTrait<14>::instance)│
│                      │  覆盖   │                              │
│ .word IRQ37_Handler  │         │ extern "C" void IRQ37_Handler │
│   ↑ 向量表入口        │         │   { obj->isr_handler(); }     │
└──────────────────────┘          └──────────────────────────────┘
                                         │
                                         ▼
                                  void UartBase::isr_handler() {
                                      // 读寄存器、写 ring buffer
                                  }
```

## HAL_ISR_CONNECT 宏

生成 `extern "C"` 函数，直接覆盖向量表弱别名：

```cpp
// irq.h
#define HAL_ISR_CONNECT(irq_n, type, obj_p)                         \
    extern "C" void IRQ##irq_n##_Handler(void) {                    \
        static_cast<type *>(const_cast<void *>(                     \
            static_cast<const void *>(obj_p)))                       \
            ->isr_handler();                                         \
    }
```

## 驱动示例

```cpp
class UartBase {
public:
    void isr_handler();  // 中断处理逻辑
};

void UartBase::isr_handler() {
    auto *regs = reinterpret_cast<UsartRegs *>(m_base);
    if (regs->STAT & STAT_RBNE) {
        uint8_t ch = static_cast<uint8_t>(regs->DATA);
        m_rx_ring.write(&ch, 1);
        m_rx_sem.release();
    }
}

// 由 gen_device_traits.py 自动生成：
HAL_ISR_CONNECT(37, UartBase, &DeviceTrait<14>::instance)
```

## 与运行时方案对比

| | 运行时查表（Zephyr） | 直接覆盖（RTOS SDK） |
|:---|:---:|:---:|
| 中断延迟 | sw_isr_table 间接跳转 | **硬件直接跳转** |
| RAM 开销 | isr_table_entry 数组 | **零** |
| 代码生成 | irq_connect() 运行时注册 | **编译期宏展开** |

## 配套工具

### hal::Irq

静态方法：enable / disable / setPriority / isEnabled

```cpp
#include <arch/irq.h>

// 使能中断
hal::Irq::enable(37);

// 禁用中断
hal::Irq::disable(37);

// 设置优先级
hal::Irq::setPriority(37, 5);

// 查询状态
bool enabled = hal::Irq::isEnabled(37);
```

### hal::IrqGuard

RAII 中断锁（构造锁中断，析构恢复）

```cpp
void critical_section() {
    hal::IrqGuard guard;  // 构造：保存中断状态并禁用

    // 临界区代码
    // ...

}  // 析构：恢复中断状态
```

## 向量表生成

`gen_irq_entries` / `gen_irq_Weaks` — `.altmacro` 宏，根据 `CONFIG_NUM_IRQS` 自动生成向量表条目和弱别名。

```asm
// vector_table.S
.macro gen_irq_entries
    .word IRQ0_Handler
    .word IRQ1_Handler
    // ... 根据 CONFIG_NUM_IRQS 生成
.endm

.macro gen_irq_weaks irq_num
    .weak IRQ\irq_num\()_Handler
    .thumb_set IRQ\irq_num\()_Handler, Default_Handler
.endm
```
