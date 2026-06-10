#pragma once

#include "link.h"
#include "fragment.h"
#include <osal.h>

namespace link {

// ── BleLink ──
// BLE 分片传输，frag_payload 可配置:
//   BLE 4.x (MTU=23):  frag_payload = 20
//   BLE 5.0 (MTU=247): frag_payload = 244

class BleLink : public Link {
public:
    // frag_payload: 每片最大载荷（不含 1B frag header）
    explicit BleLink(size_t frag_payload = 20)
        : frag_payload_(frag_payload) {
        (void)rx_buf_.create(CONFIG_LINK_MAX_FRAME_SIZE * 2, 1);
        register_link(this);
        set_poll(poll_impl, this);
    }

    // BLE 接收回调（由 BLE 驱动调用，可能在 ISR 中）
    void on_receive(const uint8_t *data, size_t len) {
        (void)rx_buf_.send(data, len, 0);
    }

    int send(const uint8_t *data, size_t len) override {
        return Fragmenter::send(data, len, frag_payload_,
            [this](const uint8_t *buf, size_t n) -> bool {
                if (!tx_func_) return false;
                tx_func_(buf, n, tx_arg_);
                return true;
            });
    }

    int recv(uint8_t *buf, size_t max_len) override {
        return reasm_.read(buf, max_len);
    }

    bool is_ready() const override { return connected_; }

    void set_connected(bool connected) { connected_ = connected; }

    // 绑定 BLE 发送函数
    using TxFunc = void (*)(const uint8_t *data, size_t len, void *arg);
    void set_tx_func(TxFunc func, void *arg = nullptr) { tx_func_ = func; tx_arg_ = arg; }

private:
    osal::StreamBuffer rx_buf_;
    size_t frag_payload_;
    bool connected_ {false};
    TxFunc tx_func_ {nullptr};
    void *tx_arg_ {nullptr};
    Reassembler reasm_;

    static void poll_impl(void *arg) {
        auto *self = static_cast<BleLink *>(arg);
        uint8_t tmp[64];
        size_t n;
        while ((n = self->rx_buf_.receive(tmp, sizeof(tmp), 0)) > 0) {
            Fragmenter::recv(tmp, n, self->reasm_);
        }
    }
};

} // namespace link
