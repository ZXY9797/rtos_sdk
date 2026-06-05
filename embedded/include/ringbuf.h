#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

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

    size_t space() const {
        return m_cap - 1 - size();
    }

    bool empty() const { return m_head == m_tail; }

    void reset() { m_head = 0; m_tail = 0; }

private:
    uint8_t *m_buf;
    size_t m_cap;
    volatile size_t m_head;
    volatile size_t m_tail;
};
