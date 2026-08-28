#pragma once

#include <gimbal/safety_manager.h>
#include <gimbal/types.h>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace gimbal {

template <typename Value, size_t SlotCount = 3U>
class SnapshotTopic {
    static_assert(std::is_trivially_copyable_v<Value>);
    static_assert(SlotCount >= 3U);
    static_assert(std::atomic<uint32_t>::is_always_lock_free);

public:
    SnapshotTopic() = default;
    SnapshotTopic(const SnapshotTopic &) = delete;
    SnapshotTopic &operator=(const SnapshotTopic &) = delete;

    [[nodiscard]] bool publish(const Value &value)
    {
        const uint32_t current = published_.load(std::memory_order_acquire);
        for (size_t offset = 1U; offset < SlotCount; ++offset) {
            const size_t index = (current + offset) % SlotCount;
            if (readers_[index].load(std::memory_order_acquire) != 0U) {
                continue;
            }
            slots_[index] = value;
            published_.store(static_cast<uint32_t>(index),
                             std::memory_order_release);
            return true;
        }
        dropped_publications_.fetch_add(1U, std::memory_order_relaxed);
        return false;
    }

    [[nodiscard]] bool read(Value &value) const
    {
        constexpr size_t kMaximumAttempts = SlotCount * 2U;
        for (size_t attempt = 0U; attempt < kMaximumAttempts; ++attempt) {
            const uint32_t index =
                published_.load(std::memory_order_acquire);
            readers_[index].fetch_add(1U, std::memory_order_acq_rel);
            if (published_.load(std::memory_order_acquire) != index) {
                readers_[index].fetch_sub(1U, std::memory_order_release);
                continue;
            }
            value = slots_[index];
            readers_[index].fetch_sub(1U, std::memory_order_release);
            return true;
        }
        return false;
    }

    [[nodiscard]] uint32_t dropped_publications() const
    {
        return dropped_publications_.load(std::memory_order_relaxed);
    }

private:
    std::array<Value, SlotCount> slots_ {};
    mutable std::array<std::atomic<uint32_t>, SlotCount> readers_ {};
    std::atomic<uint32_t> published_ {0U};
    std::atomic<uint32_t> dropped_publications_ {0U};
};

template <typename Value, size_t Capacity>
class SpscRing {
    static_assert(std::is_trivially_copyable_v<Value>);
    static_assert(Capacity >= 2U);
    static_assert(std::atomic<size_t>::is_always_lock_free);

public:
    [[nodiscard]] bool push(const Value &value)
    {
        const size_t write = write_index_.load(std::memory_order_relaxed);
        const size_t next = increment(write);
        if (next == read_index_.load(std::memory_order_acquire)) {
            dropped_.fetch_add(1U, std::memory_order_relaxed);
            return false;
        }
        storage_[write] = value;
        write_index_.store(next, std::memory_order_release);
        return true;
    }

    [[nodiscard]] bool pop(Value &value)
    {
        const size_t read = read_index_.load(std::memory_order_relaxed);
        if (read == write_index_.load(std::memory_order_acquire)) {
            return false;
        }
        value = storage_[read];
        read_index_.store(increment(read), std::memory_order_release);
        return true;
    }

    [[nodiscard]] uint32_t dropped() const
    {
        return dropped_.load(std::memory_order_relaxed);
    }

private:
    [[nodiscard]] static constexpr size_t increment(size_t value)
    {
        return (value + 1U) % Capacity;
    }

    std::array<Value, Capacity> storage_ {};
    std::atomic<size_t> write_index_ {0U};
    std::atomic<size_t> read_index_ {0U};
    std::atomic<uint32_t> dropped_ {0U};
};

struct SharedTopics {
    SnapshotTopic<ImuSample> imu_state;
    SnapshotTopic<JointState> joint_state;
    SnapshotTopic<ImuThermalState> imu_thermal_state;
    SnapshotTopic<AttitudeState> attitude_state;
    SnapshotTopic<HandleState> handle_state;
    SnapshotTopic<MotionReference> motion_reference;
    SnapshotTopic<MotorCommand> motor_command;
    SnapshotTopic<MotorFeedback> motor_feedback;
    SnapshotTopic<CapabilitySet> capabilities;
    SnapshotTopic<SafetyOutput> safety_output;
};

} // namespace gimbal
