# 调试与日志规范

有效的调试和日志系统是嵌入式开发的生命线。
好的日志可以快速定位问题，差的日志浪费存储和带宽。

---

## 1. 日志级别定义

### 1.1 标准日志级别

```c
typedef enum {
    LOG_LEVEL_NONE  = 0,  /* 关闭所有日志 */
    LOG_LEVEL_ERROR = 1,  /* 错误：必须立即处理 */
    LOG_LEVEL_WARN  = 2,  /* 警告：潜在问题 */
    LOG_LEVEL_INFO  = 3,  /* 信息：关键流程 */
    LOG_LEVEL_DEBUG = 4,  /* 调试：详细信息 */
    LOG_LEVEL_TRACE = 5,  /* 跟踪：最详细 */
} LogLevel;

/* 编译时日志级别（发布版本可裁剪） */
#ifndef LOG_MAX_LEVEL
    #ifdef NDEBUG
        #define LOG_MAX_LEVEL  LOG_LEVEL_INFO
    #else
        #define LOG_MAX_LEVEL  LOG_LEVEL_DEBUG
    #endif
#endif
```

### 1.2 日志宏实现

```c
/* 基础日志宏 */
#define LOG_ERROR(fmt, ...) \
    LOG_WRITE(LOG_LEVEL_ERROR, "E", fmt, ##__VA_ARGS__)

#define LOG_WARN(fmt, ...) \
    LOG_WRITE(LOG_LEVEL_WARN, "W", fmt, ##__VA_ARGS__)

#define LOG_INFO(fmt, ...) \
    LOG_WRITE(LOG_LEVEL_INFO, "I", fmt, ##__VA_ARGS__)

#define LOG_DEBUG(fmt, ...) \
    LOG_WRITE(LOG_LEVEL_DEBUG, "D", fmt, ##__VA_ARGS__)

#define LOG_TRACE(fmt, ...) \
    LOG_WRITE(LOG_LEVEL_TRACE, "T", fmt, ##__VA_ARGS__)

/* 核心日志写入宏 */
#define LOG_WRITE(level, tag, fmt, ...) \
    do { \
        if ((level) <= LOG_MAX_LEVEL) { \
            log_write(level, tag, __FILE__, __LINE__, \
                      fmt, ##__VA_ARGS__); \
        } \
    } while (0)
```

---

## 2. 日志格式规范

### 2.1 标准日志格式

```
[时间戳] [级别] [模块] 消息内容
示例：
[00012345] [E] [UART] RX buffer overflow, dropped 3 bytes
[00012346] [I] [SENSOR] Temperature: 25.6°C
[00012347] [D] [SPI] TX: [AA BB CC DD]
```

### 2.2 日志函数实现

```c
#include <stdarg.h>
#include <stdio.h>

/* 日志输出接口（可重定向到不同后端） */
void log_write(LogLevel level, const char *tag,
               const char *file, int line,
               const char *fmt, ...)
{
    /* 1. 时间戳 */
    uint32_t tick = HAL_GetTick();
    printf("[%08lu] ", tick);

    /* 2. 日志级别 */
    printf("[%s] ", tag);

    /* 3. 模块名（从文件路径提取） */
    const char *module = extract_module_name(file);
    printf("[%s] ", module);

    /* 4. 格式化消息 */
    va_list args;
    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);

    /* 5. 换行 */
    printf("\n");

    /* 6. 同步输出（可选） */
    fflush(stdout);
}
```

### 2.3 二进制数据日志

```c
/* 十六进制 dump */
void log_hex_dump(LogLevel level, const char *module,
                  const void *data, size_t len)
{
    if (level > LOG_MAX_LEVEL) return;

    const uint8_t *bytes = (const uint8_t *)data;
    printf("[%s] HEX DUMP (%u bytes):\n", module, (unsigned)len);

    for (size_t i = 0; i < len; i++) {
        if (i % 16 == 0) {
            printf("  %04X: ", (unsigned)i);
        }
        printf("%02X ", bytes[i]);
        if (i % 16 == 15 || i == len - 1) {
            /* 打印 ASCII */
            printf(" |");
            for (size_t j = i - (i % 16); j <= i; j++) {
                printf("%c", (bytes[j] >= 32 && bytes[j] < 127) ?
                       bytes[j] : '.');
            }
            printf("|\n");
        }
    }
}

/* 使用示例 */
void process_packet(const uint8_t *pkt, size_t len)
{
    LOG_DEBUG("Received packet:");
    LOG_HEX_DUMP(LOG_LEVEL_DEBUG, "NET", pkt, len);
}
```

---

## 3. 日志输出后端

### 3.1 多后端支持

```c
typedef void (*LogBackend)(const char *msg, size_t len);

/* 注册多个输出后端 */
static LogBackend g_log_backends[4];
static uint8_t g_log_backend_count;

void log_register_backend(LogBackend backend)
{
    if (g_log_backend_count < ARRAY_SIZE(g_log_backends)) {
        g_log_backends[g_log_backend_count++] = backend;
    }
}

/* 同时输出到所有后端 */
void log_output_all(const char *msg, size_t len)
{
    for (int i = 0; i < g_log_backend_count; i++) {
        g_log_backends[i](msg, len);
    }
}
```

### 3.2 常见后端实现

```c
/* 后端1：UART 输出 */
void log_uart_backend(const char *msg, size_t len)
{
    HAL_UART_Transmit(&huart2, (uint8_t *)msg, len, 100);
}

/* 后端2：RTT（Segger Real-Time Transfer）输出 */
void log_rtt_backend(const char *msg, size_t len)
{
    SEGGER_RTT_Write(0, msg, len);
}

/* 后端3：SWO（Serial Wire Output）输出 */
void log_swo_backend(const char *msg, size_t len)
{
    for (size_t i = 0; i < len; i++) {
        ITM_SendChar(msg[i]);
    }
}

/* 后端4：环形缓冲区（用于后续读取） */
static RingBuffer_t g_log_ring;

void log_ring_backend(const char *msg, size_t len)
{
    ring_buffer_write(&g_log_ring, msg, len);
}
```

---

## 4. 日志缓冲与同步

### 4.1 异步日志

```c
/* 异步日志 — 不阻塞调用者 */
typedef struct {
    char message[256];
    uint32_t tick;
    LogLevel level;
} LogEntry_t;

static os_queue_t g_log_queue;

void log_async(LogLevel level, const char *fmt, ...)
{
    LogEntry_t entry = { .tick = HAL_GetTick(), .level = level };

    va_list args;
    va_start(args, fmt);
    vsnprintf(entry.message, sizeof(entry.message), fmt, args);
    va_end(args);

    /* 非阻塞发送到队列 */
    os_queue_send(&g_log_queue, &entry, 0);
}

/* 日志任务 — 从队列取出并输出 */
void task_logger(void *arg)
{
    LogEntry_t entry;
    while (1) {
        if (os_queue_receive(&g_log_queue, &entry, OS_WAIT_FOREVER) == OS_OK) {
            log_output_formatted(&entry);
        }
    }
}
```

### 4.2 日志队列满处理

```c
/* 队列满时的策略 */
typedef enum {
    LOG_QUEUE_DROP_NEW,      /* 丢弃新日志 */
    LOG_QUEUE_DROP_OLD,      /* 丢弃旧日志 */
    LOG_QUEUE_BLOCK,         /* 阻塞等待（可能影响实时性） */
} LogQueuePolicy;

static LogQueuePolicy g_queue_policy = LOG_QUEUE_DROP_NEW;

void log_enqueue(const LogEntry_t *entry)
{
    if (os_queue_send(&g_log_queue, entry, 0) != OS_OK) {
        if (g_queue_policy == LOG_QUEUE_DROP_NEW) {
            g_log_drop_count++;
        }
    }
}
```

---

## 5. 调试辅助工具

### 5.1 断言实现

```c
/* 增强版断言 — 记录详细信息 */
#ifdef NDEBUG
    #define ASSERT(cond) ((void)0)
#else
    #define ASSERT(cond) \
        do { \
            if (!(cond)) { \
                assert_failed(__FILE__, __LINE__, #cond); \
            } \
        } while (0)
#endif

void assert_failed(const char *file, int line, const char *expr)
{
    /* 禁用中断 */
    __disable_irq();

    /* 输出错误信息 */
    printf("\n*** ASSERTION FAILED ***\n");
    printf("  File: %s\n", file);
    printf("  Line: %d\n", line);
    printf("  Expression: %s\n", expr);
    printf("  Tick: %lu\n", HAL_GetTick());

    /* 记录到非易失存储（如果有） */
    save_crash_info(file, line, expr);

    /* 可选：等待调试器连接 */
    #ifdef DEBUG_WAIT_FOR_ATTACH
    while (!IsDebuggerAttached()) {}
    #endif

    /* 系统复位 */
    NVIC_SystemReset();
}
```

### 5.2 栈溢出检测

```c
/* 方法1：栈涂色（Stack Painting） */
#define STACK_MAGIC  0xDEADBEEF

void init_stack_guard(void)
{
    /* 在栈底写入魔数 */
    extern uint32_t _estack;
    uint32_t *guard = &_estack - 64;  /* 栈底上方 256 字节 */
    for (int i = 0; i < 64; i++) {
        guard[i] = STACK_MAGIC;
    }
}

bool check_stack_overflow(void)
{
    extern uint32_t _estack;
    uint32_t *guard = &_estack - 64;
    for (int i = 0; i < 64; i++) {
        if (guard[i] != STACK_MAGIC) {
            return true;  /* 栈溢出 */
        }
    }
    return false;
}

/* 方法2：MPU 硬件保护 */
void configure_mpu_stack_guard(void *stack_base, size_t guard_size)
{
    MPU_Region_InitTypeDef mpu = {
        .Number           = MPU_REGION_NUMBER0,
        .BaseAddress      = (uint32_t)stack_base,
        .Size             = MPU_REGION_SIZE_256B,
        .AccessPermission = MPU_REGION_NO_ACCESS,
        .TypeExtField     = MPU_TEX_LEVEL0,
        .IsBufferable     = MPU_ACCESS_NOT_BUFFERABLE,
        .IsCacheable      = MPU_ACCESS_NOT_CACHEABLE,
        .IsShareable      = MPU_ACCESS_NOT_SHAREABLE,
    };
    HAL_MPU_ConfigRegion(&mpu);
}
```

### 5.3 运行时统计

```c
/* 任务运行时间统计 */
typedef struct {
    uint32_t total_ticks;
    uint32_t max_ticks;
    uint32_t run_count;
} TaskStats_t;

static TaskStats_t g_task_stats[MAX_TASKS];

void trace_task_switch(uint8_t from_id, uint8_t to_id)
{
    uint32_t now = DWT->CYCCNT;

    /* 记录离开的任务运行时间 */
    if (from_id < MAX_TASKS) {
        uint32_t elapsed = now - g_task_switch_tick;
        g_task_stats[from_id].total_ticks += elapsed;
        if (elapsed > g_task_stats[from_id].max_ticks) {
            g_task_stats[from_id].max_ticks = elapsed;
        }
        g_task_stats[from_id].run_count++;
    }

    g_task_switch_tick = now;
}

/* 打印统计 */
void print_task_stats(void)
{
    printf("Task Statistics:\n");
    printf("  %-16s %10s %10s %10s\n",
           "Task", "Total", "Max", "Count");
    printf("  %-16s %10s %10s %10s\n",
           "----", "-----", "---", "-----");

    for (int i = 0; i < MAX_TASKS; i++) {
        if (g_task_stats[i].run_count > 0) {
            printf("  %-16s %10lu %10lu %10lu\n",
                   get_task_name(i),
                   g_task_stats[i].total_ticks,
                   g_task_stats[i].max_ticks,
                   g_task_stats[i].run_count);
        }
    }
}
```

---

## 6. 条件编译与调试

### 6.1 调试版本配置

```c
/* 调试版本启用额外检查 */
#ifdef DEBUG
    #define DEBUG_ASSERT(x)     ASSERT(x)
    #define DEBUG_LOG(fmt, ...) LOG_DEBUG(fmt, ##__VA_ARGS__)
    #define DEBUG_HEX_DUMP(...) log_hex_dump(__VA_ARGS__)
    #define DEBUG_TRACE_ENTER() LOG_TRACE(">>> %s", __func__)
    #define DEBUG_TRACE_EXIT()  LOG_TRACE("<<< %s", __func__)
#else
    #define DEBUG_ASSERT(x)     ((void)0)
    #define DEBUG_LOG(fmt, ...) ((void)0)
    #define DEBUG_HEX_DUMP(...) ((void)0)
    #define DEBUG_TRACE_ENTER() ((void)0)
    #define DEBUG_TRACE_EXIT()  ((void)0)
#endif
```

### 6.2 模块级日志控制

```c
/* 每个模块独立的日志级别 */
#define LOG_LEVEL_NET    LOG_LEVEL_DEBUG
#define LOG_LEVEL_SENSOR LOG_LEVEL_INFO
#define LOG_LEVEL_UI     LOG_LEVEL_WARN

/* 模块日志宏 */
#define LOG_NET_ERROR(fmt, ...) \
    LOG_MODULE(LOG_LEVEL_NET, LOG_LEVEL_ERROR, "NET", fmt, ##__VA_ARGS__)

#define LOG_NET_DEBUG(fmt, ...) \
    LOG_MODULE(LOG_LEVEL_NET, LOG_LEVEL_DEBUG, "NET", fmt, ##__VA_ARGS__)

#define LOG_MODULE(max, level, mod, fmt, ...) \
    do { \
        if ((level) <= (max) && (level) <= LOG_MAX_LEVEL) { \
            log_write(level, mod, __FILE__, __LINE__, \
                      fmt, ##__VA_ARGS__); \
        } \
    } while (0)
```

---

## 7. 故障记录与复位

### 7.1 崩溃信息保存

```c
/* 使用备份寄存器保存崩溃信息 */
typedef struct {
    uint32_t magic;           /* 魔数标识有效数据 */
    uint32_t reset_reason;    /* 复位原因 */
    uint32_t fault_addr;      /* 故障地址 */
    uint32_t fault_status;    /* 故障状态 */
    uint32_t stack_pointer;   /* 崩溃时的 SP */
    uint32_t registers[4];    /* R0-R3 */
    uint32_t timestamp;       /* 崩溃时间戳 */
} CrashInfo_t;

void save_crash_info(const char *file, int line, const char *expr)
{
    CrashInfo_t info = {
        .magic        = 0xDEADBEEF,
        .reset_reason = get_reset_reason(),
        .timestamp    = HAL_GetTick(),
    };

    /* 保存到备份寄存器（掉电保持） */
    HAL_PWR_EnableBkUpAccess();
    RTC->BKP0R = info.magic;
    RTC->BKP1R = info.reset_reason;
    RTC->BKP2R = info.timestamp;
    HAL_PWR_DisableBkUpAccess();
}

/* 启动时检查是否有崩溃记录 */
void check_crash_history(void)
{
    HAL_PWR_EnableBkUpAccess();
    if (RTC->BKP0R == 0xDEADBEEF) {
        LOG_ERROR("Previous crash detected:");
        LOG_ERROR("  Reset reason: 0x%08lX", RTC->BKP1R);
        LOG_ERROR("  Timestamp: %lu", RTC->BKP2R);

        /* 清除标记 */
        RTC->BKP0R = 0;
    }
    HAL_PWR_DisableBkUpAccess();
}
```

---

## 调试日志检查清单

- [ ] 日志级别在发布版本中适当裁剪
- [ ] 日志格式统一，包含时间戳和模块名
- [ ] 关键路径有 INFO 级别日志
- [ ] 错误处理有 ERROR 级别日志
- [ ] 二进制数据有 HEX DUMP 功能
- [ ] 断言记录文件名和行号
- [ ] 栈溢出检测已启用
- [ ] 崩溃信息可保存到非易失存储
- [ ] 日志输出不阻塞关键任务
- [ ] 调试版本有完整的运行时统计
