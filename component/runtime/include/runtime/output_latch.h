#pragma once
#include <array>
#include <cstddef>
#include <cstdint>

namespace runtime {
// Caller serializes registry access. Stops are idempotent, bounded and safe
// with scheduling and interrupts disabled. Rearming requires a system reset.
template <size_t Capacity>
class OutputLatch {
public:
    using Stop = void (*)(void*);
    int add(void* context, Stop stop) {
        if (latched_ || context == nullptr || stop == nullptr) return -1;
        for (const auto& item : items_) if (item.context == context) return -1;
        for (size_t i = 0U; i < Capacity; ++i) {
            if (items_[i].stop == nullptr) { items_[i] = {context, stop}; return static_cast<int>(i); }
        }
        return -1;
    }
    void remove(int id) { if (id >= 0 && static_cast<size_t>(id) < Capacity) items_[id] = {}; }
    void trip(uint32_t reason) {
        if (!latched_) reason_ = reason;
        latched_ = true;
        for (const auto& item : items_) if (item.stop != nullptr) item.stop(item.context);
    }
    bool latched() const { return latched_; }
    uint32_t reason() const { return reason_; }
private:
    struct Item { void* context {nullptr}; Stop stop {nullptr}; };
    std::array<Item, Capacity> items_ {};
    bool latched_ {false};
    uint32_t reason_ {0U};
};
}
