#include <log.h>
#include <drivers/uart.h>

#include <cstdarg>
#include <cstdio>

#if __has_include(<rtthread.h>)
#include <rtthread.h>
#define LOG_HAS_RTTHREAD 1
#else
#define LOG_HAS_RTTHREAD 0
#endif

namespace {

constexpr size_t MAX_BACKENDS = 4;
constexpr size_t LOG_BUF_SIZE = 256;

LogBackend *s_backends[MAX_BACKENDS];
size_t s_backend_count = 0;
LogLevel s_level = LogLevel::Info;

hal::UartBase *s_uart = nullptr;

class UartLogBackend : public LogBackend {
public:
    void write(const char *str, size_t len) override
    {
        if (!s_uart || !s_uart->is_initialized()) return;

        (void)s_uart->send(reinterpret_cast<const uint8_t *>(str), len, nullptr, 1000);

        static const char crlf[] = "\r\n";
        (void)s_uart->send(reinterpret_cast<const uint8_t *>(crlf),
                           sizeof(crlf) - 1, nullptr, 1000);
    }
};

UartLogBackend s_uart_backend;

#if LOG_HAS_RTTHREAD
class RtKprintfBackend : public LogBackend {
public:
    void write(const char *str, size_t len) override
    {
        (void)len;
        rt_kprintf("%s", str);
    }
};

RtKprintfBackend s_rtkprintf_backend;
#endif

void log_add_backend_internal(LogBackend *backend)
{
    if (!backend) return;

    for (size_t i = 0; i < s_backend_count; i++) {
        if (s_backends[i] == backend) return;
    }

    if (s_backend_count < MAX_BACKENDS) {
        s_backends[s_backend_count++] = backend;
    }
}

} // namespace

int log_init_uart(const LogConfig &config, hal::UartBase &uart)
{
    s_level = config.level;
    if (!uart.is_initialized()) return -1;

    s_uart = &uart;
    log_add_backend_internal(&s_uart_backend);
    return 0;
}

int log_init_generic(const LogConfig &config)
{
    s_level = config.level;

    switch (config.backend) {
    case LogBackendId::RtKprintf:
#if LOG_HAS_RTTHREAD
        log_add_backend_internal(&s_rtkprintf_backend);
#else
        return -1;
#endif
        break;

    case LogBackendId::Custom:
        if (config.custom_backend) {
            log_add_backend_internal(static_cast<LogBackend *>(config.custom_backend));
        }
        break;

    case LogBackendId::Rtt:
        return -1;

    default:
        return -1;
    }

    return 0;
}

void log_set_level(LogLevel level)
{
    s_level = level;
}

void log_add_backend(LogBackend *backend)
{
    log_add_backend_internal(backend);
}

void log_write(LogLevel level, const char *tag, const char *fmt, ...)
{
    if (level < s_level || s_backend_count == 0) return;

    static const char *level_str[] = {"D", "I", "W", "E"};
    char buf[LOG_BUF_SIZE];

    int prefix = snprintf(buf, sizeof(buf), "[%s/%s] ",
                          level_str[static_cast<int>(level)], tag);
    if (prefix < 0) return;

    va_list args;
    va_start(args, fmt);
    int body = vsnprintf(buf + prefix, sizeof(buf) - prefix, fmt, args);
    va_end(args);

    if (body < 0) return;
    size_t total = static_cast<size_t>(prefix + body);
    if (total >= sizeof(buf)) total = sizeof(buf) - 1;

    for (size_t i = 0; i < s_backend_count; i++) {
        s_backends[i]->write(buf, total);
    }
}
