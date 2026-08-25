#include <device.h>
#include <device_pm.h>
#include <osal.h>

#include <atomic>

namespace hal {
namespace {

std::atomic_flag transition_lock = ATOMIC_FLAG_INIT;
std::atomic<DevicePowerState> power_state {DevicePowerState::Active};
std::atomic<size_t> transitioned_devices {0U};
std::atomic<int> failed_ordinal {-1};
std::atomic<int> last_error {0};

class TransitionGuard {
public:
    TransitionGuard()
        : locked_(!transition_lock.test_and_set(std::memory_order_acquire)) {}
    ~TransitionGuard() {
        if (locked_) {
            transition_lock.clear(std::memory_order_release);
        }
    }
    [[nodiscard]] explicit operator bool() const { return locked_; }
private:
    bool locked_;
};

size_t managed_device_count()
{
    size_t count = 0U;
    const DeviceInfo* registry = get_device_registry(&count);
    if (registry == nullptr) {
        return 0U;
    }
    size_t managed = 0U;
    for (size_t index = 0U; index < count; ++index) {
        if (registry[index].supports_power_management) {
            ++managed;
        }
    }
    return managed;
}

size_t rollback_suspended_devices(const DeviceInfo* registry,
                                  size_t begin,
                                  size_t count,
                                  int& rollback_ordinal,
                                  int& rollback_error)
{
    size_t remaining = 0U;
    for (size_t index = begin; index < count; ++index) {
        const DeviceInfo& device = registry[index];
        if (!device.supports_power_management) {
            continue;
        }
        const int error = device.resume(device.instance);
        if (error != 0) {
            ++remaining;
            if (rollback_error == 0) {
                rollback_ordinal = device.ord;
                rollback_error = error;
            }
        }
    }
    return remaining;
}

} // namespace

bool suspend_devices()
{
    TransitionGuard guard;
    if (!guard || osal::Kernel::in_isr()
        || power_state.load(std::memory_order_acquire)
            != DevicePowerState::Active) {
        return false;
    }

    size_t count = 0U;
    const DeviceInfo* registry = get_device_registry(&count);
    if (count != 0U && registry == nullptr) {
        failed_ordinal.store(-1, std::memory_order_release);
        last_error.store(-1, std::memory_order_release);
        transitioned_devices.store(0U, std::memory_order_release);
        power_state.store(DevicePowerState::Error,
                          std::memory_order_release);
        return false;
    }
    size_t completed = 0U;
    power_state.store(DevicePowerState::Suspending,
                      std::memory_order_release);
    failed_ordinal.store(-1, std::memory_order_relaxed);
    last_error.store(0, std::memory_order_relaxed);
    transitioned_devices.store(0U, std::memory_order_relaxed);

    for (size_t index = count; index > 0U; --index) {
        const DeviceInfo& device = registry[index - 1U];
        if (!device.supports_power_management) {
            continue;
        }
        if (device.instance == nullptr || device.suspend == nullptr
            || device.resume == nullptr) {
            int rollback_ordinal = -1;
            int rollback_error = 0;
            const size_t remaining = rollback_suspended_devices(
                registry, index, count, rollback_ordinal, rollback_error);
            failed_ordinal.store(
                rollback_error == 0 ? device.ord : rollback_ordinal,
                std::memory_order_release);
            last_error.store(
                rollback_error == 0 ? -1 : rollback_error,
                std::memory_order_release);
            transitioned_devices.store(remaining,
                                       std::memory_order_release);
            power_state.store(
                remaining == 0U ? DevicePowerState::Active
                                : DevicePowerState::Error,
                std::memory_order_release);
            return false;
        }
        const int error = device.suspend(device.instance);
        if (error != 0) {
            int rollback_ordinal = -1;
            int rollback_error = 0;
            const size_t remaining = rollback_suspended_devices(
                registry, index, count, rollback_ordinal, rollback_error);
            failed_ordinal.store(
                rollback_error == 0 ? device.ord : rollback_ordinal,
                std::memory_order_release);
            last_error.store(
                rollback_error == 0 ? error : rollback_error,
                std::memory_order_release);
            transitioned_devices.store(remaining,
                                       std::memory_order_release);
            power_state.store(
                remaining == 0U ? DevicePowerState::Active
                                : DevicePowerState::Error,
                std::memory_order_release);
            return false;
        }
        ++completed;
    }
    transitioned_devices.store(completed, std::memory_order_release);
    power_state.store(DevicePowerState::Suspended,
                      std::memory_order_release);
    return true;
}

bool resume_devices()
{
    TransitionGuard guard;
    if (!guard || osal::Kernel::in_isr()
        || power_state.load(std::memory_order_acquire)
            != DevicePowerState::Suspended) {
        return false;
    }

    size_t count = 0U;
    const DeviceInfo* registry = get_device_registry(&count);
    if (count != 0U && registry == nullptr) {
        failed_ordinal.store(-1, std::memory_order_release);
        last_error.store(-1, std::memory_order_release);
        transitioned_devices.store(0U, std::memory_order_release);
        power_state.store(DevicePowerState::Error,
                          std::memory_order_release);
        return false;
    }
    size_t completed = 0U;
    power_state.store(DevicePowerState::Resuming,
                      std::memory_order_release);
    for (size_t index = 0U; index < count; ++index) {
        const DeviceInfo& device = registry[index];
        if (!device.supports_power_management) {
            continue;
        }
        if (device.instance == nullptr || device.resume == nullptr) {
            failed_ordinal.store(device.ord, std::memory_order_release);
            last_error.store(-1, std::memory_order_release);
            transitioned_devices.store(completed,
                                       std::memory_order_release);
            power_state.store(DevicePowerState::Error,
                              std::memory_order_release);
            return false;
        }
        const int error = device.resume(device.instance);
        if (error != 0) {
            failed_ordinal.store(device.ord, std::memory_order_release);
            last_error.store(error, std::memory_order_release);
            transitioned_devices.store(completed, std::memory_order_release);
            power_state.store(DevicePowerState::Error,
                              std::memory_order_release);
            return false;
        }
        ++completed;
    }
    transitioned_devices.store(completed, std::memory_order_release);
    failed_ordinal.store(-1, std::memory_order_release);
    last_error.store(0, std::memory_order_release);
    power_state.store(DevicePowerState::Active, std::memory_order_release);
    return true;
}

DevicePowerStatus device_power_status()
{
    return {
        power_state.load(std::memory_order_acquire),
        managed_device_count(),
        transitioned_devices.load(std::memory_order_acquire),
        failed_ordinal.load(std::memory_order_acquire),
        last_error.load(std::memory_order_acquire),
    };
}

} // namespace hal
