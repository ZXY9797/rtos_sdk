#pragma once

#include "link.h"
#include "fragment.h"
#include <osal.h>

namespace link {

// ── BleLink ──
// BLE 分片传输，frag_payload 可配置:
//   BLE 4.x (MTU=23):  frag_payload = 19
//   BLE 5.0 (MTU=247): frag_payload = 243

class BleLink : public Link {
public:
    // frag_payload: 每片最大载荷（不含 1B frag header）
    explicit BleLink(size_t frag_payload = 19)
        : frag_payload_(frag_payload) {
        valid_ = frag_payload_ > 0U
            && frag_payload_ <= kMaxBleFragmentPayload
            && completed_frames_.create(kCompletedFrameDepth,
                                        sizeof(RxFrame),
                                        completed_frame_storage_,
                                        sizeof(completed_frame_storage_))
            && register_link(this);
    }

    // Task context only. Preserve each BLE packet boundary and serialize the
    // reassembler against the Router consumer without a shallow staging queue.
    void on_receive(const uint8_t *data, size_t len) {
        if (!valid_ || data == nullptr || len < 2U
            || len > kMaxBleFragmentPayload + 1U
            || !rx_enabled_.load(std::memory_order_acquire)) {
            return;
        }
        osal::LockGuard lock(ingress_mutex_);
        if (lock.owns_lock()
            && rx_enabled_.load(std::memory_order_acquire)) {
            if (Fragmenter::recv(data, len, ingress_reasm_)) {
                ingress_frame_ = {};
                const int frame_len = ingress_reasm_.read(
                    ingress_frame_.data, sizeof(ingress_frame_.data));
                if (frame_len > 0) {
                    ingress_frame_.len = static_cast<uint16_t>(frame_len);
                    if (!completed_frames_.send(&ingress_frame_, 0U)) {
                        rx_dropped_.fetch_add(1U, std::memory_order_relaxed);
                    }
                }
            }
        }
    }

    int send(const uint8_t *data, size_t len) override {
        osal::LockGuard lock(tx_config_mutex_);
        if (!lock.owns_lock() || tx_func_ == nullptr || !is_ready()) {
            return -1;
        }
        return Fragmenter::send<kMaxBleFragmentPayload>(
            data, len, frag_payload_,
            [this](const uint8_t *buf, size_t n) -> bool {
                return tx_func_(buf, n, tx_arg_);
            });
    }

    int recv(uint8_t *buf, size_t max_len) override {
        if (buf == nullptr || max_len == 0U) {
            return 0;
        }
        osal::LockGuard lock(rx_consumer_mutex_);
        if (!lock.owns_lock()) {
            return 0;
        }
        if (read_idx_ >= current_frame_.len) {
            current_frame_ = {};
            read_idx_ = 0U;
            if (!completed_frames_.receive(&current_frame_, 0U)
                || current_frame_.len == 0U
                || current_frame_.len > sizeof(current_frame_.data)) {
                current_frame_ = {};
                return 0;
            }
        }
        const size_t remaining = current_frame_.len - read_idx_;
        const size_t read_len = remaining < max_len ? remaining : max_len;
        std::memcpy(buf, current_frame_.data + read_idx_, read_len);
        read_idx_ += read_len;
        return static_cast<int>(read_len);
    }

    bool is_ready() const override {
        return valid_ && connected_.load(std::memory_order_acquire);
    }

    void set_connected(bool connected) {
        if (connected) {
            rx_enabled_.store(true, std::memory_order_release);
            connected_.store(true, std::memory_order_release);
        } else {
            connected_.store(false, std::memory_order_release);
            rx_enabled_.store(false, std::memory_order_release);
        }
    }

    [[nodiscard]] uint32_t rx_dropped() const {
        return rx_dropped_.load(std::memory_order_relaxed);
    }

    // Task context only. Call after disconnecting/unregistering the producer.
    void reset_rx() {
        osal::LockGuard ingress_lock(ingress_mutex_);
        osal::LockGuard consumer_lock(rx_consumer_mutex_);
        if (!ingress_lock.owns_lock() || !consumer_lock.owns_lock()) {
            return;
        }
        ingress_reasm_.reset();
        (void)completed_frames_.reset();
        ingress_frame_ = {};
        current_frame_ = {};
        read_idx_ = 0U;
        rx_dropped_.store(0U, std::memory_order_relaxed);
    }

    // 绑定 BLE 发送函数
    // Return true only when the complete fragment was accepted for TX.
    using TxFunc = bool (*)(const uint8_t *data, size_t len, void *arg);
    void set_tx_func(TxFunc func, void *arg = nullptr) {
        osal::LockGuard lock(tx_config_mutex_);
        if (lock.owns_lock()) {
            tx_func_ = func;
            tx_arg_ = arg;
        }
    }

private:
    // ATT payload minus this protocol's one-byte fragment header.
    static constexpr size_t kMaxBleFragmentPayload = 243U;
    static constexpr size_t kCompletedFrameDepth = 2U;

    struct RxFrame {
        uint16_t len {0U};
        uint8_t data[Reassembler::MAX_FRAME] {};
    };

    size_t frag_payload_;
    bool valid_ {false};
    std::atomic<bool> connected_ {false};
    std::atomic<bool> rx_enabled_ {false};
    std::atomic<uint32_t> rx_dropped_ {0U};
    osal::Mutex ingress_mutex_;
    osal::Mutex rx_consumer_mutex_;
    osal::MessageQueue completed_frames_;
    alignas(std::max_align_t)
        uint8_t completed_frame_storage_[
            osal::MessageQueue::storage_size<
                RxFrame, kCompletedFrameDepth>()] {};
    osal::Mutex tx_config_mutex_;
    TxFunc tx_func_ {nullptr};
    void *tx_arg_ {nullptr};
    Reassembler ingress_reasm_;
    RxFrame ingress_frame_ {};
    RxFrame current_frame_ {};
    size_t read_idx_ {0U};

};

} // namespace link
