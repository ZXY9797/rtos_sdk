#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace runtime::experimental {

enum class Completion : uint8_t { Success, Failed, Cancelled, TimedOut };
struct RequestToken {
    uint32_t generation {0U};
    uint16_t slot {UINT16_MAX};
};

// Serialized-owner request registry. The caller stops the peripheral before
// publishing Cancelled/TimedOut and releasing its buffer. A stale completion
// cannot release the buffer of a newer request in the same slot.
template <typename Value, size_t Capacity>
class AsyncPool {
    static_assert(Capacity > 0U && Capacity < UINT16_MAX);
public:
    using Callback = void (*)(void*, RequestToken, Completion, const Value&);

    RequestToken acquire(const Value& value, uint32_t now, uint32_t timeout,
                          Callback callback, void* context) {
        if (cancelling_ || timeout == 0U || timeout > INT32_MAX || callback == nullptr) return {};
        for (size_t i = 0U; i < Capacity; ++i) {
            Entry& entry = entries_[i];
            if (entry.active) continue;
            // Exhaustion fails closed instead of reusing a generation after wrap.
            if (generation_ == UINT32_MAX) return {};
            entry = {value, now, timeout, ++generation_, callback, context, true};
            return {entry.generation, static_cast<uint16_t>(i)};
        }
        return {};
    }

    bool active(RequestToken token) const {
        return token.slot < Capacity && entries_[token.slot].active
            && entries_[token.slot].generation == token.generation;
    }

    bool expired(RequestToken token, uint32_t now) const {
        return active(token) && now - entries_[token.slot].started >= entries_[token.slot].timeout;
    }

    bool complete(RequestToken token, Completion result) {
        if (!active(token)) return false;
        Entry copy = entries_[token.slot];
        entries_[token.slot].active = false;
        copy.callback(copy.context, token, result, copy.value);
        return true;
    }

    // Hardware owner must quiesce all operations before resetting a session.
    void cancel_all() {
        if (cancelling_) return;
        cancelling_ = true;
        for (size_t i = 0U; i < Capacity; ++i) {
            if (entries_[i].active) complete({entries_[i].generation,
                static_cast<uint16_t>(i)}, Completion::Cancelled);
        }
        cancelling_ = false;
    }

private:
    struct Entry {
        Value value {};
        uint32_t started {0U}, timeout {0U}, generation {0U};
        Callback callback {nullptr};
        void* context {nullptr};
        bool active {false};
    };
    std::array<Entry, Capacity> entries_ {};
    uint32_t generation_ {0U};
    bool cancelling_ {false};
};

} // namespace runtime::experimental
