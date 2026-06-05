#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

/**
 * @brief 单生产者/单消费者 (SPSC) 无锁环形缓冲区
 *
 * ISR 安全性说明：
 * 在 ARM Cortex-M 上，单字 volatile 读写是原子的，且内存模型强有序，
 * 因此 volatile 足以保证 ISR（生产者）和主线程（消费者）之间的正确性。
 *
 * 约束：
 * - 仅支持一个生产者（通常是 ISR）和一个消费者（通常是主线程）
 * - write() 仅由生产者调用，read() 仅由消费者调用
 * - space() 的返回值是瞬时值，不应用于关键决策
 */
class RingBuf {
public:
    constexpr RingBuf(uint8_t *buf, size_t capacity)
        : m_buf(buf), m_cap(capacity), m_head(0), m_tail(0) {}

    size_t write(const uint8_t *data, size_t len) {
        size_t written = 0;
        while (written < len && space() > 0) {
            m_buf[m_head] = data[written];
            m_head = (m_head + 1) % m_cap;
            written++;
        }
        return written;
    }

    size_t read(uint8_t *data, size_t len) {
        size_t rd = 0;
        while (rd < len && m_tail != m_head) {
            data[rd] = m_buf[m_tail];
            m_tail = (m_tail + 1) % m_cap;
            rd++;
        }
        return rd;
    }

    size_t size() const {
        if (m_head >= m_tail) return m_head - m_tail;
        return m_cap - m_tail + m_head;
    }

    /** 可用空间（会浪费一个槽位以区分满/空状态） */
    size_t space() const {
        return m_cap - 1 - size();
    }

    bool empty() const { return m_head == m_tail; }

    void reset() { m_head = 0; m_tail = 0; }

private:
    uint8_t *m_buf;
    size_t m_cap;
    volatile size_t m_head;  // 仅由生产者写入
    volatile size_t m_tail;  // 仅由消费者写入
};
