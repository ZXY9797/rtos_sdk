#include <algo/filter/transfer_function.h>

#include <algo/math/basic_math.h>

namespace algo::motor::filter {

bool TransferFunction::configure(const TransferFunctionConfig& config)
{
    const uint8_t order = static_cast<uint8_t>(config.order);
    if (order > kMaxOrder) {
        configured_ = false;
        return false;
    }

    if ((order > 0U) && (config.den[0] != 1.0F)) {
        configured_ = false;
        return false;
    }

    for (uint8_t i = 0; i <= kMaxOrder; ++i) {
        if (!basic_math::is_finite_float(config.num[i]) ||
            !basic_math::is_finite_float(config.den[i])) {
            configured_ = false;
            return false;
        }
    }

    config_ = config;
    configured_ = true;
    reset();
    return true;
}

void TransferFunction::reset()
{
    for (uint8_t i = 0; i <= kMaxOrder; ++i) {
        x_[i] = 0.0F;
    }
    for (uint8_t i = 0; i < kMaxOrder; ++i) {
        y_[i] = 0.0F;
    }
}

float TransferFunction::update(float input)
{
    if (!configured_) {
        return 0.0F;
    }

    const uint8_t order = static_cast<uint8_t>(config_.order);
    if (order == 0U) {
        return config_.num[0] * input;
    }

    x_[0] = input;
    float output = 0.0F;
    for (uint8_t i = 0; i <= order; ++i) {
        output += config_.num[i] * x_[i];
    }
    for (uint8_t i = 1; i <= order; ++i) {
        output -= config_.den[i] * y_[i - 1U];
    }

    for (uint8_t i = order; i > 0U; --i) {
        x_[i] = x_[i - 1U];
    }
    for (uint8_t i = order - 1U; i > 0U; --i) {
        y_[i] = y_[i - 1U];
    }
    y_[0] = output;

    return output;
}

}  // namespace algo::motor::filter
