# 异常处理框架设计文档

## 1. 概述

本模块为 ARM Cortex-M 平台提供统一的异常处理框架，覆盖以下场景：

- **硬件异常**：HardFault、MemManage、BusFault、UsageFault
- **RTOS 钩子**：FreeRTOS 栈溢出、malloc 失败
- **断言机制**：`__ASSERT` 宏（需 `CONFIG_ASSERT=y`）

核心特性：
- **可插拔后端架构**：通过 `IBackend` 接口支持多种记录方式
- **noinit 故障记录**：复位后 RAM 保留，启动时自动输出
- **帧指针回溯**：保存 16 级调用栈地址
- **栈内存快照**：保存 128 字节原始栈数据
- **零依赖**：不依赖 RTOS、libc 或动态内存

## 2. 架构

```
vector_table.S (弱别名 → Default_Handler)
    │
    ▼
fault.S (汇编存根，覆盖弱别名)
    ├── HardFault_Handler  (.weak，RT-Thread 可覆盖)
    ├── MemManage_Handler  (.global)
    ├── BusFault_Handler   (.global)
    └── UsageFault_Handler (.global)
         │
         │  TST lr, #4 → MSP/PSP → r0=FaultFrame*
         │  PUSH {r4-r11} → SUB r0, r0, #32 → r1=EXC_RETURN
         ▼
fault.cc (C++ 实现，RTOS 无关)
    ├── buildRecord()        — 构建 FaultRecord
    ├── 遍历已注册后端 → onFault(rec)
    ├── bootCheck()          — 启动时遍历后端 → onBoot()
    ├── assert_post_action   — 断言失败处理
    └── FreeRTOS hooks       — 栈溢出/malloc 失败
```

### 可插拔后端

```
┌─────────────────────────────────────────────┐
│              hal::fault::IBackend           │
│  ┌───────────────────────────────────────┐  │
│  │  onFault(rec)  — fault 时调用         │  │
│  │  onBoot()      — 启动时调用           │  │
│  │  clear()       — 清除记录             │  │
│  └───────────────────────────────────────┘  │
└─────────────────────────────────────────────┘
         ▲              ▲              ▲
         │              │              │
  NoinitBackend   UartBackend    FlashBackend(TODO)
  (.noinit RAM)   (putc 弱符号)  (Flash 分区)
```

默认后端通过懒初始化自动注册，无需手动调用 `registerBackend()`。

## 3. 文件结构

| 文件 | 说明 |
|------|------|
| `embedded/include/arch/arm/cortex_m/fault.h` | 头文件：数据结构、IBackend 接口、工具函数 |
| `embedded/arch/arm/core/cortex_m/fault.S` | 汇编存根：异常入口 |
| `embedded/arch/arm/core/cortex_m/fault.cc` | C++ 实现：后端、记录构建、输出 |

## 4. 数据结构

### FaultRecord（~264 字节）

```cpp
struct Frame {
    uint32_t r0, r1, r2, r3;     // 硬件自动压栈
    uint32_t r12, lr, pc, xpsr;  // 硬件自动压栈
    uint32_t r4, r5, r6, r7;     // fault.S 手动压栈
    uint32_t r8, r9, r10, r11;   // fault.S 手动压栈
};

struct FaultRecord {
    uint32_t magic;              // 0xFA17FAUL = 有效记录
    uint32_t cfsr;               // Configurable Fault Status Register
    uint32_t hfsr;               // HardFault Status Register
    uint32_t mmfar;              // MemManage Fault Address
    uint32_t bfar;               // BusFault Address
    uint32_t excReturn;          // EXC_RETURN 值
    Frame    frame;              // 完整寄存器快照（r0-r11, r12, lr, pc, xpsr）

    uint32_t msp;                // Main Stack Pointer
    uint32_t psp;                // Process Stack Pointer
    uint32_t backtrace[16];      // 帧指针回溯的返回地址
    int      backtraceDepth;     // 有效条目数
    uint32_t stackSnapshot[32];  // 栈内存快照（128 字节）
    uint32_t snapshotSp;         // 快照来源 SP
};
```

### CFSR 位域

CFSR 包含三个子域，每个位代表一种故障原因：

| 子域 | 位范围 | 含义 |
|------|--------|------|
| MMFSR | [7:0] | MemManage Fault Status |
| BFSR | [15:8] | BusFault Status |
| UFSR | [31:16] | UsageFault Status |

常见位含义：
- `IACCVIOL`：指令访问违规
- `DACCVIOL`：数据访问违规
- `IBUSERR`：指令总线错误
- `PRECISERR`：精确数据总线错误
- `UNDEFINSTR`：未定义指令
- `INVSTATE`：无效状态（Thumb 位错误）
- `DIVBYZERO`：除零
- `UNALIGNED`：未对齐访问

### HFSR 位域

| 位 | 含义 |
|----|------|
| VECTTBL | 向量表读取错误 |
| FORCED | 由可配置 fault 升级为 HardFault |
| DEBUGEVT | 调试事件触发 |

## 5. 异常处理流程

### 5.1 Fault 发生时

```
1. 硬件自动压栈 r0-r3, r12, lr, pc, xpsr 到当前 SP
2. 硬件设置 CFSR/HFSR 等状态寄存器
3. 跳转到 vector_table.S 中的异常入口
4. fault.S 汇编存根：
   a. TST lr, #0x04 判断 MSP/PSP
   b. 将硬件压栈帧地址放入 r0
   c. PUSH {r4-r11} 手动保存callee-saved寄存器
   d. SUB r0, r0, #32 调整 r0 指向完整 Frame 起始地址
   e. 将 EXC_RETURN 放入 r1
   f. BL arm_fault_handler
5. fault.cc::arm_fault_handler()：
   a. ensureInitialized() — 懒初始化注册默认后端
   b. buildRecord() — 构建完整 FaultRecord
      - 读取 SCB->CFSR/HFSR/MMFAR/BFAR
      - 帧指针回溯（r7 或 r11）
      - 栈内存快照
   c. 遍历所有后端 → onFault(rec)
      - NoinitBackend：写入 .noinit RAM
      - UartBackend：formatRecord() → putc() 逐字符输出（异常上下文安全）
   d. 死循环（cpsid i + wfi）
```

### 5.2 启动时

```
1. main() 中调用 hal::fault::bootCheck()
2. bootCheck() 遍历所有后端 → onBoot()
3. UartBackend::onBoot()：
   a. 检查 s_faultRecord.magic == MAGIC
   b. 有记录 → 通过 log_write(LogLevel::Error) 输出 "===== LAST FAULT (noinit) ====="
   c. 输出完整诊断信息
4. 应用层可调用 hal::fault::clear() 清除记录
```

### 5.3 输出策略（双路径）

故障模块采用双路径输出策略，根据执行上下文选择不同的输出方式：

| 上下文 | 输出方式 | 原因 |
|--------|----------|------|
| 异常/中断上下文（`onFault`） | `putc()` 逐字符输出 | 不依赖 RTOS，无信号量/锁，安全可靠 |
| 正常线程上下文（`onBoot`/`dump`） | `log_write()` 格式化输出 | 走 LOG 模块统一通道，支持日志级别/标签过滤 |

**安全约束**：LOG 模块的 UART 后端使用 `osal::Semaphore`（FreeRTOS 信号量），在异常上下文中调用会导致死锁或未定义行为。因此 `onFault()` 必须使用低级 `putc` 输出。

**共享格式化**：`formatRecord()` 函数将 `FaultRecord` 格式化为字符串缓冲区（使用 `vsnprintf`），两种输出路径共享同一格式化逻辑。

### 5.4 帧指针回溯原理

ARM Thumb-2 下，使用 `-fno-omit-frame-pointer` 编译时：
- 函数 prologue 执行 `push {r7, lr}` 和 `mov r7, sp`
- `r7` 指向当前栈帧的帧记录
- `[r7+0]` = 保存的上一帧 r7（帧指针链）
- `[r7+4]` = 返回地址（调用者的 PC）

回溯算法：
```
fp = frame->r7 (PSP 模式) 或 frame->r11 (MSP 模式)
while (fp != 0 && fp 对齐):
    retAddr = *(fp + 4)
    保存 retAddr 到 backtrace[]
    fp = *fp  // 上一帧
```

注意：r7/r11 由 fault.S 中 `PUSH {r4-r11}` 保存到 Frame 结构体中。

## 6. 后端接口

### IBackend

```cpp
class IBackend {
public:
    virtual ~IBackend() = default;
    virtual void onFault(const FaultRecord &rec) = 0;  // fault 时调用
    virtual void onBoot() {}                            // 启动时调用
    virtual void clear() {}                             // 清除记录
};
```

### 注册自定义后端

```cpp
// 在 main() 中注册
hal::fault::registerBackend(&myFlashBackend);
hal::fault::registerBackend(&myRttBackend);
```

最多支持 `MAX_BACKENDS = 4` 个后端。

### 实现 Flash 后端（TODO）

```cpp
class FlashBackend : public hal::fault::IBackend {
public:
    void onFault(const hal::fault::FaultRecord &rec) override {
        // 在 fault 上下文中写 Flash（需关闭中断）
        // 注意：Flash 擦写时间可能较长，需权衡
        flash_erase(FAULT_PARTITION);
        flash_write(FAULT_PARTITION, 0, &rec, sizeof(rec));
    }
    void onBoot() override {
        // 从 Flash 读取记录
        hal::fault::FaultRecord rec;
        flash_read(FAULT_PARTITION, 0, &rec, sizeof(rec));
        if (rec.valid()) {
            // 处理记录（打印/上传）
            flash_erase(FAULT_PARTITION);
        }
    }
};
```

注意事项：
- fault 上下文中 Flash 操作需确保时钟已初始化
- Flash 擦写期间不能被中断打断
- 建议使用独立的 Flash 分区，避免与 NVS/filesystem 冲突
- 可考虑 CRC 校验防止写入中断导致的数据损坏

## 7. FreeRTOS 集成

### 启用的钩子

```c
// FreeRTOSConfig.h
#define configCHECK_FOR_STACK_OVERFLOW  2  // 方法 2：检查栈溢出哨兵
#define configUSE_MALLOC_FAILED_HOOK    1  // malloc 失败钩子
```

### 栈溢出检测

FreeRTOS 方法 2 在上下文切换时检查任务栈末尾的 20 字节哨兵模式。如果被覆盖，调用 `vApplicationStackOverflowHook()`。

### SysTick 映射

```c
#define xPortSysTickHandler SysTick_Handler
```

确保 FreeRTOS port 的 SysTick 处理函数正确映射到 vector_table.S 中的入口。

## 8. 断言机制

### 启用方式

```
# prj.conf
CONFIG_ASSERT=y
CONFIG_ASSERT_LEVEL=2
```

### 工作原理

1. `__ASSERT(test, fmt, ...)` 检查条件
2. 失败时调用 `assert_print()` 打印文件名和行号
3. 然后调用 `assert_post_action(file, line)` 进入死循环

`assert_print()` 和 `assert_post_action()` 均在 `fault.cc` 中实现。

## 9. VTOR 向量表重定位

```c
// prep_c.c
SCB->VTOR = VECTOR_ADDRESS & VTOR_MASK;
```

将向量表基地址写入 SCB->VTOR，确保异常向量指向正确的 handler。对于 STM32H7 等 Flash 地址非零的芯片，这是必须的。

## 10. 编译要求

帧指针回溯需要编译器保留帧指针：

```cmake
target_compile_options(arch PRIVATE -fno-omit-frame-pointer)
```

不加此选项时回溯可能不完整，但栈快照始终可用（可离线用 GDB 手动解析）。

## 11. 输出示例

### Fault 输出

```
===== FAULT =====
R0 : 0x00000000
R1 : 0x20001000
R2 : 0x00000001
R3 : 0x00000000
R12: 0x00000000
LR : 0x08001234
PC : 0x08001238
xPSR: 0x61000000
EXC_RETURN: 0xFFFFFFFD
CFSR: 0x00000082
HFSR: 0x40000000
  MemManage: DACCVIOL MMFAR=0x00000000
  HFSR: FORCED
MSP: 0x20010000
PSP: 0x20008000
Backtrace (3 frames):
  #0 0x08001234
  #1 0x0800ABCD
  #2 0x08005678
Stack @ 0x20008000:
0x20008000: 0xDEADBEEF 0xCAFEBABE 0x00000001 0x20001000
0x20008010: 0x08001234 0x00000000 0x00000000 0x00000000
...
==================
```

### Noinit 记录输出（热复位后）

```
===== LAST FAULT (noinit) =====
... (同上格式) ...
================================
```

### Assert 输出

```
ASSERT FAIL @ src/main.cc:42
```

### 栈溢出输出

```
STACK OVERFLOW: my_task
```

## 12. 设计决策

| 决策 | 原因 |
|------|------|
| 懒初始化替代静态构造函数 | 嵌入式系统中 C++ 静态构造函数执行时机不确定 |
| `HardFault_Handler` 使用 `.weak` | RT-Thread 的 `context_gcc.S` 需要强符号覆盖 |
| `MemManage/BusFault/UsageFault` 使用 `.global` | 覆盖 vector_table.S 中的弱别名 |
| `putc` 使用弱符号 | SoC 层可覆盖为 UART 寄存器直写 |
| noinit 记录放在 `.noinit` 段 | 热复位后 RAM 内容保留，冷启动丢失 |
| FaultRecord 固定大小 | 避免动态内存分配，fault 上下文中不能调用 malloc |
| 帧指针回溯 + 栈快照双保险 | 帧指针回溯依赖编译选项，栈快照始终可用 |
| 双路径输出（`putc` + `log_write`） | 异常上下文中 LOG 模块的信号量不安全，`onFault` 用 `putc`；`onBoot`/`dump` 用 `log_write` 走统一日志通道 |
| `formatRecord()` 共享格式化 | `vsnprintf` 构建缓冲区，两种输出路径复用同一格式化逻辑，避免代码重复 |
