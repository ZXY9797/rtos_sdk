/**
 * @brief 通用日志模块 — 可插拔后端
 */

#include <log.h>
#include <drivers/uart.h>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <new>

// ─── 内部常量 ──────────────────────────────────────────────────────

static constexpr size_t MAX_BACKENDS = 4;
static constexpr size_t LOG_BUF_SIZE = 256;

// ─── 内部状态 ──────────────────────────────────────────────────────

static LogBackend *s_backends[MAX_BACKENDS];
static size_t s_backend_count = 0;
static LogLevel s_level = LogLevel::Info;

// UART 实例（placement new 构造）
alignas(hal::UartBase) static uint8_t s_uart_storage[sizeof(hal::UartBase)];
static hal::UartBase *s_uart = nullptr;
static uint8_t s_rx_buf[128];

// ─── 内部后端：UART ────────────────────────────────────────────────

class UartLogBackend : public LogBackend {
public:
    void write(const char *str, size_t len) override {
        if (s_uart && s_uart->is_initialized())
            (void)s_uart->send(reinterpret_cast<const uint8_t *>(str), len, nullptr, 0);
    }
};

static UartLogBackend s_uart_backend;

// ─── 内部后端：RT-Thread rt_kprintf ────────────────────────────────

#if __has_include(<rtthread.h>)
#include <rtthread.h>

class RtKprintfBackend : public LogBackend {
public:
    void write(const char *str, size_t len) override {
        (void)len;
        rt_kprintf("%s", str);
    }
};

static RtKprintfBackend s_rtkprintf_backend;
#endif

// ─── 辅助函数 ──────────────────────────────────────────────────────

static void log_add_backend_internal(LogBackend *backend) {
    if (backend && s_backend_count < MAX_BACKENDS)
        s_backends[s_backend_count++] = backend;
}

// ─── 核心 API ──────────────────────────────────────────────────────

int log_init_uart(const LogConfig &config, uintptr_t base, int irq, uint32_t baudrate) {
    s_level = config.level;
    if (!base) return -1;

    s_uart = new (s_uart_storage) hal::UartBase(base, irq);
    hal::UartConfig ucfg {};
    ucfg.baudrate       = baudrate;
    ucfg.rx_buffer      = s_rx_buf;
    ucfg.rx_buffer_size = sizeof(s_rx_buf);
    if (s_uart->init(ucfg) != hal::Status::Ok) return -1;

    log_add_backend_internal(&s_uart_backend);
    return 0;
}

int log_init_generic(const LogConfig &config) {
    s_level = config.level;

    switch (config.backend) {
    case LogBackendId::RtKprintf:
#if __has_include(<rtthread.h>)
        log_add_backend_internal(&s_rtkprintf_backend);
#else
        return -1;
#endif
        break;

    case LogBackendId::Custom:
        if (config.custom_backend)
            log_add_backend_internal(static_cast<LogBackend *>(config.custom_backend));
        break;

    case LogBackendId::Rtt:
        // TODO: Segger RTT
        return -1;

    default:
        return -1;
    }

    return 0;
}

void log_set_level(LogLevel level) {
    s_level = level;
}

void log_add_backend(LogBackend *backend) {
    log_add_backend_internal(backend);
}

void log_write(LogLevel level, const char *tag, const char *fmt, ...) {
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

    for (size_t i = 0; i < s_backend_count; i++)
        s_backends[i]->write(buf, total);
}
