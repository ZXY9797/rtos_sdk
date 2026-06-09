
# C++ 嵌入式编程参考

C++ 在嵌入式系统中可以提供比 C 更强的类型安全和零开销抽象，
但必须严格控制语言特性的使用范围。本文档规定了嵌入式 C++ 的
允许特性、禁止特性和最佳实践。

---

## 1. 编译器标志与标准选择

### 必须的编译器标志

```makefile
# 禁用异常
-fno-exceptions
# 禁用 RTTI（运行时类型识别）
-fno-rtti
# 禁用线程安全的静态局部变量初始化（节省代码体积）
-fno-threadsafe-statics
# 使用 C++17（推荐）或 C++14
-std=c++17
```

### 标准选择建议

| 标准 | 推荐度 | 理由 |
|------|-------|------|
| C++11 | 可用 | 基础特性足够，移动语义、lambda、constexpr |
| C++14 | 推荐 | constexpr 增强，泛型 lambda |
| C++17 | 推荐 | `std::optional`、`std::variant`、`if constexpr`、结构化绑定 |
| C++20 | 谨慎 | 概念（concepts）和模块支持尚不成熟 |

---

## 2. 零开销抽象原则

C++ 的核心价值是"你不为不使用的特性付费"。在嵌入式中，
这意味着只使用编译期解析或零额外运行时开销的特性。

### 允许的零开销特性

| 特性 | 开销 | 替代的 C 模式 |
|------|------|-------------|
| `constexpr` | 零（编译期求值） | 宏常量、编译期魔法数字 |
| 模板 | 零（编译期实例化） | `void*` + 函数指针的类型擦除 |
| `enum class` | 零 | 带前缀的 C 枚举 |
| `inline` 函数 | 零（编译器内联） | 宏函数 |
| `static_assert` | 零（编译期检查） | 无对应（运行时才报错） |
| `auto` | 零（类型推导） | 显式类型声明 |
| 命名空间 | 零 | 模块名前缀 |
| RAII | 零（析构函数调用） | 手动 init/deinit |
| `constexpr if` | 零（编译期分支） | 条件编译宏 |

### 需要评估的特性

| 特性 | 开销 | 何时可用 |
|------|------|---------|
| 虚函数 | 4-8字节 vptr + 间接调用 | 需要运行时多态时 |
| `std::function` | 堆分配 + 类型擦除 | **禁止**（用函数指针或模板） |
| `new`/`delete` | 堆管理开销 | **尽量避免**（见堆规则） |
| 异常 | 代码体积 + 栈展开 | **禁止** |
| RTTI | `typeid`/`dynamic_cast` | **禁止** |

---

## 3. 禁用的 C++ 特性

### 3.1 异常（禁止）

```makefile
-fno-exceptions
```

异常在嵌入式系统中是不可接受的：
- 代码体积膨胀（栈展开表）
- 执行时间不可预测
- 破坏实时性保证

**替代方案：**

```cpp
/* 方案1：错误码（与C相同） */
enum class ErrorCode : int32_t {
    Ok            =  0,
    NullPointer   = -1,
    InvalidParam  = -2,
    Timeout       = -3,
};

/* 方案2：std::optional（C++17） */
std::optional<SensorData> read_sensor()
{
    if (!hw_ready()) {
        return std::nullopt;  /* 无值 */
    }
    return SensorData{raw_to_celsius(read_adc())};
}

/* 方案3：Expected 模式（自定义或 C++23 std::expected） */
template<typename T, typename E = ErrorCode>
class Expected {
    bool has_value_;
    union {
        T value_;
        E error_;
    };
public:
    bool has_value() const { return has_value_; }
    const T& value() const { return value_; }
    E error() const { return error_; }
};
```

### 3.2 RTTI（禁止）

```makefile
-fno-rtti
```

禁止使用 `dynamic_cast`、`typeid` 和 `type_info`。

**替代方案：**

```cpp
/* 方案1：虚函数 + 多态（推荐） */
class Device {
public:
    virtual DeviceType type() const = 0;
    /* ... */
};

/* 方案2：枚举类型标识 */
enum class DeviceType : uint8_t {
    Uart, Spi, I2c, Gpio
};

struct Device {
    DeviceType type;
    /* ... */
};
```

### 3.3 标准库受限

以下标准库组件在嵌入式中**禁止或不推荐使用**：

| 组件 | 原因 | 替代 |
|------|------|------|
| `std::string` | 堆分配 | `std::string_view`、固定 `char[]` |
| `std::vector` | 堆分配 | `std::array`、自定义容器 |
| `std::map`/`set` | 堆分配 + 红黑树 | 排序数组 + 二分查找 |
| `std::shared_ptr` | 堆分配 + 引用计数 | 所有权语义 + RAII |
| `std::function` | 堆分配 + 类型擦除 | 函数指针、模板参数 |
| `std::cout` | 代码体积巨大 | 自定义日志函数 |
| `std::thread` | 依赖 OS | RTOS 线程 API |
| `std::mutex` | 依赖 OS | RTOS 互斥锁 API |

**可安全使用的标准库：**

| 组件 | 说明 |
|------|------|
| `<type_traits>` | 编译期类型操作，零开销 |
| `<cstdint>` | 固定宽度整数类型 |
| `<cstring>` | C 字符串操作 |
| `<algorithm>` | `std::sort`、`std::find` 等（编译期实例化） |
| `<array>` | `std::array`，栈分配，带边界检查 |
| `<optional>` | C++17，替代可空指针 |
| `<variant>` | C++17，替代 union + 类型标签 |
| `<string_view>` | C++17，非拥有字符串引用 |
| `<ratio>` | 编译期有理数运算 |
| `<numeric>` | `std::accumulate` 等 |

---

## 4. RAII 资源管理

RAII（Resource Acquisition Is Initialization）是 C++ 最强大的
嵌入式特性之一。它通过构造/析构函数自动管理资源生命周期，
消除资源泄漏和忘记释放的问题。

### 4.1 锁守卫

```cpp
class MutexGuard {
    os_mutex_t &mutex_;
public:
    explicit MutexGuard(os_mutex_t &m) : mutex_(m)
    {
        os_mutex_lock(&mutex_);
    }

    ~MutexGuard()
    {
        os_mutex_unlock(&mutex_);
    }

    /* 禁止拷贝和移动 */
    MutexGuard(const MutexGuard &) = delete;
    MutexGuard &operator=(const MutexGuard &) = delete;
};

/* 使用 — 自动加锁/解锁，包括异常路径 */
void process_shared_data(SharedData &data)
{
    MutexGuard lock(data_mutex);
    data.counter++;
    data.buffer[0] = 0xFF;
    /* 函数返回时自动解锁 */
}
```

### 4.2 作用域退出守卫

```cpp
class ScopeGuard {
    std::function<void()> action_;
    bool active_ = true;
public:
    explicit ScopeGuard(std::function<void()> action)
        : action_(std::move(action)) {}

    ~ScopeGuard()
    {
        if (active_) {
            action_();
        }
    }

    void dismiss() { active_ = false; }

    ScopeGuard(const ScopeGuard &) = delete;
    ScopeGuard &operator=(const ScopeGuard &) = delete;
};

/* 使用示例 */
int init_hardware()
{
    enable_clocks();
    ScopeGuard clk_cleanup([]{ disable_clocks(); });

    if (!configure_pll()) {
        return ERR_HW_FAULT;
        /* 自动调用 disable_clocks() */
    }

    clk_cleanup.dismiss(); /* 成功，取消清理 */
    return ERR_OK;
}
```

### 4.3 禁止在析构函数中阻塞

**关键规则：** 析构函数中不得调用可能阻塞的函数。
如果 RAII 对象持有需要异步释放的资源（如 DMA 传输），
在析构函数中启动取消操作但不等待完成。

```cpp
/* 错误 — 析构函数中等待 */
~DmaTransfer() {
    while (!transfer_complete_) {}  /* 禁止！ */
}

/* 正确 — 启动取消，不等待 */
~DmaTransfer() {
    if (!transfer_complete_) {
        abort_dma_transfer(channel_);
    }
}
```

---

## 5. constexpr 编译期计算

`constexpr` 可以在编译期完成计算，消除运行时开销和宏的类型安全问题。

### 5.1 替代宏常量

```cpp
/* C 风格宏 — 无类型安全 */
#define BAUD_RATE       (115200U)
#define TX_BUF_SIZE     (256U)

/* C++ constexpr — 有类型安全 */
constexpr uint32_t kBaudRate = 115200;
constexpr size_t   kTxBufSize = 256;
```

### 5.2 编译期查找表

```cpp
/* 编译期生成 CRC 表 */
constexpr uint32_t crc32_entry(uint8_t byte)
{
    uint32_t crc = byte;
    for (int i = 0; i < 8; i++) {
        crc = (crc >> 1) ^ (0xEDB88320 & (-(crc & 1)));
    }
    return crc;
}

constexpr std::array<uint32_t, 256> make_crc_table()
{
    std::array<uint32_t, 256> table{};
    for (uint32_t i = 0; i < 256; i++) {
        table[i] = crc32_entry(static_cast<uint8_t>(i));
    }
    return table;
}

/* 编译期生成，零运行时开销 */
static constexpr auto CRC_TABLE = make_crc_table();
```

### 5.3 编译期断言

```cpp
/* 验证配置在编译期 */
static_assert(sizeof(DeviceConfig) == 64,
              "DeviceConfig must be exactly 64 bytes");
static_assert(alignof(DmaBuffer) >= 32,
              "DmaBuffer must be 32-byte aligned for DMA");

/* 验证编译器标志 */
#ifndef __cpp_exceptions
static_assert(true, "Exceptions disabled — OK");
#else
#error "Exceptions must be disabled (-fno-exceptions)"
#endif
```

---

## 6. enum class 替代 C 风格枚举

```cpp
/* C 风格 — 命名污染，隐式转换为整数 */
typedef enum {
    STATE_IDLE,
    STATE_RUNNING,
    STATE_ERROR,
} State_t;

/* C++ — 强类型，作用域限定 */
enum class State : uint8_t {
    Idle,
    Running,
    Error,
};

/* 使用时必须带作用域前缀 */
State current = State::Idle;
if (current == State::Running) { /* ... */ }

/* 禁止隐式转换 */
int x = State::Idle;  /* 编译错误 */

/* 需要显式转换时 */
int x = static_cast<int>(State::Idle);
```

---

## 7. 模板与 CRTP 静态多态

### 7.1 虚函数 vs CRTP 开销对比

| 特性 | 虚函数 | CRTP |
|------|--------|------|
| 多态方式 | 运行时（vtable 查找） | 编译期（模板实例化） |
| 每对象开销 | 4-8字节 vptr | 零 |
| 调用开销 | 间接跳转（~2-3周期） | 直接调用（可内联） |
| 代码体积 | 共享一份实现 | 每类型一份实现 |
| 适用场景 | 运行时确定类型 | 编译时确定类型 |

### 7.2 CRTP 示例

```cpp
/* 基类模板 */
template<typename Derived>
class SensorBase {
public:
    int read(int32_t &value)
    {
        return static_cast<Derived*>(this)->read_impl(value);
    }

    int init()
    {
        return static_cast<Derived*>(this)->init_impl();
    }
};

/* 具体实现 — 零虚函数开销 */
class NtcSensor : public SensorBase<NtcSensor> {
    friend class SensorBase<NtcSensor>;
private:
    int read_impl(int32_t &value)
    {
        /* NTC 特有实现 */
        return 0;
    }

    int init_impl()
    {
        /* NTC 特有初始化 */
        return 0;
    }
};

/* 编译期多态 — 可内联 */
template<typename Sensor>
int calibrate(Sensor &sensor)
{
    int32_t raw;
    sensor.read(raw);  /* 直接调用，无间接跳转 */
    return raw * CALIBRATION_FACTOR;
}
```

### 7.3 何时使用虚函数

当且仅当需要**运行时**确定具体类型时使用虚函数：
- 设备类型在运行时由硬件检测决定
- 插件/驱动可动态加载
- 接口在编译时无法确定所有实现

```cpp
/* 适合虚函数的场景 — 存储设备在运行时确定 */
class Storage {
public:
    virtual ~Storage() = default;  /* 必须有虚析构函数 */
    virtual int read(uint32_t addr, uint8_t *buf, size_t len) = 0;
    virtual int write(uint32_t addr, const uint8_t *buf, size_t len) = 0;
};
```

---

## 8. 嵌入式内存管理

### 8.1 堆使用规则

1. **优先栈分配和静态分配** — 绝不在 ISR 中使用堆
2. **必须使用堆时** — 仅在初始化阶段分配，运行时不分配/释放
3. **自定义分配器** — 替代全局 `new`/`delete`

```cpp
/* 禁止 — 运行时动态分配 */
void process_data()
{
    auto *buf = new uint8_t[256];  /* 禁止！ */
    /* ... */
    delete[] buf;
}

/* 正确 — 静态/栈分配 */
void process_data()
{
    std::array<uint8_t, 256> buf;  /* 栈分配 */
    /* ... */
}

/* 正确 — 初始化时分配，运行时复用 */
class Driver {
    uint8_t *tx_buf_;
public:
    int init()
    {
        tx_buf_ = static_cast<uint8_t*>(
            platform_malloc(TX_BUF_SIZE));
        return tx_buf_ ? ERR_OK : ERR_NO_MEMORY;
    }

    ~Driver()
    {
        platform_free(tx_buf_);
    }
};
```

### 8.2 placement new

当需要在预分配内存中构造对象时：

```cpp
#include <new>  /* placement new */

/* 预分配的内存池 */
alignas(4) static uint8_t device_pool[4][sizeof(UartDriver)];

void create_drivers()
{
    for (int i = 0; i < 4; i++) {
        /* 在预分配内存中构造对象 */
        new (&device_pool[i]) UartDriver(i);
    }
}
```

### 8.3 内存布局控制

```cpp
/* 对齐控制 — DMA 缓冲区必须对齐 */
alignas(32) uint8_t dma_buffer[1024];

/* 结构体紧凑布局 — 硬件寄存器映射 */
struct __attribute__((packed)) HwRegister {
    uint32_t cr;     /* 控制寄存器 */
    uint32_t sr;     /* 状态寄存器 */
    uint16_t dr;     /* 数据寄存器 */
};
static_assert(offsetof(HwRegister, cr) == 0);
static_assert(offsetof(HwRegister, sr) == 4);
static_assert(offsetof(HwRegister, dr) == 8);
```

---

## 9. C/C++ 混合编程

### 9.1 extern "C" 链接规范

C++ 编译器会对符号名进行修饰（name mangling），
与 C 代码交互时必须使用 `extern "C"` 声明。

```cpp
/* C++ 文件中调用 C 函数 */
extern "C" {
#include "platform_device.h"
#include "rtos_api.h"
}

/* C++ 函数暴露给 C 调用 */
#ifdef __cplusplus
extern "C" {
#endif

void cpp_callback_handler(uint32_t event);
int  cpp_module_init(void);

#ifdef __cplusplus
}
#endif
```

### 9.2 头文件兼容性

所有被 C 和 C++ 共同使用的头文件必须包含链接规范保护：

```c
/* common_header.h */
#ifndef COMMON_HEADER_H
#define COMMON_HEADER_H

#ifdef __cplusplus
extern "C" {
#endif

/* C 兼容的声明 */
typedef struct Device Device_t;
int Device_Init(Device_t *dev);

#ifdef __cplusplus
}
#endif

#endif /* COMMON_HEADER_H */
```

### 9.3 C++ 调用 C 代码的规则

1. C 头文件用 `extern "C"` 包裹 `#include`
2. 不要将 C 结构体的指针传给期望 C++ 类的函数
3. 不要在 C++ 代码中对 C 结构体使用 C++ 特性（如成员函数）
4. 共享的错误码枚举使用 C 兼容的 `typedef enum`

---

## 10. 中断上下文中的 C++ 限制

在 ISR（中断服务例程）中使用 C++ 代码时，以下限制适用：

### 禁止的操作

1. **禁止 `new`/`delete`** — 堆操作不可重入且可能阻塞
2. **禁止虚函数调用** — vtable 查找在某些架构上非原子操作
3. **禁止 `std::function`** — 可能涉及堆分配
4. **禁止异常** — 即使编译时未禁用，ISR 中也不能抛出异常
5. **禁止互斥锁** — ISR 不能等待锁，只能用临界区
6. **禁止 STL 容器操作** — 可能涉及堆分配

### ISR 安全的 C++ 代码

```cpp
/* ISR 安全 — 纯栈操作，无虚函数，无堆 */
extern "C" void USART1_IRQHandler()
{
    /* 读取硬件寄存器 */
    uint32_t sr = USART1->SR;
    uint8_t  dr = USART1->DR;

    /* 简单数据处理 */
    if (sr & USART_SR_RXNE) {
        isr_rx_buffer[isr_rx_count++] = dr;
        if (isr_rx_count >= RX_BUF_SIZE) {
            isr_rx_count = 0;
        }
    }

    /* 释放信号量（ISR 安全 API） */
    osSemaphoreRelease(rx_sem);
}
```

---

## 11. 嵌入式 C++ 命名约定

在 C 语言命名约定的基础上，补充以下 C++ 特有约定：

| 元素 | 约定 | 示例 |
|-----|------|------|
| 类名 | CamelCase | `UartDriver`、`SensorBase` |
| 成员函数 | camelCase | `readData()`、`init()` |
| 成员变量 | camelCase + 尾部下划线 | `tx_buf_`、`is_open_` |
| 常量 | `k` 前缀 + CamelCase | `kMaxBaudRate`、`kBufSize` |
| 模板参数 | CamelCase | `typename Derived`、`typename T` |
| 命名空间 | 小写蛇形 | `drivers::uart` |
| 枚举类 | CamelCase 成员 | `State::Idle`、`State::Running` |

---

## 12. C vs C++ 选择指南

### 使用 C 的场景

- 纯硬件抽象层（HAL）代码
- 与 C 语言 SDK/库紧密集成
- 团队 C++ 经验不足
- 极度受限的 MCU（< 16KB Flash）

### 使用 C++ 的场景

- 应用层业务逻辑（状态机、协议处理）
- 需要强类型安全的接口
- 多个类似设备驱动（模板消除重复）
- 需要 RAII 管理资源的复杂模块

### 混合使用的最佳实践

```
HAL 层        → C（厂商提供）
Platform 层   → C（与硬件紧密耦合）
驱动层        → C++（OOP + RAII + 模板）
中间件层      → C++（类型安全 + 抽象）
应用层        → C++（状态机 + 业务逻辑）
```

---

## 13. Rule of Five（C++11 起）

如果类管理资源（堆内存、硬件句柄、DMA 通道等），
必须显式定义或删除以下五个特殊成员函数：

```cpp
class DmaChannel {
    uint8_t *buffer_;
    size_t   size_;
public:
    /* 构造函数 — 获取资源 */
    explicit DmaChannel(size_t size)
        : buffer_(static_cast<uint8_t*>(platform_malloc(size)))
        , size_(size) {}

    /* 析构函数 — 释放资源 */
    ~DmaChannel()
    {
        platform_free(buffer_);
    }

    /* 拷贝构造 — 禁止（资源不可共享） */
    DmaChannel(const DmaChannel &) = delete;
    DmaChannel &operator=(const DmaChannel &) = delete;

    /* 移动构造 — 转移所有权 */
    DmaChannel(DmaChannel &&other) noexcept
        : buffer_(other.buffer_)
        , size_(other.size_)
    {
        other.buffer_ = nullptr;
        other.size_ = 0;
    }

    DmaChannel &operator=(DmaChannel &&other) noexcept
    {
        if (this != &other) {
            platform_free(buffer_);
            buffer_ = other.buffer_;
            size_ = other.size_;
            other.buffer_ = nullptr;
            other.size_ = 0;
        }
        return *this;
    }
};
```

---

## 14. Lambda 表达式（嵌入式适用）

Lambda 在嵌入式中可用于回调和中断处理，但有约束：

### 允许的用法

```cpp
/* 无捕获 lambda — 可转换为函数指针 */
void register_callback(void (*cb)(uint32_t));

register_callback([](uint32_t event) {
    handle_event(event);
});

/* 带捕获 lambda — 仅用于栈上短暂使用 */
void process(std::array<int, 10> &data, int threshold)
{
    auto count = std::count_if(data.begin(), data.end(),
        [threshold](int val) { return val > threshold; });
    /* ... */
}
```

### 禁止的用法

```cpp
/* 禁止 — 捕获 lambda 存储在类成员中（生命周期问题） */
class Driver {
    std::function<void()> callback_;  /* 禁止 std::function */
};

/* 禁止 — lambda 捕获了局部引用并逃逸 */
auto make_callback(int &value)
{
    return [&value]() { return value; };  /* 悬挂引用！ */
}
```

---

## 嵌入式 C++ 检查清单

编写 C++ 嵌入式代码时，验证以下内容：

- [ ] 编译器标志包含 `-fno-exceptions` 和 `-fno-rtti`
- [ ] 未使用 `new`/`delete`（或仅在初始化阶段使用）
- [ ] 未使用 `std::function`、`std::string`、`std::vector`
- [ ] 未使用 `dynamic_cast`、`typeid`
- [ ] 虚函数类有虚析构函数
- [ ] 管理资源的类遵循 Rule of Five
- [ ] 析构函数中无阻塞调用
- [ ] ISR 中无虚函数调用、无堆操作、无 STL 容器操作
- [ ] `constexpr` 优先于宏定义常量
- [ ] `enum class` 优先于 C 风格枚举
- [ ] `nullptr` 优先于 `NULL` 或 `0`
- [ ] 共享头文件有 `extern "C"` 保护
- [ ] RAII 用于锁、文件句柄等资源管理
