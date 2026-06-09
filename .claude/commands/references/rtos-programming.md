# RTOS 编程规范

RTOS（实时操作系统）是嵌入式系统的基础软件层。
正确使用 RTOS 可以简化并发编程，错误使用会导致
优先级反转、死锁、栈溢出等难以调试的问题。

---

## 1. 任务设计原则

### 1.1 单一职责

每个任务只负责一个功能模块：

```c
/* 正确 — 每个任务职责单一 */
void task_sensor_read(void *arg)
{
    while (1) {
        read_all_sensors();
        os_delay_ms(100);
    }
}

void task_data_process(void *arg)
{
    while (1) {
        os_sem_take(&data_ready_sem, OS_WAIT_FOREVER);
        process_sensor_data();
    }
}

/* 错误 — 一个任务做所有事情 */
void task_main(void *arg)
{
    while (1) {
        read_sensors();      /* 传感器读取 */
        process_data();      /* 数据处理 */
        update_display();    /* 显示更新 */
        check_buttons();     /* 按键检测 */
        os_delay_ms(10);
    }
}
```

### 1.2 任务优先级分配

```c
/* 优先级分配原则：
 * 1. 实时性要求越高，优先级越高
 * 2. 执行时间越短，优先级可以越高
 * 3. 避免过多高优先级任务 */

/* 典型优先级分配 */
#define PRIO_IDLE         0   /* 空闲任务 — 最低 */
#define PRIO_BACKGROUND   1   /* 后台任务：日志、统计 */
#define PRIO_NORMAL       5   /* 普通任务：UI、按键 */
#define PRIO_HIGH         8   /* 高优先级：控制环路 */
#define PRIO_CRITICAL    10   /* 关键任务：安全监测 */
#define PRIO_ISR_DEFER   12   /* ISR 延迟处理 */
```

### 1.3 任务栈大小

```c
/* 栈大小计算公式：
 * 栈需求 = 函数调用深度 × 每帧大小
 *        + 局部变量总和
 *        + 中断嵌套开销（如果有）
 *        + 安全余量（建议 20-30%） */

/* 使用 uxTaskGetStackHighWaterMark 监控栈使用 */
void task_monitor_stack(void *arg)
{
    while (1) {
        UBaseType_t watermark = uxTaskGetStackHighWaterMark(NULL);
        if (watermark < STACK_WARNING_THRESHOLD) {
            LOG_WARN("Task stack low: %u words free", watermark);
        }
        os_delay_ms(1000);
    }
}
```

---

## 2. 同步原语使用规范

### 2.1 信号量（Semaphore）

```c
/* 信号量用于事件通知和资源计数 */

/* 场景1：ISR 通知任务 */
os_sem_t rx_sem;

void USART1_IRQHandler(void)
{
    BaseType_t woken = pdFALSE;
    uint8_t data = USART1->DR;
    xQueueSendFromISR(rx_queue, &data, &woken);
    portYIELD_FROM_ISR(woken);
}

void task_uart_rx(void *arg)
{
    uint8_t data;
    while (1) {
        /* 等待信号量（由 ISR 释放） */
        if (xQueueReceive(rx_queue, &data, OS_WAIT_FOREVER) == pdTRUE) {
            process_byte(data);
        }
    }
}

/* 场景2：资源计数（如缓冲区池） */
os_sem_t buffer_sem;  /* 初始值 = 缓冲区数量 */

void *acquire_buffer(uint32_t timeout_ms)
{
    if (os_sem_take(&buffer_sem, timeout_ms) != OS_OK) {
        return NULL;
    }
    return alloc_from_pool();
}

void release_buffer(void *buf)
{
    free_to_pool(buf);
    os_sem_give(&buffer_sem);
}
```

### 2.2 互斥锁（Mutex）

```c
/* 互斥锁用于保护共享资源 */

os_mutex_t i2c_bus_mutex;

void i2c_read(uint8_t dev_addr, uint8_t *buf, size_t len)
{
    os_mutex_lock(&i2c_bus_mutex, OS_WAIT_FOREVER);

    /* 临界区 — 同一时刻只有一个任务访问 I2C 总线 */
    i2c_start();
    i2c_send_address(dev_addr, I2C_READ);
    i2c_read_data(buf, len);
    i2c_stop();

    os_mutex_unlock(&i2c_bus_mutex);
}
```

### 2.3 互斥锁 vs 临界区选择

```
决策树：

需要保护的操作耗时 > 1μs？
├── 是 → 使用互斥锁（允许任务切换）
└── 否 → 在 ISR 中？
    ├── 是 → 使用临界区（关中断）
    └── 否 → 保护的代码会调用阻塞 API？
        ├── 是 → 使用互斥锁
        └── 否 → 使用临界区（关中断，更快）
```

### 2.4 事件组（Event Group）

```c
/* 事件组用于多条件同步 */

#define EVT_SENSOR_READY  (1 << 0)
#define EVT_DATA_VALID    (1 << 1)
#define EVT_TIMEOUT       (1 << 2)

os_event_t system_events;

void task_sensor(void *arg)
{
    while (1) {
        read_sensor();
        os_event_set(&system_events, EVT_SENSOR_READY);
        os_delay_ms(100);
    }
}

void task_process(void *arg)
{
    while (1) {
        /* 等待所有条件满足 */
        EventBits_t bits = os_event_wait(
            &system_events,
            EVT_SENSOR_READY | EVT_DATA_VALID,
            pdTRUE,   /* 清除标志 */
            pdTRUE,   /* 等待所有位 */
            OS_WAIT_FOREVER
        );

        if ((bits & (EVT_SENSOR_READY | EVT_DATA_VALID)) ==
            (EVT_SENSOR_READY | EVT_DATA_VALID)) {
            process_data();
        }
    }
}
```

---

## 3. 队列（Queue）使用规范

### 3.1 生产者-消费者模式

```c
/* 队列是任务间通信的首选方式 */

/* 定义消息结构 */
typedef struct {
    uint32_t timestamp;
    uint16_t sensor_id;
    int16_t  value;
} SensorMsg_t;

os_queue_t sensor_queue;

void init_communication(void)
{
    /* 创建队列，深度16，每个元素大小固定 */
    os_queue_create(&sensor_queue, sizeof(SensorMsg_t), 16);
}

/* 生产者任务 */
void task_adc(void *arg)
{
    while (1) {
        SensorMsg_t msg = {
            .timestamp = get_tick(),
            .sensor_id = ADC_CHANNEL_0,
            .value     = adc_read(ADC_CHANNEL_0),
        };

        /* 非阻塞发送 — 队列满则丢弃 */
        if (os_queue_send(&sensor_queue, &msg, 0) != OS_OK) {
            g_dropped_samples++;
        }

        os_delay_ms(10);
    }
}

/* 消费者任务 */
void task_logger(void *arg)
{
    SensorMsg_t msg;
    while (1) {
        /* 阻塞接收 — 有数据才运行 */
        if (os_queue_receive(&sensor_queue, &msg, OS_WAIT_FOREVER) == OS_OK) {
            log_sensor_data(&msg);
        }
    }
}
```

### 3.2 队列深度设计

```c
/* 队列深度 = 生产速率 × 最大容忍延迟 / 消费速率 */

/* 示例：ADC 采样率 1kHz，处理延迟容忍 10ms */
/* 深度 = 1000 × 0.01 = 10，建议 16（2的幂） */

/* 队列满时的策略 */
typedef enum {
    QUEUE_FULL_DROP_NEW,     /* 丢弃新数据 */
    QUEUE_FULL_DROP_OLD,     /* 丢弃旧数据 */
    QUEUE_FULL_BLOCK,        /* 阻塞等待 */
} QueueFullPolicy;
```

---

## 4. 内存管理规范

### 4.1 静态分配优先

```c
/* 正确 — 静态分配任务栈和控制块 */
static StackType_t task_sensor_stack[512];
static StaticTask_t task_sensor_tcb;

void init_tasks(void)
{
    xTaskCreateStatic(
        task_sensor,
        "Sensor",
        512,
        NULL,
        PRIO_NORMAL,
        task_sensor_stack,
        &task_sensor_tcb
    );
}

/* 避免 — 动态分配（碎片化风险） */
void init_tasks_bad(void)
{
    /* 可能失败，且导致内存碎片 */
    xTaskCreate(task_sensor, "Sensor", 512, NULL, PRIO_NORMAL, NULL);
}
```

### 4.2 堆使用限制

```c
/* 规则：
 * 1. 仅在初始化阶段使用 malloc/free
 * 2. 运行时禁止动态分配
 * 3. 如果必须运行时分配，使用内存池 */

/* 内存池示例 */
#define PACKET_POOL_SIZE  8
#define PACKET_SIZE       256

static uint8_t packet_pool[PACKET_POOL_SIZE][PACKET_SIZE];
static uint32_t packet_pool_bitmap;

void *packet_alloc(void)
{
    uint32_t primask = __get_PRIMASK();
    __disable_irq();

    for (int i = 0; i < PACKET_POOL_SIZE; i++) {
        if (!(packet_pool_bitmap & (1 << i))) {
            packet_pool_bitmap |= (1 << i);
            __set_PRIMASK(primask);
            return &packet_pool[i];
        }
    }

    __set_PRIMASK(primask);
    return NULL;  /* 池已满 */
}

void packet_free(void *p)
{
    uint32_t idx = ((uint8_t *)p - &packet_pool[0][0]) / PACKET_SIZE;
    uint32_t primask = __get_PRIMASK();
    __disable_irq();
    packet_pool_bitmap &= ~(1 << idx);
    __set_PRIMASK(primask);
}
```

---

## 5. 时间管理

### 5.1 延迟函数选择

```c
/* 正确 — 让出 CPU 给其他任务 */
os_delay_ms(100);

/* 错误 — 忙等待，浪费 CPU */
void delay_ms_bad(uint32_t ms)
{
    uint32_t start = get_tick();
    while (get_tick() - start < ms) {}  /* 禁止！ */
}
```

### 5.2 周期性任务

```c
/* 正确 — 使用绝对时间，避免累积误差 */
void task_periodic(void *arg)
{
    TickType_t last_wake = xTaskGetTickCount();

    while (1) {
        /* 周期性执行，精确 10ms 间隔 */
        do_work();

        /* vTaskDelayUntil 使用绝对时间 */
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(10));
    }
}

/* 错误 — 使用相对延迟，会有累积误差 */
void task_periodic_bad(void *arg)
{
    while (1) {
        do_work();
        vTaskDelay(pdMS_TO_TICKS(10));  /* 误差累积 */
    }
}
```

### 5.3 超时设计

```c
/* 带超时的操作，避免无限等待 */
int safe_i2c_read(uint8_t addr, uint8_t *buf, size_t len)
{
    os_tick_t timeout = os_ms_to_tick(100);
    os_tick_t start = os_tick_get();

    while (!i2c_transfer_complete()) {
        if (os_tick_get() - start > timeout) {
            i2c_reset_bus();
            return ERR_TIMEOUT;
        }
        os_delay_ms(1);
    }

    memcpy(buf, i2c_get_data(), len);
    return ERR_OK;
}
```

---

## 6. 死锁防范

### 6.1 锁顺序规则

```c
/* 规则：所有任务必须按相同顺序获取多个锁 */

/* 定义锁顺序 */
#define LOCK_ORDER_SENSOR   1
#define LOCK_ORDER_STORAGE  2
#define LOCK_ORDER_COMM     3

/* 任务 A — 正确顺序 */
void task_a(void *arg)
{
    os_mutex_lock(&sensor_mutex, OS_WAIT_FOREVER);   /* 先获取 sensor */
    os_mutex_lock(&storage_mutex, OS_WAIT_FOREVER);  /* 再获取 storage */

    /* ... */

    os_mutex_unlock(&storage_mutex);
    os_mutex_unlock(&sensor_mutex);
}

/* 任务 B — 相同顺序 */
void task_b(void *arg)
{
    os_mutex_lock(&sensor_mutex, OS_WAIT_FOREVER);   /* 相同顺序 */
    os_mutex_lock(&storage_mutex, OS_WAIT_FOREVER);

    /* ... */

    os_mutex_unlock(&storage_mutex);
    os_mutex_unlock(&sensor_mutex);
}
```

### 6.2 死锁检测

```c
/* 运行时死锁检测（调试模式） */
typedef struct {
    os_mutex_t *mutex;
    os_task_t  *owner;
    uint32_t    lock_time;
} LockRecord_t;

static LockRecord_t g_lock_records[MAX_LOCKS];

int safe_mutex_lock(os_mutex_t *mutex, uint32_t timeout)
{
    os_task_t *current = os_task_current();

    /* 检查是否会形成环路 */
    if (would_deadlock(current, mutex)) {
        LOG_ERROR("Deadlock detected!");
        return ERR_DEADLOCK;
    }

    int ret = os_mutex_lock(mutex, timeout);
    if (ret == OS_OK) {
        record_lock(mutex, current);
    }
    return ret;
}
```

---

## 7. 任务间通信模式

### 7.1 命令队列模式

```c
/* 应用任务通过命令队列控制驱动任务 */

typedef enum {
    CMD_START,
    CMD_STOP,
    CMD_SET_PARAM,
    CMD_GET_STATUS,
} CommandType;

typedef struct {
    CommandType type;
    union {
        uint32_t param;
        struct {
            uint16_t id;
            uint16_t value;
        } set_param;
    };
} Command_t;

os_queue_t cmd_queue;

/* 命令发送方 */
int motor_set_speed(uint16_t speed)
{
    Command_t cmd = {
        .type = CMD_SET_PARAM,
        .set_param = { .id = PARAM_SPEED, .value = speed },
    };
    return os_queue_send(&cmd_queue, &cmd, 100);
}

/* 命令接收方 — 驱动任务 */
void task_motor_driver(void *arg)
{
    Command_t cmd;
    while (1) {
        if (os_queue_receive(&cmd_queue, &cmd, OS_WAIT_FOREVER) == OS_OK) {
            switch (cmd.type) {
            case CMD_START:
                motor_hw_start();
                break;
            case CMD_STOP:
                motor_hw_stop();
                break;
            case CMD_SET_PARAM:
                motor_hw_set_param(cmd.set_param.id, cmd.set_param.value);
                break;
            }
        }
    }
}
```

### 7.2 发布-订阅模式

```c
/* 简单的发布-订阅实现 */

typedef void (*SubscriberCallback)(const void *data);

typedef struct {
    SubscriberCallback callbacks[MAX_SUBSCRIBERS];
    uint8_t count;
} EventBus_t;

static EventBus_t g_event_bus;

void event_subscribe(SubscriberCallback cb)
{
    if (g_event_bus.count < MAX_SUBSCRIBERS) {
        g_event_bus.callbacks[g_event_bus.count++] = cb;
    }
}

void event_publish(const void *data)
{
    for (int i = 0; i < g_event_bus.count; i++) {
        g_event_bus.callbacks[i](data);
    }
}
```

---

## RTOS 编程检查清单

- [ ] 每个任务有明确的单一职责
- [ ] 任务优先级分配合理，无优先级反转
- [ ] 栈大小经过计算和测试验证
- [ ] 共享资源使用互斥锁或临界区保护
- [ ] 多个锁的获取顺序一致（防死锁）
- [ ] 使用队列进行任务间通信
- [ ] 避免运行时动态内存分配
- [ ] 周期性任务使用绝对时间延迟
- [ ] 所有阻塞操作有超时保护
- [ ] ISR 使用 `_from_isr` 后缀的 API
