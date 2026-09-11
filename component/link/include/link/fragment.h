#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

#ifndef CONFIG_LINK_MAX_FRAME_SIZE
#define CONFIG_LINK_MAX_FRAME_SIZE 512
#endif

namespace link {

/** Fixed-buffer reassembler for one ordered fragment stream. */
class Reassembler {
public:
    static constexpr size_t MAX_FRAME = CONFIG_LINK_MAX_FRAME_SIZE;

    bool append_fragment(uint8_t index, const uint8_t* data, size_t len,
                         bool last)
    {
        if (data == nullptr || len == 0U || len > MAX_FRAME) {
            reset();
            return false;
        }
        // A new first fragment is an explicit resynchronization point. Keep
        // it instead of dropping the first frame after an interrupted stream.
        if (index == 0U && expected_index_ != 0U) {
            reset();
        }
        if (ready_ || index != expected_index_ || len > MAX_FRAME - widx_) {
            reset();
            return false;
        }
        std::memcpy(buf_ + widx_, data, len);
        widx_ += len;
        if (index == 0x7FU && !last) {
            // The wire index has only seven bits. Continuing would make a
            // wrapped fragment stream indistinguishable from a new frame.
            reset();
            return false;
        }
        expected_index_ = static_cast<uint8_t>(
            (expected_index_ + 1U) & 0x7FU);
        if (last) {
            ready_ = true;
            ready_len_ = widx_;
            read_idx_ = 0U;
        }
        return true;
    }

    int read(uint8_t* buffer, size_t capacity)
    {
        if (!ready_ || buffer == nullptr || capacity == 0U) {
            return 0;
        }
        const size_t remaining = ready_len_ - read_idx_;
        const size_t read_size = remaining < capacity ? remaining : capacity;
        std::memcpy(buffer, buf_ + read_idx_, read_size);
        read_idx_ += read_size;
        if (read_idx_ == ready_len_) {
            reset();
        }
        return static_cast<int>(read_size);
    }

    void reset()
    {
        widx_ = 0U;
        read_idx_ = 0U;
        ready_len_ = 0U;
        expected_index_ = 0U;
        ready_ = false;
    }

private:
    uint8_t buf_[MAX_FRAME] {};
    size_t widx_ {0U};
    size_t read_idx_ {0U};
    size_t ready_len_ {0U};
    uint8_t expected_index_ {0U};
    bool ready_ {false};
};

/** One-byte fragment header: bit 7 is last, bits 6..0 are the index. */
class Fragmenter {
public:
    template<size_t MaxFragmentPayload, typename SendFunction>
    static int send(const uint8_t* data, size_t len, size_t fragment_payload,
                    SendFunction&& send_function)
    {
        static_assert(MaxFragmentPayload > 0U);
        static_assert(MaxFragmentPayload <= Reassembler::MAX_FRAME);
        if ((len > 0U && data == nullptr) || len > Reassembler::MAX_FRAME
            || fragment_payload == 0U
            || fragment_payload > MaxFragmentPayload
            || len > 128U * fragment_payload) {
            return -1;
        }
        size_t offset = 0U;
        uint8_t index = 0U;
        while (offset < len) {
            const size_t remaining = len - offset;
            const size_t fragment_size = remaining < fragment_payload
                ? remaining : fragment_payload;
            uint8_t fragment[1U + MaxFragmentPayload] {};
            const bool last = offset + fragment_size >= len;
            fragment[0] = static_cast<uint8_t>(
                index | (last ? 0x80U : 0U));
            std::memcpy(fragment + 1U, data + offset, fragment_size);
            if (!send_function(fragment, fragment_size + 1U)) {
                return static_cast<int>(offset);
            }
            offset += fragment_size;
            ++index;
        }
        return static_cast<int>(len);
    }

    static bool recv(const uint8_t* frame, size_t frame_len,
                     Reassembler& reassembler)
    {
        if (frame == nullptr || frame_len < 2U) {
            return false;
        }
        const bool last = (frame[0] & 0x80U) != 0U;
        const uint8_t index = frame[0] & 0x7FU;
        return reassembler.append_fragment(
            index, frame + 1U, frame_len - 1U, last) && last;
    }
};

} // namespace link
