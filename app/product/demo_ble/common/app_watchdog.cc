#include <system/watchdog.h>

#include <atomic>

extern "C" {
#include "app_aon_wdt.h"
#include "app_drv_error.h"
}

namespace {

enum class WatchdogState : uint8_t {
    Stopped,
    Running,
    FeedFault,
};

app_aon_wdt_params_t s_watchdog_params {};
std::atomic<WatchdogState> s_watchdog_state {WatchdogState::Stopped};
static_assert(std::atomic<WatchdogState>::is_always_lock_free,
              "watchdog ISR state must be lock-free");

void watchdog_alarm()
{
    // Once the alarm window is reached, fail closed and let the independent
    // AON counter reset the SoC. Resuming feeds here would hide a health stall.
    s_watchdog_state.store(WatchdogState::FeedFault,
                           std::memory_order_release);
}

} // namespace

extern "C" bool app_watchdog_start(uint32_t timeout_ms)
{
    if (timeout_ms == 0U
        || s_watchdog_state.load(std::memory_order_acquire)
            != WatchdogState::Stopped) {
        return false;
    }

    // The alarm counter is the remaining time before reset and has a smaller
    // hardware range than the main counter. Keep a bounded diagnostic window.
    uint32_t alarm_ms = timeout_ms / 4U;
    if (alarm_ms == 0U) {
        alarm_ms = 1U;
    } else if (alarm_ms > 2000U) {
        alarm_ms = 2000U;
    }
    s_watchdog_params = {};
    s_watchdog_params.init.counter = timeout_ms;
    s_watchdog_params.init.alarm_counter = alarm_ms;
    if (app_aon_wdt_init(&s_watchdog_params, watchdog_alarm)
        != APP_DRV_SUCCESS) {
        return false;
    }
    s_watchdog_state.store(WatchdogState::Running,
                           std::memory_order_release);
    return true;
}

extern "C" void app_watchdog_feed(void)
{
    if (s_watchdog_state.load(std::memory_order_acquire)
            != WatchdogState::Running) {
        return;
    }
    if (app_aon_wdt_refresh() != APP_DRV_SUCCESS) {
        // Do not retry after a driver failure: the hardware counter remains
        // armed and provides a deterministic reset path.
        s_watchdog_state.store(WatchdogState::FeedFault,
                               std::memory_order_release);
    }
}

extern "C" bool app_watchdog_stop(void)
{
    const WatchdogState state =
        s_watchdog_state.load(std::memory_order_acquire);
    if (state == WatchdogState::Stopped) {
        return true;
    }
    if (state == WatchdogState::FeedFault) {
        return false;
    }
    if (app_aon_wdt_deinit() != APP_DRV_SUCCESS) {
        return false;
    }
    s_watchdog_state.store(WatchdogState::Stopped,
                           std::memory_order_release);
    return true;
}
