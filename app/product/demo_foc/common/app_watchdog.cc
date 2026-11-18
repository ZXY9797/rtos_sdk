#include <system/watchdog.h>

#include <atomic>
#include <cstdint>
#include <iterator>

extern "C" {
#include <gd32f50x.h>
}

namespace {

constexpr uint32_t kControlOffset = 0x00U;
constexpr uint32_t kPrescalerOffset = 0x04U;
constexpr uint32_t kReloadOffset = 0x08U;
constexpr uint32_t kStatusOffset = 0x0CU;
constexpr uint32_t kWriteAccessEnable = 0x5555U;
constexpr uint32_t kReloadCounter = 0xAAAAU;
constexpr uint32_t kEnableCounter = 0xCCCCU;
constexpr uint32_t kUpdateBusyMask = 0x03U;
constexpr uint32_t kMaximumReload = 0x0FFFU;
constexpr uint32_t kNominalClockHz = 40000U;
constexpr uint32_t kRegisterTimeout = 1000000U;

std::atomic<bool> watchdog_running {false};
static_assert(std::atomic<bool>::is_always_lock_free);

[[nodiscard]] volatile uint32_t &watchdog_register(uint32_t offset)
{
    return *reinterpret_cast<volatile uint32_t *>(FWDGT_BASE + offset);
}

struct WatchdogTiming {
    uint32_t prescaler_code {0U};
    uint32_t reload {0U};
    bool valid {false};
};

[[nodiscard]] WatchdogTiming calculate_timing(uint32_t timeout_ms)
{
    constexpr uint32_t dividers[] {
        4U, 8U, 16U, 32U, 64U, 128U, 256U, 512U,
        1024U, 2048U, 4096U, 8192U, 16384U, 32768U,
    };
    for (uint32_t code = 0U; code < std::size(dividers); ++code) {
        const uint64_t numerator =
            static_cast<uint64_t>(timeout_ms) * kNominalClockHz;
        const uint64_t denominator =
            static_cast<uint64_t>(dividers[code]) * 1000U;
        const uint64_t ticks = (numerator + denominator - 1U)
            / denominator;
        if (ticks >= 1U && ticks <= kMaximumReload + 1U) {
            return {code, static_cast<uint32_t>(ticks - 1U), true};
        }
    }
    return {};
}

[[nodiscard]] bool wait_for_register_update()
{
    uint32_t remaining = kRegisterTimeout;
    while ((watchdog_register(kStatusOffset) & kUpdateBusyMask) != 0U
           && remaining > 0U) {
        --remaining;
    }
    return (watchdog_register(kStatusOffset) & kUpdateBusyMask) == 0U;
}

} // namespace

extern "C" bool app_watchdog_start(uint32_t timeout_ms)
{
    if (watchdog_running.load(std::memory_order_acquire)) {
        return false;
    }
    const WatchdogTiming timing = calculate_timing(timeout_ms);
    if (!timing.valid) {
        return false;
    }
    watchdog_register(kControlOffset) = kWriteAccessEnable;
    watchdog_register(kPrescalerOffset) = timing.prescaler_code;
    if (!wait_for_register_update()) {
        return false;
    }
    watchdog_register(kReloadOffset) = timing.reload;
    if (!wait_for_register_update()) {
        return false;
    }
    watchdog_register(kControlOffset) = kReloadCounter;
    watchdog_running.store(true, std::memory_order_release);
    watchdog_register(kControlOffset) = kEnableCounter;
    return true;
}

extern "C" void app_watchdog_feed(void)
{
    if (watchdog_running.load(std::memory_order_acquire)) {
        watchdog_register(kControlOffset) = kReloadCounter;
    }
}

extern "C" bool app_watchdog_stop(void)
{
    // FWDGT is intentionally irreversible until the next hardware reset.
    return !watchdog_running.load(std::memory_order_acquire);
}
