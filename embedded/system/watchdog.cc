#include <system/watchdog.h>

#include <arch/arm/cortex_m/fault.h>
#include <init.h>
#include <osal.h>

#include <cstddef>
#include <cstring>

namespace system_watchdog {
namespace {

#if defined(CONFIG_APP_WATCHDOG)
struct Client {
    const char* name {nullptr};
    uint32_t max_silence_ms {0U};
    uint32_t last_seen_ms {0U};
};

Client clients[kMaxClients] {};
std::atomic<uint32_t> hardware_feeds {0U};

osal::Thread monitor_thread;
alignas(std::max_align_t)
    uint8_t monitor_stack[CONFIG_APP_WATCHDOG_STACK_SIZE] {};

void monitor_entry(void*)
{
    while (!monitor_thread.stop_requested()) {
        const uint32_t now = osal::Kernel::uptime_ms();
        for (size_t index = 0U; index < kMaxClients; ++index) {
            Client client {};
            {
                osal::CriticalSectionGuard guard;
                client = clients[index];
            }
            if (client.name != nullptr
                && (now - client.last_seen_ms) > client.max_silence_ms) {
                hal::fault::panic(hal::fault::FatalReason::WatchdogExpired,
                                  static_cast<int32_t>(index), client.name, 0U);
            }
        }
        app_watchdog_feed();
        hardware_feeds.fetch_add(1U, std::memory_order_relaxed);
        osal::this_thread::sleep_for(CONFIG_APP_WATCHDOG_CHECK_MS);
    }
}

int watchdog_init()
{
    if (CONFIG_APP_WATCHDOG_CHECK_MS >= CONFIG_APP_WATCHDOG_TIMEOUT_MS) {
        return -1;
    }
    if (!app_watchdog_start(CONFIG_APP_WATCHDOG_TIMEOUT_MS)) {
        return -1;
    }
    hardware_feeds.store(0U, std::memory_order_relaxed);
#if defined(CONFIG_APP_PRODUCTION)
    bool has_client = false;
    {
        osal::CriticalSectionGuard guard;
        for (const Client& client : clients) {
            has_client = has_client || client.name != nullptr;
        }
    }
    if (!has_client) {
        (void)app_watchdog_stop();
        return -1;
    }
#endif
    osal::ThreadConfig config {};
    config.name = "health";
    config.priority = osal::kDefaultThreadPriority;
    config.stack_buffer = monitor_stack;
    config.stack_size_bytes = sizeof(monitor_stack);
    if (!monitor_thread.start(monitor_entry, nullptr, config)
        || monitor_thread.startup() != 0) {
        monitor_thread.destroy();
        (void)app_watchdog_stop();
        return -1;
    }
    return 0;
}

int watchdog_deinit()
{
    monitor_thread.destroy();
    return app_watchdog_stop() ? 0 : -1;
}

SYS_INIT_ROLLBACK(watchdog_init, watchdog_deinit,
                  INITCALL_LEVEL_APPLICATION, 99);
#endif

} // namespace

ClientId register_client(const char* name, uint32_t max_silence_ms)
{
#if !defined(CONFIG_APP_WATCHDOG)
    (void)name;
    (void)max_silence_ms;
    return kInvalidClient;
#else
    if (name == nullptr
        || max_silence_ms < CONFIG_APP_WATCHDOG_CHECK_MS
        || max_silence_ms > static_cast<uint32_t>(INT32_MAX)
    ) {
        return kInvalidClient;
    }
    osal::CriticalSectionGuard guard;
    for (const Client& client : clients) {
        if (client.name != nullptr && std::strcmp(client.name, name) == 0) {
            return kInvalidClient;
        }
    }
    for (size_t index = 0U; index < kMaxClients; ++index) {
        if (clients[index].name == nullptr) {
            clients[index] = {name, max_silence_ms,
                              osal::Kernel::uptime_ms()};
            return static_cast<ClientId>(index);
        }
    }
    return kInvalidClient;
#endif
}

bool unregister_client(ClientId client)
{
#if !defined(CONFIG_APP_WATCHDOG)
    (void)client;
    return false;
#else
    if (client >= kMaxClients) {
        return false;
    }
    osal::CriticalSectionGuard guard;
    if (clients[client].name == nullptr) {
        return false;
    }
    clients[client] = {};
    return true;
#endif
}

bool heartbeat(ClientId client)
{
#if !defined(CONFIG_APP_WATCHDOG)
    (void)client;
    return false;
#else
    if (client >= kMaxClients) {
        return false;
    }
    osal::CriticalSectionGuard guard;
    if (clients[client].name == nullptr) {
        return false;
    }
    clients[client].last_seen_ms = osal::Kernel::uptime_ms();
    return true;
#endif
}

Snapshot snapshot()
{
    Snapshot result {};
#if defined(CONFIG_APP_WATCHDOG)
    result.enabled = true;
    result.hardware_feeds = hardware_feeds.load(std::memory_order_relaxed);
    const uint32_t now = osal::Kernel::uptime_ms();
    osal::CriticalSectionGuard guard;
    for (size_t index = 0U; index < kMaxClients; ++index) {
        const Client& client = clients[index];
        ClientStatus& status = result.clients[index];
        if (client.name == nullptr) {
            continue;
        }
        status.registered = true;
        status.name = client.name;
        status.max_silence_ms = client.max_silence_ms;
        status.silence_ms = now - client.last_seen_ms;
        status.overdue = status.silence_ms > status.max_silence_ms;
        ++result.registered_clients;
    }
#endif
    return result;
}

} // namespace system_watchdog
