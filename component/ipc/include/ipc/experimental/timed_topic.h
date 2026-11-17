#pragma once

#include <ipc/snapshot_topic.h>

namespace ipc::experimental {

enum class ReadStatus : uint8_t { Fresh, Empty, Contended, Invalid, Stale };

template <typename T>
struct Sample {
    T value {};
    uint32_t sequence {0U};
    uint32_t capture_time_us {0U};
    uint16_t schema {1U};
    bool valid {false};
};

// Latest-value delivery. Configuration and hook replacement are owner-only,
// before publishers start. The wake hook runs in the publisher's context;
// an ISR publisher requires an ISR-safe, bounded hook.
template <typename T, size_t Slots = 3U>
class TimedTopic {
public:
    using Wake = void (*)(void*);

    void configure_wakeup(Wake wake, void* context) { wake_ = wake; context_ = context; }

    bool publish(const Sample<T>& sample) {
        if (!topic_.publish(sample)) return false;
        populated_.store(true, std::memory_order_release);
        if (wake_ != nullptr) wake_(context_);
        return true;
    }

    ReadStatus read(Sample<T>& sample, uint32_t now_us, uint32_t max_age_us) const {
        if (!populated_.load(std::memory_order_acquire)) return ReadStatus::Empty;
        if (!topic_.read(sample)) return ReadStatus::Contended;
        if (!sample.valid || sample.schema != 1U) return ReadStatus::Invalid;
        // Intervals must be <= INT32_MAX; future timestamps are rejected too.
        if (max_age_us > INT32_MAX || now_us - sample.capture_time_us > max_age_us) {
            return ReadStatus::Stale;
        }
        return ReadStatus::Fresh;
    }

    uint32_t dropped() const { return topic_.dropped_publications(); }

private:
    SnapshotTopic<Sample<T>, Slots> topic_;
    std::atomic<bool> populated_ {false};
    Wake wake_ {nullptr};
    void* context_ {nullptr};
};

} // namespace ipc::experimental
