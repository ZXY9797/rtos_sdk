#pragma once

#include <cstddef>
#include <cstdint>

namespace system_watchdog {

using ClientId = uint8_t;
inline constexpr ClientId kInvalidClient = UINT8_MAX;
inline constexpr size_t kMaxClients = 8U;

struct ClientStatus {
    bool registered {false};
    bool overdue {false};
    const char* name {nullptr};
    uint32_t max_silence_ms {0U};
    uint32_t silence_ms {0U};
};

struct Snapshot {
    bool enabled {false};
    uint8_t registered_clients {0U};
    uint32_t hardware_feeds {0U};
    ClientStatus clients[kMaxClients] {};
};

// name must remain valid for the lifetime of the firmware (normally a literal).
// A client is healthy until max_silence_ms elapses without heartbeat(). The
// window must not exceed INT32_MAX milliseconds so unsigned uptime wrap
// comparisons remain unambiguous. When CONFIG_APP_WATCHDOG is disabled,
// registration and heartbeat fail closed.
[[nodiscard]] ClientId register_client(const char* name,
                                       uint32_t max_silence_ms);
[[nodiscard]] bool unregister_client(ClientId client);
[[nodiscard]] bool heartbeat(ClientId client);
[[nodiscard]] Snapshot snapshot();

} // namespace system_watchdog

#if defined(CONFIG_APP_WATCHDOG)
extern "C" {
// Product provider contract. All functions are task-context only.
bool app_watchdog_start(uint32_t timeout_ms);
void app_watchdog_feed(void);
bool app_watchdog_stop(void);
}
#endif
