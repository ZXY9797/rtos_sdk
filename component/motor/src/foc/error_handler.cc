#include <foc/error_handler.h>
#include <cmath>

namespace foc {

void ErrorHandler::check(float vbus, float current, float temperature) {
    if (fabsf(current) > overcurrent_.load(std::memory_order_relaxed)) {
        set_error(ErrorCode::OverCurrent);
    }
    if (vbus > overvoltage_.load(std::memory_order_relaxed)) {
        set_error(ErrorCode::OverVoltage);
    }
    if (vbus < undervoltage_.load(std::memory_order_relaxed)) {
        set_error(ErrorCode::UnderVoltage);
    }
    if (temperature > overtemp_.load(std::memory_order_relaxed)) {
        set_error(ErrorCode::OverTemperature);
    }
}

void ErrorHandler::set_error(ErrorCode code) {
    error_flags_.fetch_or(1UL << static_cast<uint8_t>(code),
                          std::memory_order_relaxed);
    last_error_.store(code, std::memory_order_relaxed);
}

void ErrorHandler::clear_error(ErrorCode code) {
    error_flags_.fetch_and(~(1UL << static_cast<uint8_t>(code)),
                           std::memory_order_relaxed);
}

void ErrorHandler::clear_all() {
    error_flags_.store(0U, std::memory_order_relaxed);
    last_error_.store(ErrorCode::None, std::memory_order_relaxed);
}

bool ErrorHandler::has_error(ErrorCode code) const {
    return (error_flags_.load(std::memory_order_relaxed)
            & (1UL << static_cast<uint8_t>(code))) != 0U;
}

} // namespace foc
