#pragma once

#include <cstdint>

namespace foc {

class DataLogger {
public:
    DataLogger() = default;

    // 记录一帧数据
    void log(float id, float iq, float speed, float vbus, float temp);

    // 读取最近数据
    struct Frame {
        float id, iq, speed, vbus, temp;
        uint32_t timestamp;
    };

    const Frame &latest() const { return buffer_[write_idx_]; }

    // 缓冲区大小
    static constexpr uint32_t kBufferSize = 64;

private:
    Frame buffer_[kBufferSize] {};
    uint32_t write_idx_ {0};
};

} // namespace foc
