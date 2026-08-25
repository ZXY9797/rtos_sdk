#pragma once

#include <atomic>
#include <cstdint>

namespace foc {

enum class ErrorCode : uint8_t {
    None = 0,
    OverCurrent,
    OverVoltage,
    UnderVoltage,
    OverTemperature,
    Stall,
    HardwareFault,
    SensorFault,
};

class ErrorHandler {
public:
    ErrorHandler() = default;

    // 检查并设置错误
    void check(float vbus, float current, float temperature);

    // 手动设置/清除错误
    void set_error(ErrorCode code);
    void clear_error(ErrorCode code);
    void clear_all();

    // 状态查询
    bool has_error() const { return error_flags_.load(std::memory_order_relaxed) != 0U; }
    bool has_error(ErrorCode code) const;
    uint32_t error_flags() const { return error_flags_.load(std::memory_order_relaxed); }
    ErrorCode last_error() const { return last_error_.load(std::memory_order_relaxed); }

    // 阈值配置
    void set_overcurrent_threshold(float threshold) { overcurrent_.store(threshold, std::memory_order_relaxed); }
    void set_overvoltage_threshold(float threshold) { overvoltage_.store(threshold, std::memory_order_relaxed); }
    void set_undervoltage_threshold(float threshold) { undervoltage_.store(threshold, std::memory_order_relaxed); }
    void set_overtemperature_threshold(float threshold) { overtemp_.store(threshold, std::memory_order_relaxed); }

private:
    std::atomic<uint32_t> error_flags_ {0U};
    std::atomic<ErrorCode> last_error_ {ErrorCode::None};

    std::atomic<float> overcurrent_ {30.0F};
    std::atomic<float> overvoltage_ {60.0F};
    std::atomic<float> undervoltage_ {10.0F};
    std::atomic<float> overtemp_ {80.0F};
};

} // namespace foc
