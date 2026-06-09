# OSAL 抽象层

## 概述

`osal.h` 是纯 C++ 接口，不包含任何 RTOS 特定宏。所有 OS 相关的类型定义和常量集中在各 RTOS 的 `osal_types.h` 中。

## 设计原则

- **头文件零 RTOS 依赖**：`osal.h` 无条件编译宏
- **类型隔离**：OS 特定常量在 `osal_types.h` 中定义
- **多内核支持**：FreeRTOS、RT-Thread

## 目录结构

```
include/osal/
├── osal.h                  ← 纯 C++，无 #ifdef configMAX_PRIORITIES
├── osal_types.h            ← 类型选择（根据 CONFIG_* 选择具体实现）
├── thread.h                ← 线程接口
├── mutex.h                 ← 互斥锁接口
└── semaphore.h             ← 信号量接口

osal/
├── freertos/
│   ├── osal_types.h        ← FreeRTOS 类型 + 常量
│   ├── thread.cc
│   ├── mutex.cc
│   └── semaphore.cc
└── rt-thread/
    ├── osal_types.h        ← RT-Thread 类型 + 常量
    ├── thread.cc
    ├── mutex.cc
    └── semaphore.cc
```

## 类型定义示例

```cpp
// osal_types.h (freertos) — OS 特定常量在此定义
namespace osal {
inline constexpr uint32_t kSemaphoreMaxCount = 0xFFFFU;
inline constexpr uint8_t  kPriorityMax =
    static_cast<uint8_t>((configMAX_PRIORITIES > 0) ? (configMAX_PRIORITIES - 1) : 0);
inline constexpr size_t   kDefaultThreadStackBytes = 1024U;
}  // namespace osal

// osal.h — 直接使用，无条件编译
namespace osal {
inline constexpr Priority kDefaultThreadPriority =
    static_cast<Priority>(kPriorityMax / 3U);  // 来自 osal_types.h
}
```

## API 接口

### Thread

```cpp
namespace osal {
class Thread {
public:
    using EntryFunc = void (*)(void *arg);

    int create(EntryFunc entry, void *arg, const ThreadConfig &cfg);
    int join();
    void yield();

    static void sleep(uint32_t ms);
};
} // namespace osal
```

### Mutex

```cpp
namespace osal {
class Mutex {
public:
    int create();
    int lock(uint32_t timeout_ms = kWaitForever);
    int unlock();
    void destroy();
};
} // namespace osal
```

### Semaphore

```cpp
namespace osal {
class Semaphore {
public:
    int create(uint32_t initial_count = 0, uint32_t max_count = kSemaphoreMaxCount);
    int acquire(uint32_t timeout_ms = kWaitForever);
    int release();
    void destroy();
};
} // namespace osal
```

## 配置选择

通过 Kconfig 选择 RTOS 实现：

```kconfig
# 选择 RTOS
CONFIG_OS_FREERTOS=y
# CONFIG_OS_RTTHREAD is not set
```

## 优势

- 业务代码不依赖具体 RTOS
- 切换 RTOS 只需重新配置，无需修改应用代码
- 编译期类型安全，无运行时开销
