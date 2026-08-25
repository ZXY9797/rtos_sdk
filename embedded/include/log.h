#pragma once

#include <cstddef>
#include <cstdint>

namespace hal {
class UartBase;
}

enum class LogLevel : uint8_t {
    Debug = 0U,
    Info = 1U,
    Warn = 2U,
    Error = 3U,
    None = 4U,
};

enum class LogBackendId : uint8_t {
    Auto,
    Uart,
    RtKprintf,
    Rtt,
    Custom,
};

using LogWriteCallback = bool (*)(void *context, const char *text, size_t len);

struct LogBackend {
    void *context {nullptr};
    LogWriteCallback write {nullptr};
};

struct LogConfig {
    LogLevel level {LogLevel::Info};
    LogBackendId backend {LogBackendId::Auto};
    const LogBackend *custom_backend {nullptr};
};

struct LogStats {
    uint32_t enqueued {0U};
    uint32_t dropped {0U};
    uint32_t backend_errors {0U};
};

[[nodiscard]] int log_init_uart(const LogConfig &config,
                                hal::UartBase &uart);
[[nodiscard]] int log_init_generic(const LogConfig &config);

inline int log_init(const LogConfig &config) {
    return log_init_generic(config);
}

void log_set_level(LogLevel level);
[[nodiscard]] bool log_add_backend(const LogBackend &backend);
void log_write(LogLevel level, const char *tag, const char *fmt, ...);
[[nodiscard]] bool log_flush(uint32_t timeout_ms);
[[nodiscard]] LogStats log_get_stats();

inline int log_uart(hal::UartBase &uart, LogLevel level) {
    LogConfig config {};
    config.level = level;
    config.backend = LogBackendId::Uart;
    return log_init_uart(config, uart);
}

#define LOGD(tag, fmt, ...) log_write(LogLevel::Debug, tag, fmt, ##__VA_ARGS__)
#define LOGI(tag, fmt, ...) log_write(LogLevel::Info, tag, fmt, ##__VA_ARGS__)
#define LOGW(tag, fmt, ...) log_write(LogLevel::Warn, tag, fmt, ##__VA_ARGS__)
#define LOGE(tag, fmt, ...) log_write(LogLevel::Error, tag, fmt, ##__VA_ARGS__)
