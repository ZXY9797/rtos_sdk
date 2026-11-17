#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace link {

enum class FragmentSendResult : uint8_t { Sent, Busy, Failed };

// Single serialized owner. Admission copies the entire encoded frame. Busy
// preserves the fragment cursor; a permanent hardware error drops that frame.
template <size_t MaxFrame, size_t Depth = 2U, size_t FragmentPayload = 7U>
class FrameTxQueue {
    static_assert(MaxFrame > 0U && Depth > 0U && FragmentPayload > 0U && FragmentPayload <= 243U);
public:
    bool enqueue(const uint8_t* data, size_t size, size_t payload) {
        if (data == nullptr || size == 0U || size > MaxFrame || payload == 0U
            || payload > FragmentPayload || size > 128U * payload || count_ == Depth) return false;
        Frame& frame = frames_[(head_ + count_) % Depth];
        std::memcpy(frame.bytes.data(), data, size);
        frame.size = size;
        frame.offset = 0U;
        frame.payload = payload;
        ++count_;
        if (count_ > high_water_) high_water_ = count_;
        return true;
    }

    template <typename Send>
    void process(Send&& send, size_t budget = 3U) {
        for (size_t attempt = 0U; attempt < budget && count_ > 0U; ++attempt) {
            Frame& frame = frames_[head_];
            const size_t remaining = frame.size - frame.offset;
            const size_t n = remaining < frame.payload ? remaining : frame.payload;
            uint8_t fragment[1U + FragmentPayload] {};
            fragment[0] = static_cast<uint8_t>(frame.offset / frame.payload);
            if (n == remaining) fragment[0] |= 0x80U;
            std::memcpy(fragment + 1U, frame.bytes.data() + frame.offset, n);
            const FragmentSendResult result = send(fragment, n + 1U);
            if (result == FragmentSendResult::Busy) return;
            if (result == FragmentSendResult::Failed) {
                ++failed_;
                pop();
                continue;
            }
            frame.offset += n;
            if (frame.offset == frame.size) { ++completed_; pop(); }
        }
    }

    void reset() { head_ = 0U; count_ = 0U; ++epoch_; }
    size_t pending() const { return count_; }
    size_t high_water() const { return high_water_; }
    uint32_t completed() const { return completed_; }
    uint32_t failed() const { return failed_; }
    uint32_t epoch() const { return epoch_; }

private:
    struct Frame {
        std::array<uint8_t, MaxFrame> bytes {};
        size_t size {0U};
        size_t offset {0U};
        size_t payload {0U};
    };
    void pop() { head_ = (head_ + 1U) % Depth; --count_; }
    std::array<Frame, Depth> frames_ {};
    size_t head_ {0U}, count_ {0U}, high_water_ {0U};
    uint32_t completed_ {0U}, failed_ {0U}, epoch_ {0U};
};

} // namespace link
