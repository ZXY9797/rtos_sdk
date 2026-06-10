#pragma once

#include <osal/osal.h>
#include <cstdint>

/// 传感器触发链：定时器 ISR → 可选传感器读取 → 可选分频 → 任务唤醒。
/// 内部创建并持有 PeriodicThread，直接调用 notify_from_isr()，不经过 IsrTrigger。
class SensorCore {
public:
    using ReadFn = bool (*)(void *arg);  // 同步传感器读取（ISR 上下文）

    struct Config {
        const char *name {"sc"};
        osal::PeriodicEntry entry {nullptr};
        void *param {nullptr};
        size_t stack_size {2048};
        int32_t priority {5};
        uint32_t frequency_hz {1000};
        osal::IrqTimer *timer {nullptr};        // 硬件定时器触发源
        ReadFn read_fn {nullptr};               // 可选：timer ISR 中调用的传感器读取
        void *sensor_arg {nullptr};
        uint32_t divider {1};                   // 分频系数
    };

    explicit SensorCore(const Config &cfg);
    ~SensorCore();

    SensorCore(const SensorCore&) = delete;
    SensorCore& operator=(const SensorCore&) = delete;

    [[nodiscard]] int start();
    [[nodiscard]] osal::PeriodicThread* thread() const { return thread_; }
    [[nodiscard]] uint32_t fire_count() const { return fire_count_; }

    /// 从外部 ISR 调用（带分频）
    void on_sensor_done();

private:
    static void timer_callback(void *arg);
    Config cfg_;
    osal::PeriodicThread *thread_ {nullptr};
    volatile uint32_t fire_count_ {0};
};
