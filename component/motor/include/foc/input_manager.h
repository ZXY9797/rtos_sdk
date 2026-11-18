#pragma once

#include <cstdint>

namespace foc {

class InputManager {
public:
    InputManager() = default;

    // 采集输入 (慢循环中调用)
    void collect();

    // 输入源设置
    void set_throttle(float value) { throttle_ = value; }
    void set_brake(float value) { brake_ = value; }
    void set_direction(bool forward) { forward_ = forward; }

    // 输入值读取
    float throttle() const { return throttle_; }
    float brake() const { return brake_; }
    bool direction() const { return forward_; }

    // 模式切换请求
    bool enable_requested() const { return enable_req_; }
    bool disable_requested() const { return disable_req_; }
    void clear_requests();

private:
    float throttle_ {0.0f};
    float brake_ {0.0f};
    bool forward_ {true};
    bool enable_req_ {false};
    bool disable_req_ {false};
};

} // namespace foc
