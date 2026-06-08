#pragma once

#include <cstddef>
#include <cstdint>

namespace hal {
class UartBase;
}

enum class LogLevel : uint8_t {
    Debug = 0,
    Info  = 1,
    Warn  = 2,
    Error = 3,
    None  = 4,
};

enum class LogBackendId : uint8_t {
    Auto,
    Uart,
    RtKprintf,
    Rtt,
    Custom,
};

struct LogConfig {
    LogLevel level {LogLevel::Info};
    LogBackendId backend {LogBackendId::Auto};
    void *custom_backend {nullptr};
};

class LogBackend {
public:
    virtual ~LogBackend() = default;
    virtual void write(const char *str, size_t len) = 0;
};

int log_init_uart(const LogConfig &config, hal::UartBase &uart);
int log_init_generic(const LogConfig &config);

inline int log_init(const LogConfig &config)
{
    return log_init_generic(config);
}

void log_set_level(LogLevel level);
void log_add_backend(LogBackend *backend);
void log_write(LogLevel level, const char *tag, const char *fmt, ...);

inline int log_uart(hal::UartBase &uart, LogLevel level)
{
    LogConfig config {};
    config.level = level;
    config.backend = LogBackendId::Uart;
    return log_init_uart(config, uart);
}

#define LOGD(tag, fmt, ...) log_write(LogLevel::Debug, tag, fmt, ##__VA_ARGS__)
#define LOGI(tag, fmt, ...) log_write(LogLevel::Info,  tag, fmt, ##__VA_ARGS__)
#define LOGW(tag, fmt, ...) log_write(LogLevel::Warn,  tag, fmt, ##__VA_ARGS__)
#define LOGE(tag, fmt, ...) log_write(LogLevel::Error, tag, fmt, ##__VA_ARGS__)
