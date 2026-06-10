#pragma once

#include "link.h"
#include "fragment.h"
#include <drivers/can.h>
#include <cstring>

namespace link {

// ── CanLink ──
// CAN 分片传输，frag_payload 可配置:
//   CAN classic (8B): frag_payload = 7
//   CAN FD (64B):     frag_payload = 63

class CanLink : public Link {
public:
    // frag_payload: 每片最大载荷（不含 1B frag header）
    CanLink(uint8_t can_port, uint32_t can_id, size_t frag_payload = 7)
        : can_(hal::Can::instance(can_port)), can_id_(can_id),
          frag_payload_(frag_payload) {
        register_link(this);
        set_poll(poll_impl, this);
    }

    int send(const uint8_t *data, size_t len) override {
        return Fragmenter::send(data, len, frag_payload_,
            [this](const uint8_t *buf, size_t n) -> bool {
                return can_.send(can_id_, buf, static_cast<uint8_t>(n), 0)
                       == hal::Status::Ok;
            });
    }

    int recv(uint8_t *buf, size_t max_len) override {
        return reasm_.read(buf, max_len);
    }

    bool is_ready() const override { return can_.is_initialized(); }

private:
    hal::Can &can_;
    uint32_t can_id_;
    size_t frag_payload_;
    Reassembler reasm_;

    static void poll_impl(void *arg) {
        auto *self = static_cast<CanLink *>(arg);
        uint32_t id;
        uint8_t data[64], len;
        while (self->can_.get_rx_message(&id, data, &len) == hal::Status::Ok) {
            Fragmenter::recv(data, len, self->reasm_);
        }
    }
};

} // namespace link
