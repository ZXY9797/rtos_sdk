#pragma once

#include <stdint.h>

namespace algo::motor::filter {

inline constexpr uint8_t kMaxOrder = 5;

enum class Order : uint8_t {
    Zero = 0,
    First = 1,
    Second = 2,
    Third = 3,
    Fourth = 4,
    Fifth = 5,
};

struct TransferFunctionConfig {
    Order order = Order::Zero;
    float num[kMaxOrder + 1] {};
    float den[kMaxOrder + 1] {};
};

class TransferFunction {
public:
    bool configure(const TransferFunctionConfig& config);
    void reset();
    float update(float input);

    const TransferFunctionConfig& config() const { return config_; }
    bool configured() const { return configured_; }

private:
    TransferFunctionConfig config_ {};
    float x_[kMaxOrder + 1] {};
    float y_[kMaxOrder] {};
    bool configured_ = false;
};

}  // namespace algo::motor::filter
