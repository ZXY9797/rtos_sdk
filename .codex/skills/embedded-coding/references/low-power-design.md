# 低功耗设计规范

低功耗是嵌入式系统的核心竞争力。
功耗优化需要从硬件选型、软件架构、代码实现三个层面系统考虑。

---

## 1. 功耗模型基础

### 1.1 功耗组成

```
总功耗 = 动态功耗 + 静态功耗

动态功耗 = C × V² × f × α
  C: 负载电容
  V: 供电电压（最关键因素 — 平方关系）
  f: 时钟频率
  α: 活跃因子（翻转率）

静态功耗 = 漏电流 × V
  取决于工艺和温度
```

### 1.2 功耗优化层次

```
优先级从高到低：

1. 电压调节（效果最显著）
   └── 降压：3.3V → 1.8V 可节省约 70% 动态功耗

2. 时钟管理
   └── 降频：168MHz → 8MHz 可节省约 95% 动态功耗

3. 外设管理
   └── 关闭未使用的外设时钟

4. 睡眠模式
   └── 空闲时进入低功耗模式

5. 代码优化
   └── 减少 CPU 活跃时间
```

---

## 2. 时钟管理

### 2.1 动态时钟调节

```c
/* 根据负载动态调节系统时钟 */
typedef enum {
    CLOCK_MODE_SLEEP,     /* 32kHz — RTC 运行 */
    CLOCK_MODE_LOW,       /* 4MHz  — 基本处理 */
    CLOCK_MODE_NORMAL,    /* 64MHz — 常规运行 */
    CLOCK_MODE_HIGH,      /* 168MHz — 高性能计算 */
} ClockMode;

void set_clock_mode(ClockMode mode)
{
    switch (mode) {
    case CLOCK_MODE_SLEEP:
        /* 切换到内部 RC 振荡器，关闭 PLL */
        RCC->CFGR |= RCC_CFGR_SW_HSI;
        RCC->CR &= ~RCC_CR_PLLON;
        break;

    case CLOCK_MODE_LOW:
        /* 使用 HSI，无分频 */
        RCC->CFGR |= RCC_CFGR_SW_HSI;
        RCC->CR &= ~RCC_CR_PLLON;
        break;

    case CLOCK_MODE_NORMAL:
        /* PLL 输出 64MHz */
        configure_pll(64000000);
        break;

    case CLOCK_MODE_HIGH:
        /* PLL 输出 168MHz */
        configure_pll(168000000);
        break;
    }

    SystemCoreClockUpdate();
}
```

### 2.2 外设时钟门控

```c
/* 未使用的外设关闭时钟 */
void enable_peripheral_clock(Peripheral periph, bool enable)
{
    if (enable) {
        RCC->AHB1ENR |= (1 << periph);
    } else {
        RCC->AHB1ENR &= ~(1 << periph);
    }
}

/* 使用示例 */
void init_spi(void)
{
    enable_peripheral_clock(PERIPH_SPI1, true);
    /* ... 配置 SPI ... */
}

void deinit_spi(void)
{
    /* ... 反初始化 SPI ... */
    enable_peripheral_clock(PERIPH_SPI1, false);
}
```

---

## 3. 睡眠模式

### 3.1 睡眠模式对比

| 模式 | 唤醒时间 | 功耗 | 保持状态 | 典型用途 |
|------|---------|------|---------|---------|
| Sleep (WFI) | ~1μs | 中 | 全部 | 短暂空闲 |
| Stop | ~10μs | 低 | SRAM/寄存器 | 等待事件 |
| Standby | ~50μs | 极低 | 仅备份域 | 长时间待机 |
| Shutdown | ~ms | 最低 | 无 | 电池供电设备 |

### 3.2 睡眠模式实现

```c
/* 模式1：WFI（Wait For Interrupt）— 最简单 */
void enter_sleep_mode(void)
{
    /* 所有中断都可唤醒 */
    __WFI();
}

/* 模式2：Stop 模式 — 低功耗 */
void enter_stop_mode(void)
{
    /* 配置唤醒源（如 RTC 闹钟、外部中断） */
    RTC->CR |= RTC_CR_ALRAIE;
    EXTI->IMR |= EXTI_IMR_MR17;
    EXTI->RTSR |= EXTI_RTSR_TR17;

    /* 设置电压调节器为低功耗模式 */
    PWR->CR |= PWR_CR_LPDS;

    /* 进入 Stop 模式 */
    SCB->SCR |= SCB_SCR_SLEEPDEEP_Msk;
    __WFI();
    SCB->SCR &= ~SCB_SCR_SLEEPDEEP_Msk;

    /* 唤醒后重新配置时钟 */
    SystemClock_Config();
}

/* 模式3：Standby 模式 — 最低功耗 */
void enter_standby_mode(void)
{
    /* 仅 RTC 闹钟、WKUP 引脚可唤醒 */

    /* 清除唤醒标志 */
    PWR->CR |= PWR_CR_CWUF;

    /* 使能 WKUP 引脚唤醒 */
    PWR->CSR |= PWR_CSR_EWUP;

    /* 进入 Standby 模式 */
    PWR->CR |= PWR_CR_PDDS;
    SCB->SCR |= SCB_SCR_SLEEPDEEP_Msk;
    __WFI();
}
```

### 3.3 唤醒源配置

```c
/* 配置多种唤醒源 */
void configure_wakeup_sources(void)
{
    /* 1. RTC 闹钟唤醒（定时唤醒） */
    RTC_AlarmTypeDef alarm = {
        .AlarmTime = { .Hours = 6, .Minutes = 0, .Seconds = 0 },
        .AlarmMask = RTC_ALARMMASK_DATEWEEKDAY,
    };
    HAL_RTC_SetAlarm_IT(&hrtc, &alarm, RTC_FORMAT_BIN);

    /* 2. 外部中断唤醒（按键、传感器） */
    /* PA0 配置为上升沿触发 */
    HAL_GPIO_Init(GPIOA, &(GPIO_InitTypeDef){
        .Pin  = GPIO_PIN_0,
        .Mode = GPIO_MODE_IT_RISING,
        .Pull = GPIO_PULLDOWN,
    });
    HAL_NVIC_SetPriority(EXTI0_IRQn, 0, 0);
    HAL_NVIC_EnableIRQ(EXTI0_IRQn);

    /* 3. UART 唤醒（接收数据唤醒） */
    HAL_UARTEx_EnableStopMode(&huart1);
    __HAL_UART_ENABLE_IT(&huart1, UART_IT_WUF);
}
```

---

## 4. 外设功耗管理

### 4.1 GPIO 功耗优化

```c
/* 未使用的 GPIO 配置为模拟输入（最低功耗） */
void configure_unused_gpio(void)
{
    GPIO_InitTypeDef gpio = {
        .Pin   = GPIO_PIN_All,
        .Mode  = GPIO_MODE_ANALOG,
        .Pull  = GPIO_NOPULL,
        .Speed = GPIO_SPEED_FREQ_LOW,
    };

    /* 所有未使用引脚设为模拟模式 */
    HAL_GPIO_Init(GPIOA, &gpio);
    HAL_GPIO_Init(GPIOB, &gpio);
    /* ... */
}

/* 输出引脚在睡眠前设为已知状态 */
void prepare_gpio_for_sleep(void)
{
    /* 关闭 LED */
    HAL_GPIO_WritePin(LED_GPIO_Port, LED_Pin, GPIO_PIN_RESET);

    /* 关闭传感器电源 */
    HAL_GPIO_WritePin(SENSOR_PWR_GPIO_Port, SENSOR_PWR_Pin, GPIO_PIN_RESET);

    /* 配置为低功耗状态 */
    GPIO_InitTypeDef gpio = {
        .Pin  = LED_Pin | SENSOR_PWR_Pin,
        .Mode = GPIO_MODE_OUTPUT_PP,
        .Pull = GPIO_NOPULL,
    };
    HAL_GPIO_Init(LED_GPIO_Port, &gpio);
}
```

### 4.2 ADC 功耗管理

```c
/* ADC 不使用时关闭 */
void adc_power_down(void)
{
    ADC1->CR2 &= ~ADC_CR2_ADON;  /* 关闭 ADC */
    RCC->APB2ENR &= ~RCC_APB2ENR_ADC1EN;  /* 关闭 ADC 时钟 */
}

void adc_power_up(void)
{
    RCC->APB2ENR |= RCC_APB2ENR_ADC1EN;
    ADC1->CR2 |= ADC_CR2_ADON;
    delay_us(10);  /* 等待 ADC 稳定 */
}
```

### 4.3 通信接口功耗

```c
/* UART 低功耗配置 */
void uart_enter_low_power(UART_HandleTypeDef *huart)
{
    /* 等待发送完成 */
    while (__HAL_UART_GET_FLAG(huart, UART_FLAG_TC) == RESET) {}

    /* 禁用 UART */
    __HAL_UART_DISABLE(huart);

    /* 配置 TX 引脚为推挽输出低电平 */
    HAL_GPIO_WritePin(huart->pTxBuffPtr, huart->Init.TxPin, GPIO_PIN_RESET);
}

/* SPI 低功耗配置 */
void spi_enter_low_power(SPI_HandleTypeDef *hspi)
{
    /* 禁用 SPI */
    __HAL_SPI_DISABLE(hspi);

    /* 片选引脚拉高（不选中） */
    HAL_GPIO_WritePin(SPI_CS_GPIO_Port, SPI_CS_Pin, GPIO_PIN_SET);
}
```

---

## 5. 软件架构优化

### 5.1 事件驱动架构

```c
/* 错误 — 轮询模式，CPU 持续运行 */
void task_polling(void *arg)
{
    while (1) {
        if (button_pressed()) {
            handle_button();
        }
        if (data_ready()) {
            process_data();
        }
        delay_ms(10);  /* 浪费 CPU */
    }
}

/* 正确 — 事件驱动，CPU 大部分时间休眠 */
void task_event_driven(void *arg)
{
    while (1) {
        /* 等待事件（CPU 进入睡眠） */
        EventBits_t events = xEventGroupWaitBits(
            all_events,
            EVT_BUTTON | EVT_DATA | EVT_TIMER,
            pdTRUE, pdFALSE, portMAX_DELAY
        );

        if (events & EVT_BUTTON) {
            handle_button();
        }
        if (events & EVT_DATA) {
            process_data();
        }
    }
}
```

### 5.2 批量处理

```c
/* 错误 — 逐字节处理，频繁唤醒 */
void task_process_byte(void *arg)
{
    while (1) {
        uint8_t byte = uart_receive_byte();  /* 阻塞 */
        process_byte(byte);                   /* 处理一个字节 */
        /* 立即返回等待下一个字节 */
    }
}

/* 正确 — 批量处理，减少唤醒次数 */
void task_process_batch(void *arg)
{
    uint8_t buffer[32];
    while (1) {
        /* 等待一批数据 */
        size_t count = uart_receive(buffer, sizeof(buffer), 100);

        /* 批量处理 */
        for (size_t i = 0; i < count; i++) {
            process_byte(buffer[i]);
        }
    }
}
```

### 5.3 计算密集型任务优化

```c
/* 优化浮点运算 — 使用定点数 */
typedef int32_t fixed_t;  /* Q16.16 定点数 */

#define FIXED_SCALE  65536
#define FLOAT_TO_FIXED(x)  ((fixed_t)((x) * FIXED_SCALE))
#define FIXED_TO_FLOAT(x)  ((float)(x) / FIXED_SCALE)

fixed_t fixed_multiply(fixed_t a, fixed_t b)
{
    return (fixed_t)(((int64_t)a * b) >> 16);
}

/* 优化查表 — 空间换时间 */
static const fixed_t sin_table[256] = { /* ... */ };

fixed_t fast_sin(uint16_t angle)
{
    /* 线性插值查表 */
    uint16_t idx = angle >> 8;
    uint16_t frac = angle & 0xFF;
    return sin_table[idx] +
           (fixed_t)(((int64_t)(sin_table[idx+1] - sin_table[idx]) * frac) >> 8);
}
```

---

## 6. 电源管理框架

### 6.1 电源状态机

```c
typedef enum {
    POWER_STATE_ACTIVE,      /* 全速运行 */
    POWER_STATE_IDLE,        /* 空闲，等待事件 */
    POWER_STATE_LOW_POWER,   /* 低功耗，部分外设关闭 */
    POWER_STATE_SLEEP,       /* 深度睡眠 */
    POWER_STATE_SHUTDOWN,    /* 关机 */
} PowerState_t;

static PowerState_t g_power_state = POWER_STATE_ACTIVE;

void power_transition(PowerState_t new_state)
{
    PowerState_t old_state = g_power_state;

    /* 退出当前状态 */
    switch (old_state) {
    case POWER_STATE_SLEEP:
        exit_sleep_mode();
        break;
    case POWER_STATE_SHUTDOWN:
        /* 从关机恢复 = 系统重启 */
        NVIC_SystemReset();
        break;
    }

    /* 进入新状态 */
    switch (new_state) {
    case POWER_STATE_ACTIVE:
        set_clock_mode(CLOCK_MODE_HIGH);
        enable_all_peripherals();
        break;

    case POWER_STATE_IDLE:
        set_clock_mode(CLOCK_MODE_NORMAL);
        disable_unused_peripherals();
        break;

    case POWER_STATE_LOW_POWER:
        set_clock_mode(CLOCK_MODE_LOW);
        disable_nonessential_peripherals();
        break;

    case POWER_STATE_SLEEP:
        prepare_for_sleep();
        enter_stop_mode();
        break;

    case POWER_STATE_SHUTDOWN:
        prepare_for_shutdown();
        enter_standby_mode();
        break;
    }

    g_power_state = new_state;
}
```

### 6.2 自动休眠

```c
/* 无活动时自动进入低功耗 */
static uint32_t g_last_activity_tick;
#define IDLE_TIMEOUT_MS  5000

void power_activity_detected(void)
{
    g_last_activity_tick = HAL_GetTick();

    if (g_power_state != POWER_STATE_ACTIVE) {
        power_transition(POWER_STATE_ACTIVE);
    }
}

void power_check_idle(void)
{
    if (g_power_state == POWER_STATE_ACTIVE &&
        (HAL_GetTick() - g_last_activity_tick) > IDLE_TIMEOUT_MS) {
        power_transition(POWER_STATE_LOW_POWER);
    }
}

/* 在主循环或空闲钩子中调用 */
void vApplicationIdleHook(void)
{
    power_check_idle();

    /* 可以进入浅睡眠 */
    __WFI();
}
```

---

## 7. 功耗测量与优化

### 7.1 功耗测量方法

```c
/* 方法1：使用电流检测电阻 + ADC */
/* 在电源线上串联 0.1Ω 电阻，测量压降 */

float measure_current_ma(void)
{
    uint16_t adc_value = adc_read(CURRENT_SENSE_CHANNEL);
    float voltage = adc_value * ADC_REF_VOLTAGE / ADC_MAX_VALUE;
    float current = voltage / CURRENT_SENSE_RESISTOR;
    return current * 1000.0f;  /* 转换为 mA */
}

/* 方法2：使用功耗分析仪（如 Otii Arc、Joulescope） */
/* 更精确，可记录长时间功耗曲线 */
```

### 7.2 功耗预算

```c
/* 功耗预算表 */
typedef struct {
    const char *name;
    float active_ma;
    float sleep_ua;
    float duty_cycle;  /* 活跃时间占比 */
} PowerBudget_t;

static const PowerBudget_t power_budget[] = {
    { "MCU",       50.0f,   5.0f,   0.10f },
    { "Sensor",    10.0f,   1.0f,   0.05f },
    { "Radio",    120.0f,   2.0f,   0.01f },
    { "Display",   20.0f,   0.0f,   0.10f },
    { "LED",       10.0f,   0.0f,   0.02f },
};

float calculate_average_current(void)
{
    float total = 0;
    for (int i = 0; i < ARRAY_SIZE(power_budget); i++) {
        const PowerBudget_t *p = &power_budget[i];
        total += p->active_ma * p->duty_cycle +
                 p->sleep_ua / 1000.0f * (1.0f - p->duty_cycle);
    }
    return total;
}
```

---

## 低功耗设计检查清单

- [ ] 未使用的 GPIO 配置为模拟输入
- [ ] 未使用的外设关闭时钟
- [ ] 系统时钟按需动态调节
- [ ] 空闲时进入睡眠模式
- [ ] 使用事件驱动架构替代轮询
- [ ] 批量处理数据减少唤醒次数
- [ ] 浮点运算优化为定点运算
- [ ] 唤醒源配置正确且可靠
- [ ] 功耗预算满足电池寿命要求
- [ ] 实际功耗经过测量验证
