#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace runtime {

enum class Trigger : uint8_t { Periodic, External, Background };
// Preserve Fault's existing value; policy 1 had no implementation.
enum class Overflow : uint8_t { RejectNewest = 0U, Fault = 2U };

struct ExecutionSpec {
    const char* name {nullptr};
    Trigger trigger {Trigger::Periodic};
    uint32_t period_us {0U};
    uint32_t deadline_us {0U};
    uint32_t budget_us {0U};
    Overflow overflow {Overflow::RejectNewest};

    constexpr bool valid_options() const {
        return (trigger == Trigger::Periodic || trigger == Trigger::External
                || trigger == Trigger::Background)
            && (overflow == Overflow::RejectNewest || overflow == Overflow::Fault);
    }

    constexpr bool valid() const {
        return valid_options() && name != nullptr
            && deadline_us > 0U && deadline_us <= INT32_MAX
            && budget_us > 0U && budget_us <= deadline_us
            && (trigger == Trigger::Background || period_us > 0U)
            && period_us <= INT32_MAX;
    }
};

struct TimingSnapshot {
    uint32_t count {0U}, max_execution_us {0U}, max_latency_us {0U};
    uint32_t budget_misses {0U}, deadline_misses {0U};
    std::array<uint32_t, 8U> latency_histogram {};
};

// One recording owner; observers may read concurrently. Histogram bins are
// <=1, <=4, <=16, <=64, <=256, <=1024, <=4096, >4096 microseconds.
class TimingMetrics {
public:
    void record(const ExecutionSpec& spec, uint32_t captured, uint32_t started,
                 uint32_t finished) {
        const uint32_t execution = finished - started;
        const uint32_t latency = finished - captured;
        count_.fetch_add(1U, std::memory_order_relaxed);
        maximum(execution_, execution);
        maximum(latency_, latency);
        if (execution > spec.budget_us) budget_.fetch_add(1U, std::memory_order_relaxed);
        if (latency > spec.deadline_us) deadline_.fetch_add(1U, std::memory_order_relaxed);
        size_t bin = 0U;
        for (uint32_t limit = 1U; bin < 7U && latency > limit; limit *= 4U) ++bin;
        histogram_[bin].fetch_add(1U, std::memory_order_relaxed);
    }

    TimingSnapshot snapshot() const {
        TimingSnapshot value {count_.load(), execution_.load(), latency_.load(),
                              budget_.load(), deadline_.load(), {}};
        for (size_t i = 0U; i < histogram_.size(); ++i) value.latency_histogram[i] = histogram_[i].load();
        return value;
    }

private:
    static void maximum(std::atomic<uint32_t>& destination, uint32_t value) {
        // Only the recording owner updates maxima, so no unbounded CAS loop.
        if (value > destination.load(std::memory_order_relaxed)) destination.store(value);
    }
    std::atomic<uint32_t> count_ {0U}, execution_ {0U}, latency_ {0U};
    std::atomic<uint32_t> budget_ {0U}, deadline_ {0U};
    std::array<std::atomic<uint32_t>, 8U> histogram_ {};
};

} // namespace runtime
