#pragma once
#include <cstddef>
#include <cstdint>

namespace runtime {
class WireWriter {
public:
    WireWriter(uint8_t* data, size_t size) : data_(data), size_(data != nullptr ? size : 0U) {}
    bool u8(uint8_t value) {
        if (used_ == size_) return false;
        data_[used_++] = value;
        return true;
    }
    bool u16(uint16_t value) { return integer(value, 2U); }
    bool u32(uint32_t value) { return integer(value, 4U); }
    bool text(const char* value, size_t maximum) {
        size_t n = 0U;
        if (value != nullptr) while (n < maximum && value[n] != '\0') ++n;
        if (n > UINT8_MAX || remaining() < n + 1U) return false;
        u8(static_cast<uint8_t>(n));
        for (size_t i = 0U; i < n; ++i) u8(static_cast<uint8_t>(value[i]));
        return true;
    }
    size_t used() const { return used_; }
    size_t remaining() const { return size_ - used_; }
private:
    bool integer(uint32_t value, size_t bytes) {
        if (remaining() < bytes) return false;
        for (size_t i = 0U; i < bytes; ++i) u8(static_cast<uint8_t>(value >> (8U * i)));
        return true;
    }
    uint8_t* data_;
    size_t size_, used_ {0U};
};
}
