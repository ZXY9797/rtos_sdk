#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace ipc {

template <typename Value, std::size_t SlotCount = 3U>
class SnapshotTopic {
    static_assert(std::is_trivially_copyable_v<Value>);
    static_assert(SlotCount >= 3U);
    static_assert(std::atomic<uint32_t>::is_always_lock_free);
    static_assert(std::atomic<bool>::is_always_lock_free);

public:
    SnapshotTopic() = default;
    SnapshotTopic(const SnapshotTopic &) = delete;
    SnapshotTopic &operator=(const SnapshotTopic &) = delete;

    [[nodiscard]] bool publish(const Value &value)
    {
        if (publisher_active_.exchange(true, std::memory_order_acq_rel)) {
            dropped_publications_.fetch_add(1U, std::memory_order_relaxed);
            return false;
        }
        const uint32_t current = published_.load(std::memory_order_acquire);
        for (std::size_t offset = 1U; offset < SlotCount; ++offset) {
            const std::size_t index = (current + offset) % SlotCount;
            uint32_t expected = 0U;
            if (!claims_[index].compare_exchange_strong(
                    expected, kWriterClaim, std::memory_order_acquire,
                    std::memory_order_relaxed)) {
                continue;
            }
            slots_[index] = value;
            claims_[index].store(0U, std::memory_order_release);
            published_.store(static_cast<uint32_t>(index),
                             std::memory_order_release);
            publisher_active_.store(false, std::memory_order_release);
            return true;
        }
        dropped_publications_.fetch_add(1U, std::memory_order_relaxed);
        publisher_active_.store(false, std::memory_order_release);
        return false;
    }

    [[nodiscard]] bool read(Value &value) const
    {
        constexpr std::size_t kMaximumAttempts = SlotCount * 2U;
        for (std::size_t attempt = 0U;
             attempt < kMaximumAttempts; ++attempt) {
            const uint32_t index = published_.load(std::memory_order_acquire);
            uint32_t expected = 0U;
            if (!claims_[index].compare_exchange_strong(
                    expected, kReaderClaim, std::memory_order_acquire,
                    std::memory_order_relaxed)) {
                continue;
            }
            if (published_.load(std::memory_order_acquire) != index) {
                claims_[index].store(0U, std::memory_order_release);
                continue;
            }
            value = slots_[index];
            claims_[index].store(0U, std::memory_order_release);
            return true;
        }
        return false;
    }

    [[nodiscard]] uint32_t dropped_publications() const
    {
        return dropped_publications_.load(std::memory_order_relaxed);
    }

private:
    static constexpr uint32_t kReaderClaim = 1U;
    static constexpr uint32_t kWriterClaim = UINT32_MAX;

    std::array<Value, SlotCount> slots_ {};
    mutable std::array<std::atomic<uint32_t>, SlotCount> claims_ {};
    std::atomic<uint32_t> published_ {0U};
    std::atomic<uint32_t> dropped_publications_ {0U};
    std::atomic<bool> publisher_active_ {false};
};

template <typename Value, std::size_t Capacity>
class SpscRing {
    static_assert(std::is_trivially_copyable_v<Value>);
    static_assert(Capacity >= 2U);
    static_assert(std::atomic<std::size_t>::is_always_lock_free);

public:
    [[nodiscard]] bool push(const Value &value)
    {
        const std::size_t write =
            write_index_.load(std::memory_order_relaxed);
        const std::size_t next = increment(write);
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
        const std::size_t read =
            read_index_.load(std::memory_order_relaxed);
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
    [[nodiscard]] static constexpr std::size_t increment(
        std::size_t value)
    {
        return (value + 1U) % Capacity;
    }

    std::array<Value, Capacity> storage_ {};
    std::atomic<std::size_t> write_index_ {0U};
    std::atomic<std::size_t> read_index_ {0U};
    std::atomic<uint32_t> dropped_ {0U};
};

} // namespace ipc
