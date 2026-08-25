#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

#ifndef CONFIG_LINK_MAX_FRAME_SIZE
#define CONFIG_LINK_MAX_FRAME_SIZE 512
#endif

namespace link {

// ── 分片重组器 ──
// CAN/BLE 等 MTU 受限的链路共用，将多个小帧重组为完整 link 帧

class Reassembler {
public:
    static constexpr size_t MAX_FRAME = CONFIG_LINK_MAX_FRAME_SIZE;

    bool append_fragment(uint8_t index, const uint8_t *data, size_t len,
                         bool last) {
        if (ready_ || data == nullptr || len == 0U
            || index != expected_index_ || len > MAX_FRAME - widx_) {
            reset();
            return false;
        }
        memcpy(buf_ + widx_, data, len);
        widx_ += len;
        if (index == 0x7FU && !last) {
            // The wire index has only seven bits. Continuing would make a
            // wrapped fragment stream indistinguishable from a new frame.
            reset();
            return false;
        }
        expected_index_ = static_cast<uint8_t>((expected_index_ + 1U) & 0x7FU);
        if (last) {
            ready_ = true;
            ready_len_ = widx_;
            read_idx_ = 0U;
        }
        return true;
    }

    int read(uint8_t *buf, size_t max_len) {
        if (!ready_ || buf == nullptr || max_len == 0U) return 0;
        const size_t remaining = ready_len_ - read_idx_;
        const size_t n = (remaining < max_len) ? remaining : max_len;
        memcpy(buf, buf_ + read_idx_, n);
        read_idx_ += n;
        if (read_idx_ == ready_len_) {
            reset();
        }
        return static_cast<int>(n);
    }

    void reset() {
        widx_ = 0U;
        read_idx_ = 0U;
        ready_len_ = 0U;
        expected_index_ = 0U;
        ready_ = false;
    }

private:
    uint8_t buf_[MAX_FRAME] {};
    size_t widx_ {0};
    size_t read_idx_ {0};
    size_t ready_len_ {0};
    uint8_t expected_index_ {0U};
    bool ready_ {false};
};

// ── 分片发送/接收工具 ──
// 分片格式: [frag_hdr(1B) | payload(最多 N 字节)]
// frag_hdr: bit7=最后一片标志, bit6..0=片索引

class Fragmenter {
public:
    // 分片发送：将 data 按 frag_payload 大小切片，逐片调用 send_fn
    // send_fn(frame_buf, frame_len) -> bool，返回 false 中断发送
    template<size_t MaxFragmentPayload, typename SendFn>
    static int send(const uint8_t *data, size_t len, size_t frag_payload,
                    SendFn &&send_fn) {
        static_assert(MaxFragmentPayload > 0U);
        static_assert(MaxFragmentPayload <= Reassembler::MAX_FRAME);
        if ((len > 0U && data == nullptr) || len > Reassembler::MAX_FRAME
            || frag_payload == 0U || frag_payload > MaxFragmentPayload
            || len > 128U * frag_payload) {
            return -1;
        }
        size_t off = 0;
        uint8_t idx = 0;
        while (off < len) {
            size_t n = len - off;
            if (n > frag_payload) n = frag_payload;
            uint8_t frag[1U + MaxFragmentPayload];
            frag[0] = idx | (off + n >= len ? 0x80 : 0);
            memcpy(frag + 1, data + off, n);
            if (!send_fn(frag, n + 1)) return static_cast<int>(off);
            off += n;
            idx++;
        }
        return static_cast<int>(len);
    }

    // 分片接收：从一帧中解析 frag header，追加到重组器
    // 返回 true 表示重组完成（最后一片已收到）
    static bool recv(const uint8_t *frame, size_t frame_len, Reassembler &reasm) {
        if (frame == nullptr || frame_len < 2U) return false;
        const bool last = (frame[0] & 0x80U) != 0U;
        const uint8_t index = frame[0] & 0x7FU;
        return reasm.append_fragment(index, frame + 1U, frame_len - 1U, last)
            && last;
    }
};

} // namespace link
