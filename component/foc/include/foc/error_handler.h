#pragma once

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
    bool has_error() const { return error_flags_ != 0; }
    bool has_error(ErrorCode code) const;
    uint32_t error_flags() const { return error_flags_; }
    ErrorCode last_error() const { return last_error_; }

    // 阈值配置
    void set_overcurrent_threshold(float threshold) { overcurrent_ = threshold; }
    void set_overvoltage_threshold(float threshold) { overvoltage_ = threshold; }
    void set_undervoltage_threshold(float threshold) { undervoltage_ = threshold; }
    void set_overtemperature_threshold(float threshold) { overtemp_ = threshold; }

private:
    uint32_t error_flags_ {0};
    ErrorCode last_error_ {ErrorCode::None};

    float overcurrent_ {30.0f};
    float overvoltage_ {60.0f};
    float undervoltage_ {10.0f};
    float overtemp_ {80.0f};
};

} // namespace foc
