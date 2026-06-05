#pragma once

#include <devicetree.h>
#include <cstddef>
#include <cstdint>

// ─── 日志级别 ──────────────────────────────────────────────────────

enum class LogLevel : uint8_t {
    Debug = 0,
    Info  = 1,
    Warn  = 2,
    Error = 3,
    None  = 4,
};

// ─── 后端类型 ──────────────────────────────────────────────────────

enum class LogBackendId : uint8_t {
    Auto,
    Uart,
    RtKprintf,
    Rtt,
    Custom,
};

// ─── 配置 ──────────────────────────────────────────────────────────

struct LogConfig {
    LogLevel      level {LogLevel::Info};
    LogBackendId  backend {LogBackendId::Auto};
    void         *custom_backend {nullptr};
};

// ─── 后端接口 ──────────────────────────────────────────────────────

class LogBackend {
public:
    virtual ~LogBackend() = default;
    virtual void write(const char *str, size_t len) = 0;
};

// ─── 内部接口 ──────────────────────────────────────────────────────

int log_init_uart(const LogConfig &config, uintptr_t base, int irq, uint32_t baudrate);
int log_init_generic(const LogConfig &config);

// ─── 核心 API ──────────────────────────────────────────────────────

// 非 UART 后端：RtKprintf / Rtt / Custom
inline int log_init(const LogConfig &config) {
    return log_init_generic(config);
}

void log_set_level(LogLevel level);
void log_add_backend(LogBackend *backend);
void log_write(LogLevel level, const char *tag, const char *fmt, ...);

// ─── 便捷宏 ────────────────────────────────────────────────────────

// 从 DTS alias 初始化 UART 日志（DT 宏使用 token pasting，不能用在模板中）
#define LOG_INIT_UART(alias, lvl) \
    log_init_uart(LogConfig{lvl}, \
        DT_REG_ADDR(DT_ALIAS(alias)), \
        DT_IRQ(DT_ALIAS(alias), irq), \
        DT_PROP(DT_ALIAS(alias), current_speed))

#define LOGD(tag, fmt, ...) log_write(LogLevel::Debug, tag, fmt, ##__VA_ARGS__)
#define LOGI(tag, fmt, ...) log_write(LogLevel::Info,  tag, fmt, ##__VA_ARGS__)
#define LOGW(tag, fmt, ...) log_write(LogLevel::Warn,  tag, fmt, ##__VA_ARGS__)
#define LOGE(tag, fmt, ...) log_write(LogLevel::Error, tag, fmt, ##__VA_ARGS__)
