#pragma once

#include "link.h"
#include "fragment.h"
#include "frame_tx_queue.h"
#include <drivers/can.h>
#include <cstring>

namespace link {

// ── CanLink ──
// CAN 分片传输，frag_payload 可配置:
//   CAN classic (8B): frag_payload = 7
// The current HAL is classic CAN only; CAN FD requires a separate backend.

class CanLink : public Link {
public:
    // Called by the router poll task for unmatched CAN IDs; data is borrowed
    // only for this call. The callback must not block or read the CAN FIFO.
    using RawFrameHandler = void (*)(void*, uint32_t, const uint8_t*, size_t, bool);
    // frag_payload: 每片最大载荷（不含 1B frag header）
    CanLink(uint8_t can_port, uint32_t can_id, size_t frag_payload = 7,
            RawFrameHandler raw_handler = nullptr, void* raw_context = nullptr)
        : can_(hal::Can::instance(can_port < kMaxCanPorts ? can_port : 0U)),
          can_id_(can_id),
          frag_payload_(frag_payload), raw_handler_(raw_handler), raw_context_(raw_context) {
        if (can_port < kMaxCanPorts && !s_port_claimed_[can_port]
            && frag_payload_ > 0U && frag_payload_ <= 7U
            && register_link(this)) {
            s_port_claimed_[can_port] = true;
            valid_ = true;
        }
        set_poll(poll_impl, this);
    }

    int send(const uint8_t *data, size_t len) override {
        osal::LockGuard lock(tx_mutex_, 0U);
        if (!lock.owns_lock()) return -2;
        if (!is_ready() || data == nullptr || len == 0U
            || len > Reassembler::MAX_FRAME || len > 128U * frag_payload_) return -1;
        return tx_.enqueue(data, len, frag_payload_) ? static_cast<int>(len) : -2;
    }

    void reset_session() override {
        osal::LockGuard lock(tx_mutex_);
        if (!lock.owns_lock()) return;
        tx_.reset();
        reasm_.reset();
    }

    int recv(uint8_t *buf, size_t max_len) override {
        return reasm_.read(buf, max_len);
    }

    bool is_ready() const override {
        return valid_ && can_.is_initialized() && can_.is_started();
    }

private:
    static constexpr uint8_t kMaxCanPorts = 2U;
    static constexpr size_t kPollFrameBudget = 32U;
    inline static bool s_port_claimed_[kMaxCanPorts] {};

    hal::Can &can_;
    uint32_t can_id_;
    size_t frag_payload_;
    RawFrameHandler raw_handler_;
    void* raw_context_;
    bool valid_ {false};
    Reassembler reasm_;
    osal::Mutex tx_mutex_;
#if defined(CONFIG_LINK_CAN_TX_DEPTH)
    FrameTxQueue<Reassembler::MAX_FRAME, CONFIG_LINK_CAN_TX_DEPTH> tx_;
#else
    FrameTxQueue<Reassembler::MAX_FRAME, 2U> tx_;
#endif

    static void poll_impl(void *arg) {
        auto *self = static_cast<CanLink *>(arg);
        {
            osal::LockGuard lock(self->tx_mutex_, 0U);
            if (lock.owns_lock()) {
                if (!self->is_ready()) {
                    if (self->tx_.pending() > 0U) self->tx_.reset();
                    self->reasm_.reset();
                    return;
                }
                self->tx_.process([self](const uint8_t* data, size_t size) {
                    const auto status = self->can_.send(
                        self->can_id_, data, static_cast<uint8_t>(size), 0U);
                    return status == hal::Status::Ok ? FragmentSendResult::Sent
                        : status == hal::Status::Busy ? FragmentSendResult::Busy
                        : FragmentSendResult::Failed;
                });
            }
        }
        uint32_t id;
        bool is_extended = false;
        uint8_t data[64], len;
        for (size_t frame = 0U; frame < kPollFrameBudget; ++frame) {
            if (self->can_.get_rx_message(&id, data, sizeof(data), &len,
                                          &is_extended)
                != hal::Status::Ok) {
                break;
            }
            if (is_extended || id != self->can_id_) {
                if (self->raw_handler_)
                    self->raw_handler_(self->raw_context_, id, data, len, is_extended);
                continue;
            }
            if (Fragmenter::recv(data, len, self->reasm_)) {
                break;
            }
        }
    }
};

} // namespace link
