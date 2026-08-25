#include <log.h>

#ifdef CONFIG_UART
#include <drivers/uart.h>
#endif

#include <osal.h>

#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <cstring>

#if __has_include(<rtthread.h>)
#include <rtthread.h>
#define LOG_HAS_RTTHREAD 1
#else
#define LOG_HAS_RTTHREAD 0
#endif

namespace {

constexpr size_t kMaxBackends = 4U;
constexpr size_t kLogBufferSize = 256U;

struct LogMessage {
    uint16_t length;
    char text[kLogBufferSize];
};

LogBackend s_backends[kMaxBackends] {};
size_t s_backend_count = 0U;
std::atomic<uint8_t> s_level {static_cast<uint8_t>(LogLevel::Info)};
std::atomic<uint32_t> s_enqueued {0U};
std::atomic<uint32_t> s_dropped {0U};
std::atomic<uint32_t> s_backend_errors {0U};
std::atomic<bool> s_backend_active {false};
std::atomic_flag s_dispatch_lock = ATOMIC_FLAG_INIT;

#if defined(CONFIG_LOG_ASYNC)
osal::MessageQueue s_log_queue;
osal::Thread s_log_thread;
alignas(std::max_align_t)
uint8_t s_queue_storage[CONFIG_LOG_QUEUE_DEPTH * sizeof(LogMessage)] {};
alignas(std::max_align_t)
uint8_t s_thread_stack[CONFIG_LOG_THREAD_STACK_SIZE] {};
std::atomic<bool> s_async_started {false};
#endif

bool backend_equal(const LogBackend &lhs, const LogBackend &rhs) {
    return lhs.context == rhs.context && lhs.write == rhs.write;
}

size_t copy_backends(LogBackend (&snapshot)[kMaxBackends]) {
    osal::CriticalSectionGuard guard;
    std::memcpy(snapshot, s_backends,
                s_backend_count * sizeof(LogBackend));
    return s_backend_count;
}

bool dispatch_message(const LogMessage &message) {
    if (s_dispatch_lock.test_and_set(std::memory_order_acquire)) {
        s_dropped.fetch_add(1U, std::memory_order_relaxed);
        return false;
    }
    LogBackend snapshot[kMaxBackends] {};
    const size_t count = copy_backends(snapshot);
    s_backend_active.store(true, std::memory_order_release);
    for (size_t index = 0U; index < count; ++index) {
        if (!snapshot[index].write(snapshot[index].context,
                                   message.text, message.length)) {
            s_backend_errors.fetch_add(1U, std::memory_order_relaxed);
        }
    }
    s_backend_active.store(false, std::memory_order_release);
    s_dispatch_lock.clear(std::memory_order_release);
    return true;
}

#if defined(CONFIG_LOG_ASYNC)
void log_worker(void *) {
    LogMessage message {};
    for (;;) {
        if (s_log_queue.receive(&message, osal::kWaitForever)) {
            (void)dispatch_message(message);
        }
    }
}

bool ensure_async_started() {
    if (s_async_started.load(std::memory_order_acquire)) return true;
    if (!osal::Kernel::is_running()) return false;
    if (!s_log_queue.create(CONFIG_LOG_QUEUE_DEPTH, sizeof(LogMessage),
                            s_queue_storage, sizeof(s_queue_storage))) {
        return false;
    }

    osal::ThreadConfig config {};
    config.name = "log";
    config.priority = osal::kDefaultThreadPriority;
    config.stack_size_bytes = sizeof(s_thread_stack);
    config.stack_buffer = s_thread_stack;
    if (!s_log_thread.start(log_worker, nullptr, config)) {
        s_log_queue.destroy();
        return false;
    }

    s_async_started.store(true, std::memory_order_release);
    if (s_log_thread.startup() != 0) {
        s_async_started.store(false, std::memory_order_release);
        s_log_thread.destroy();
        s_log_queue.destroy();
        return false;
    }
    return true;
}
#else
bool ensure_async_started() {
    return true;
}
#endif

bool add_backend_internal(const LogBackend &backend) {
    if (backend.write == nullptr || !ensure_async_started()) return false;

    osal::CriticalSectionGuard guard;
    for (size_t index = 0U; index < s_backend_count; ++index) {
        if (backend_equal(s_backends[index], backend)) return true;
    }
    if (s_backend_count >= kMaxBackends) return false;
    s_backends[s_backend_count++] = backend;
    return true;
}

#ifdef CONFIG_UART
bool uart_write(void *context, const char *text, size_t len) {
    auto *uart = static_cast<hal::UartBase *>(context);
    if (uart == nullptr || !uart->is_initialized()) return false;

    osal::Deadline deadline(CONFIG_LOG_BACKEND_TIMEOUT_MS);
    size_t written = 0U;
    if (uart->send(reinterpret_cast<const uint8_t *>(text), len,
                   &written, deadline.remaining()) != hal::Status::Ok
        || written != len) {
        return false;
    }

    static constexpr uint8_t kCrLf[] = {'\r', '\n'};
    written = 0U;
    return uart->send(kCrLf, sizeof(kCrLf), &written,
                      deadline.remaining()) == hal::Status::Ok
        && written == sizeof(kCrLf);
}
#endif

#if LOG_HAS_RTTHREAD
bool rtkprintf_write(void *, const char *text, size_t) {
    rt_kprintf("%s\r\n", text);
    return true;
}
#endif

} // namespace

int log_init_uart(const LogConfig &config, hal::UartBase &uart) {
#ifdef CONFIG_UART
    if (!uart.is_initialized()) return -1;
    log_set_level(config.level);
    return add_backend_internal(LogBackend {&uart, uart_write}) ? 0 : -2;
#else
    (void)config;
    (void)uart;
    return -1;
#endif
}

int log_init_generic(const LogConfig &config) {
    log_set_level(config.level);
    switch (config.backend) {
    case LogBackendId::Auto:
    case LogBackendId::RtKprintf:
#if LOG_HAS_RTTHREAD
        return add_backend_internal(LogBackend {nullptr, rtkprintf_write})
            ? 0 : -2;
#else
        return -1;
#endif

    case LogBackendId::Custom:
        return config.custom_backend != nullptr
            && add_backend_internal(*config.custom_backend) ? 0 : -1;

    case LogBackendId::Rtt:
    case LogBackendId::Uart:
    default:
        return -1;
    }
}

void log_set_level(LogLevel level) {
    s_level.store(static_cast<uint8_t>(level), std::memory_order_release);
}

bool log_add_backend(const LogBackend &backend) {
    return add_backend_internal(backend);
}

void log_write(LogLevel level, const char *tag, const char *fmt, ...) {
    const uint8_t raw_level = static_cast<uint8_t>(level);
    if (raw_level < s_level.load(std::memory_order_acquire)
        || raw_level >= static_cast<uint8_t>(LogLevel::None)
        || tag == nullptr || fmt == nullptr) {
        return;
    }

#if defined(CONFIG_LOG_ASYNC)
    if (!s_async_started.load(std::memory_order_acquire)
        || osal::Kernel::in_isr()) {
        s_dropped.fetch_add(1U, std::memory_order_relaxed);
        return;
    }
#endif

    static constexpr const char *kLevelText[] = {"D", "I", "W", "E"};
    LogMessage message {};
    const int prefix = std::snprintf(message.text, sizeof(message.text),
                                     "[%s/%s] ", kLevelText[raw_level], tag);
    if (prefix < 0 || static_cast<size_t>(prefix) >= sizeof(message.text)) {
        s_dropped.fetch_add(1U, std::memory_order_relaxed);
        return;
    }

    va_list args;
    va_start(args, fmt);
    const int body = std::vsnprintf(message.text + prefix,
                                    sizeof(message.text)
                                        - static_cast<size_t>(prefix),
                                    fmt, args);
    va_end(args);
    if (body < 0) {
        s_dropped.fetch_add(1U, std::memory_order_relaxed);
        return;
    }

    const size_t requested = static_cast<size_t>(prefix)
        + static_cast<size_t>(body);
    const size_t total = requested < sizeof(message.text)
        ? requested : sizeof(message.text) - 1U;
    message.length = static_cast<uint16_t>(total);

#if defined(CONFIG_LOG_ASYNC)
    if (!s_log_queue.send(&message, 0U)) {
        s_dropped.fetch_add(1U, std::memory_order_relaxed);
        return;
    }
#else
    if (!dispatch_message(message)) return;
#endif
    s_enqueued.fetch_add(1U, std::memory_order_relaxed);
}

bool log_flush(uint32_t timeout_ms) {
#if defined(CONFIG_LOG_ASYNC)
    if (!s_async_started.load(std::memory_order_acquire)
        || osal::Kernel::in_isr()) {
        return false;
    }
    osal::Deadline deadline(timeout_ms);
    while (s_log_queue.count() != 0U
           || s_backend_active.load(std::memory_order_acquire)) {
        if (deadline.expired()) return false;
        osal::this_thread::sleep_for(1U);
    }
#else
    (void)timeout_ms;
#endif
    return true;
}

LogStats log_get_stats() {
    return LogStats {
        s_enqueued.load(std::memory_order_relaxed),
        s_dropped.load(std::memory_order_relaxed),
        s_backend_errors.load(std::memory_order_relaxed),
    };
}
