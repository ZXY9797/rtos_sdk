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

    void append(const uint8_t *data, size_t len) {
        if (widx_ + len > MAX_FRAME) { reset(); return; }
        memcpy(buf_ + widx_, data, len);
        widx_ += len;
    }

    void finish() {
        ready_ = true;
        ready_len_ = widx_;
    }

    int read(uint8_t *buf, size_t max_len) {
        if (!ready_) return 0;
        size_t n = (ready_len_ < max_len) ? ready_len_ : max_len;
        memcpy(buf, buf_, n);
        ready_ = false;
        widx_ = 0;
        return static_cast<int>(n);
    }

    void reset() { widx_ = 0; ready_ = false; }

private:
    uint8_t buf_[MAX_FRAME] {};
    size_t widx_ {0};
    size_t ready_len_ {0};
    bool ready_ {false};
};

// ── 分片发送/接收工具 ──
// 分片格式: [frag_hdr(1B) | payload(最多 N 字节)]
// frag_hdr: bit7=最后一片标志, bit6..0=片索引

class Fragmenter {
public:
    // 分片发送：将 data 按 frag_payload 大小切片，逐片调用 send_fn
    // send_fn(frame_buf, frame_len) -> bool，返回 false 中断发送
    template<typename SendFn>
    static int send(const uint8_t *data, size_t len, size_t frag_payload,
                    SendFn &&send_fn) {
        size_t off = 0;
        uint8_t idx = 0;
        while (off < len) {
            size_t n = len - off;
            if (n > frag_payload) n = frag_payload;
            uint8_t frag[1 + 64]; // frag_hdr + max CAN FD payload
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
        if (frame_len < 2) return false;
        bool last = frame[0] & 0x80;
        reasm.append(frame + 1, frame_len - 1);
        if (last) reasm.finish();
        return last;
    }
};

} // namespace link
