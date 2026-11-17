#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace osal::detail {

// Kernel-independent event semantics. The owner serializes every operation.
// Ready waiters retain their snapshot until finish(), so clear-on-exit wakes
// all matching waiters from the same publication rather than consuming it once.
template <size_t Capacity>
class EventBits {
public:
    static constexpr uint32_t kMask = 0x00ffffffU;
    static constexpr int kImmediate = -1;
    static constexpr int kFull = -2;

    struct Waiter {
        void* notification {nullptr};
        uint32_t mask {0U};
        uint32_t result {0U};
        bool used {false};
        bool all {false};
        bool clear {false};
        bool ready {false};
        bool notified {false};
    };

    static constexpr bool valid(uint32_t bits) { return (bits & ~kMask) == 0U; }
    uint32_t get() const { return bits_; }
    uint32_t clear(uint32_t mask) {
        const uint32_t previous = bits_;
        bits_ &= ~mask;
        return previous;
    }

    uint32_t set(uint32_t mask) {
        bits_ |= mask;
        const uint32_t snapshot = bits_;
        uint32_t consumed = 0U;
        for (Waiter& waiter : waiters_) {
            if (waiter.used && !waiter.ready && matches(snapshot, waiter.mask, waiter.all)) {
                waiter.ready = true;
                waiter.result = snapshot;
                if (waiter.clear) consumed |= waiter.mask;
            }
        }
        bits_ &= ~consumed;
        return bits_;
    }

    int begin(uint32_t mask, bool all, bool clear_on_exit, void* notification,
              uint32_t& result, uint32_t set_mask = 0U, bool barrier = false) {
        const uint32_t snapshot = bits_ | set_mask;
        if (barrier) set(set_mask);
        result = barrier ? snapshot : bits_;
        if (matches(result, mask, all)) {
            if (clear_on_exit) bits_ &= ~mask;
            return kImmediate;
        }
        for (size_t index = 0U; index < Capacity; ++index) {
            if (!waiters_[index].used) {
                waiters_[index] = {notification, mask, 0U, true, all,
                                   clear_on_exit, false, false};
                return static_cast<int>(index);
            }
        }
        result = 0U;
        return kFull;
    }

    uint32_t finish(int ticket) {
        Waiter& waiter = waiters_[static_cast<size_t>(ticket)];
        const uint32_t result = waiter.ready ? waiter.result : bits_;
        if (!waiter.ready && waiter.clear && matches(result, waiter.mask, waiter.all)) {
            bits_ &= ~waiter.mask;
        }
        waiter = {};
        return result;
    }

    template <typename Notify>
    void notify(Notify&& function) {
        for (Waiter& waiter : waiters_) {
            if (waiter.used && waiter.ready && !waiter.notified) {
                waiter.notified = true;
                function(waiter.notification);
            }
        }
    }

    bool idle() const {
        for (const Waiter& waiter : waiters_) if (waiter.used) return false;
        return true;
    }

private:
    static bool matches(uint32_t value, uint32_t mask, bool all) {
        return all ? (value & mask) == mask : (value & mask) != 0U;
    }
    uint32_t bits_ {0U};
    std::array<Waiter, Capacity> waiters_ {};
};

} // namespace osal::detail
