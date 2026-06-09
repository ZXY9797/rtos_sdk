#include <foc/error_handler.h>
#include <cmath>

namespace foc {

void ErrorHandler::check(float vbus, float current, float temperature) {
    if (fabsf(current) > overcurrent_) {
        set_error(ErrorCode::OverCurrent);
    }
    if (vbus > overvoltage_) {
        set_error(ErrorCode::OverVoltage);
    }
    if (vbus < undervoltage_) {
        set_error(ErrorCode::UnderVoltage);
    }
    if (temperature > overtemp_) {
        set_error(ErrorCode::OverTemperature);
    }
}

void ErrorHandler::set_error(ErrorCode code) {
    error_flags_ |= (1U << static_cast<uint8_t>(code));
    last_error_ = code;
}

void ErrorHandler::clear_error(ErrorCode code) {
    error_flags_ &= ~(1U << static_cast<uint8_t>(code));
}

void ErrorHandler::clear_all() {
    error_flags_ = 0;
    last_error_ = ErrorCode::None;
}

bool ErrorHandler::has_error(ErrorCode code) const {
    return (error_flags_ & (1U << static_cast<uint8_t>(code))) != 0;
}

} // namespace foc
