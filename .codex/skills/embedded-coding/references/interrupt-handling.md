# 中断处理规范

中断服务程序（ISR）是嵌入式系统中最关键的代码路径。
ISR 的错误会导致系统崩溃、数据损坏且极难复现和调试。

---

## 1. ISR 基本原则

### 黄金法则

1. **ISR 必须尽可能短** — 执行时间 < 1μs（Cortex-M @ 72MHz）
2. **ISR 中禁止阻塞** — 不等待、不轮询、不 sleep
3. **ISR 中禁止动态内存** — 不 malloc/free、不 new/delete
4. **ISR 中禁止获取互斥锁** — 只能使用临界区（关中断）
5. **ISR 中禁止浮点运算** — 除非明确保存/恢复 FPU 上下文

### ISR 与任务的职责划分

```
ISR 职责（快路径）：
├── 读取/清除硬件状态寄存器
├── 从硬件 FIFO 读取数据到缓冲区
├── 设置事件标志 / 释放信号量
└── 更新共享状态变量（volatile）

任务职责（慢路径）：
├── 数据处理和协议解析
├── 复杂计算
├── 资源分配
└── 外设配置
```

---

## 2. ISR 编写规范

### 2.1 函数签名

```c
/* 正确 — 无参数，无返回值 */
void USART1_IRQHandler(void)
{
    /* ... */
}

/* 错误 — ISR 不应有参数或返回值 */
int USART1_IRQHandler(int mode)  /* 禁止！ */
{
    return 0;
}
```

### 2.2 局部变量限制

```c
void DMA1_Channel1_IRQHandler(void)
{
    /* 正确 — 小型局部变量（栈上） */
    uint32_t sr = DMA1->ISR;
    uint8_t  data;

    /* 错误 — 大型局部变量会占用过多栈空间 */
    uint8_t packet_buf[256];  /* 禁止！ISR 栈空间有限 */

    /* 正确 — 使用静态缓冲区（需确保可重入） */
    static uint8_t isr_buf[256];
}
```

### 2.3 共享数据访问

```c
/* 共享变量必须声明为 volatile */
volatile uint32_t g_tick_count;
volatile uint8_t  g_rx_flag;

/* 32位架构上的原子读写（对齐的32位变量） */
void SysTick_Handler(void)
{
    g_tick_count++;  /* Cortex-M 上原子操作 */
}

/* 非原子操作需要临界区保护 */
volatile uint64_t g_timestamp;

void SysTick_Handler(void)
{
    /* 64位变量在32位架构上非原子 */
    uint32_t primask = __get_PRIMASK();
    __disable_irq();
    g_timestamp++;
    __set_PRIMASK(primask);
}
```

---

## 3. 中断优先级管理

### 3.1 Cortex-M 优先级规则

```c
/* 优先级数值越小，优先级越高 */

/* 典型的优先级分配 */
#define PRIO_SYSTICK      15   /* 最低 — 系统滴答 */
#define PRIO_UART_TX      10   /* 中等 — 发送完成 */
#define PRIO_UART_RX       8   /* 较高 — 接收数据 */
#define PRIO_DMA           5   /* 高   — DMA 传输 */
#define PRIO_FAULT         0   /* 最高 — 故障处理 */
```

### 3.2 优先级配置示例

```c
void uart_init(void)
{
    /* 设置 NVIC 优先级 */
    NVIC_SetPriority(USART1_IRQn, PRIO_UART_RX);

    /* 设置抢占优先级和子优先级（如果有分组） */
    NVIC_SetPriority(USART1_IRQn,
        NVIC_EncodePriority(NVIC_PRIORITY_GROUP_4, 8, 0));

    /* 使能中断 */
    NVIC_EnableIRQ(USART1_IRQn);
}
```

### 3.3 优先级反转防范

```c
/* 问题：低优先级任务持有锁，高优先级任务等待 */
/*        中等优先级任务抢占低优先级任务 → 高优先级任务饿死 */

/* 解决方案1：优先级继承（RTOS 互斥锁支持） */
os_mutex_t mutex;
mutex_attr_t attr = {
    .type = MUTEX_TYPE_PRIORITY_INHERIT,  /* 自动提升持有者优先级 */
};
os_mutex_init(&mutex, &attr);

/* 解决方案2：关闭中断保护短临界区 */
void critical_section_example(void)
{
    uint32_t primask = __get_PRIMASK();
    __disable_irq();

    /* 临界区代码 — 必须极短（< 10 指令） */
    shared_data++;

    __set_PRIMASK(primask);
}
```

---

## 4. 中断安全 API

### 4.1 ISR 专用 API 标记

```c
/* 使用属性标记 ISR 安全函数 */
#define ISR_SAFE  __attribute__((section(".isr_code")))

/* ISR 安全的信号量释放 */
ISR_SAFE void os_sem_give_isr(os_sem_t *sem);

/* 非 ISR 安全的信号量获取（禁止在 ISR 中调用） */
int os_sem_take(os_sem_t *sem, uint32_t timeout_ms);
```

### 4.2 FreeRTOS ISR API 示例

```c
void USART1_IRQHandler(void)
{
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;

    /* 接收数据 */
    uint8_t data = USART1->DR;

    /* ISR 专用 API — 最后一个参数用于任务切换 */
    xQueueSendFromISR(rx_queue, &data, &xHigherPriorityTaskWoken);

    /* 如果有更高优先级任务被唤醒，触发上下文切换 */
    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}
```

### 4.3 RT-Thread ISR API 示例

```c
void USART1_IRQHandler(void)
{
    /* 进入中断 */
    rt_interrupt_enter();

    /* 接收数据 */
    uint8_t data = USART1->DR;
    rt_ringbuffer_put(rx_rb, &data, 1);

    /* 释放信号量（ISR 版本） */
    rt_sem_release(rx_sem);

    /* 退出中断 */
    rt_interrupt_leave();
}
```

---

## 5. 中断延迟与响应时间

### 5.1 中断延迟组成

```
中断延迟 = 信号传播延迟
         + 等待当前指令完成（0-1 周期）
         + 入栈（8 个寄存器，12 周期 @ Cortex-M3）
         + NVIC 仲裁（12 周期）
         + ISR 跳转（1-2 周期）
         ─────────────────────────
         总计：约 12-15 周期（Cortex-M3 @ 72MHz ≈ 0.2μs）
```

### 5.2 尾链（Tail-Chaining）优化

```c
/* Cortex-M3+ 的尾链优化：
 * 当两个中断几乎同时到达，处理器不弹栈再压栈，
 * 直接跳转到下一个 ISR，节省约 6 周期 */

/* 不需要特殊代码，硬件自动处理 */
/* 但应避免过多嵌套中断导致栈溢出 */
```

### 5.3 中断负载监控

```c
/* 监控 ISR 执行时间 */
static volatile uint32_t isr_start_tick;

void TIM2_IRQHandler(void)
{
    isr_start_tick = DWT->CYCCNT;  /* 开始计时 */

    /* ... ISR 逻辑 ... */

    uint32_t elapsed = DWT->CYCCNT - isr_start_tick;
    if (elapsed > ISR_MAX_CYCLES) {
        /* 警告：ISR 执行时间过长 */
        g_isr_overrun_count++;
    }
}
```

---

## 6. 常见中断错误模式

### 6.1 错误：ISR 中阻塞

```c
/* 错误 — ISR 中等待数据就绪 */
void USART1_IRQHandler(void)
{
    while (!(USART1->SR & USART_SR_TXE)) {}  /* 禁止轮询！ */
    USART1->DR = data;
}

/* 正确 — 只处理已就绪的数据 */
void USART1_IRQHandler(void)
{
    if (USART1->SR & USART_SR_RXNE) {
        uint8_t data = USART1->DR;
        buffer_push(&rx_buf, data);
    }
    /* 未就绪则直接返回，不等待 */
}
```

### 6.2 错误：ISR 中获取锁

```c
/* 错误 — 可能死锁 */
os_mutex_t data_mutex;

void TIM3_IRQHandler(void)
{
    os_mutex_lock(&data_mutex, WAIT_FOREVER);  /* 禁止！ */
    process_data();
    os_mutex_unlock(&data_mutex);
}

/* 正确 — 使用临界区代替锁 */
void TIM3_IRQHandler(void)
{
    uint32_t primask = __get_PRIMASK();
    __disable_irq();

    /* 短临界区 */
    shared_counter++;

    __set_PRIMASK(primask);
}
```

### 6.3 错误：未清除中断标志

```c
/* 错误 — 中断标志未清除，导致持续触发 */
void EXTI0_IRQHandler(void)
{
    handle_button_press();
    /* 忘记清除 EXTI->PR 的 bit0 */
}

/* 正确 — 清除中断标志 */
void EXTI0_IRQHandler(void)
{
    handle_button_press();
    EXTI->PR = EXTI_PR_PR0;  /* 写1清除 */
}
```

### 6.4 错误：共享数据非原子访问

```c
/* 错误 — 64位变量在32位架构上非原子 */
volatile uint64_t timestamp;

void SysTick_Handler(void)
{
    timestamp++;  /* 可能被中断撕裂 */
}

void main_loop(void)
{
    uint64_t ts = timestamp;  /* 可能读到半更新的值 */
}

/* 正确 — 使用临界区 */
void SysTick_Handler(void)
{
    uint32_t primask = __get_PRIMASK();
    __disable_irq();
    timestamp++;
    __set_PRIMASK(primask);
}
```

---

## 7. 中断栈设计

### 7.1 栈空间计算

```
ISR 栈需求 = 最深嵌套 ISR 的栈帧
           + 所有嵌套 ISR 的局部变量
           + 中断嵌套深度 × 8 字（32 字节，Cortex-M 入栈）

示例：
  ISR1: 32 字节局部变量
  ISR2（嵌套）: 16 字节局部变量
  最大嵌套: 2 层

  总需求 = 32 + 16 + 2×32 = 112 字节
  建议预留 = 112 × 2 = 224 字节（安全余量）
```

### 7.2 独立的 ISR 栈（MSP/PSP 分离）

```c
/* Cortex-M 支持双栈指针：
 * MSP（Main Stack Pointer）— ISR 和异常使用
 * PSP（Process Stack Pointer）— 任务使用 */

/* FreeRTOS 配置：ISR 使用 MSP，任务使用 PSP */
/* 在 portmacro.h 中 */
#define portTASK_USE_PSP  1

/* 上下文切换时自动切换栈指针 */
```

---

## 8. 中断调试技巧

### 8.1 中断计数器

```c
/* 统计各中断触发次数 */
volatile uint32_t g_irq_count[IRQ_COUNT];

#define DEFINE_IRQ_HANDLER(name, irqn) \
    void name##_IRQHandler(void) { \
        g_irq_count[irqn]++; \
        name##_Handler_Impl(); \
    }
```

### 8.2 最大中断延迟检测

```c
/* 使用 DWT 周期计数器测量中断延迟 */
static volatile uint32_t g_max_isr_latency;

void measure_isr_latency(void)
{
    /* 在任务中触发软件中断 */
    uint32_t start = DWT->CYCCNT;
    SCB->ICSR = SCB_ICSR_PENDSVSET_Msk;  /* 触发 PendSV */
    /* PendSV_Handler 中记录 DWT->CYCCNT - start */
}
```

---

## 中断处理检查清单

- [ ] ISR 执行时间 < 1μs
- [ ] ISR 中无阻塞调用（无等待、无轮询）
- [ ] ISR 中无动态内存分配
- [ ] ISR 中无互斥锁获取
- [ ] 共享变量声明为 `volatile`
- [ ] 非原子访问使用临界区保护
- [ ] 中断标志在 ISR 中正确清除
- [ ] 使用 ISR 专用 API（带 `_from_isr` 后缀）
- [ ] 中断优先级合理分配，无优先级反转风险
- [ ] 栈空间足够容纳最深中断嵌套
