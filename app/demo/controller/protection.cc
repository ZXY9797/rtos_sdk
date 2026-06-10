#include "protection.h"
#include <cstring>

void Protection::init(const ProtectionConfig &cfg) {
    cfg_ = cfg;
    flags_ = ProtectionFlag::None;
    bus_ov_cnt_ = bus_uv_cnt_ = 0;
    board_ot_cnt_ = motor_ot_cnt_ = oc_cnt_ = 0;
    bus_ov_recover_cnt_ = bus_uv_recover_cnt_ = 0;
    board_ot_recover_cnt_ = motor_ot_recover_cnt_ = oc_recover_cnt_ = 0;
}

void Protection::fast_check(float vbus) {
    check_bus_overvoltage(vbus);
    check_bus_undervoltage(vbus);
}

void Protection::slow_check(float board_temp, float motor_temp, float phase_current) {
    check_board_overtemp(board_temp);
    check_motor_overtemp(motor_temp);
    check_overcurrent(phase_current);
}

void Protection::check_bus_overvoltage(float vbus) {
    auto flag = ProtectionFlag::BusOverV;
    if (vbus > cfg_.bus_overvoltage) {
        bus_ov_cnt_++;
        bus_ov_recover_cnt_ = 0;
        if (bus_ov_cnt_ >= cfg_.fast_trigger_cnt) {
            flags_ = flags_ | flag;
        }
    } else if (vbus < cfg_.bus_ov_recovery) {
        if (!!(flags_ & flag)) {
            bus_ov_recover_cnt_++;
            if (bus_ov_recover_cnt_ >= cfg_.fast_recover_cnt) {
                flags_ = static_cast<ProtectionFlag>(
                    static_cast<uint32_t>(flags_) & ~static_cast<uint32_t>(flag));
                bus_ov_cnt_ = 0;
            }
        } else {
            bus_ov_cnt_ = 0;
        }
    }
}

void Protection::check_bus_undervoltage(float vbus) {
    auto flag = ProtectionFlag::BusUnderV;
    if (vbus < cfg_.bus_undervoltage) {
        bus_uv_cnt_++;
        bus_uv_recover_cnt_ = 0;
        if (bus_uv_cnt_ >= cfg_.fast_trigger_cnt) {
            flags_ = flags_ | flag;
        }
    } else if (vbus > cfg_.bus_uv_recovery) {
        if (!!(flags_ & flag)) {
            bus_uv_recover_cnt_++;
            if (bus_uv_recover_cnt_ >= cfg_.fast_recover_cnt) {
                flags_ = static_cast<ProtectionFlag>(
                    static_cast<uint32_t>(flags_) & ~static_cast<uint32_t>(flag));
                bus_uv_cnt_ = 0;
            }
        } else {
            bus_uv_cnt_ = 0;
        }
    }
}

void Protection::check_board_overtemp(float temp) {
    auto flag = ProtectionFlag::BoardOverT;
    if (temp > cfg_.board_overtemp) {
        board_ot_cnt_++;
        board_ot_recover_cnt_ = 0;
        if (board_ot_cnt_ >= cfg_.slow_trigger_cnt) {
            flags_ = flags_ | flag;
        }
    } else if (temp < cfg_.board_ot_recovery) {
        if (!!(flags_ & flag)) {
            board_ot_recover_cnt_++;
            if (board_ot_recover_cnt_ >= cfg_.slow_recover_cnt) {
                flags_ = static_cast<ProtectionFlag>(
                    static_cast<uint32_t>(flags_) & ~static_cast<uint32_t>(flag));
                board_ot_cnt_ = 0;
            }
        } else {
            board_ot_cnt_ = 0;
        }
    }
}

void Protection::check_motor_overtemp(float temp) {
    auto flag = ProtectionFlag::MotorOverT;
    if (temp > cfg_.motor_overtemp) {
        motor_ot_cnt_++;
        motor_ot_recover_cnt_ = 0;
        if (motor_ot_cnt_ >= cfg_.slow_trigger_cnt) {
            flags_ = flags_ | flag;
        }
    } else if (temp < cfg_.motor_ot_recovery) {
        if (!!(flags_ & flag)) {
            motor_ot_recover_cnt_++;
            if (motor_ot_recover_cnt_ >= cfg_.slow_recover_cnt) {
                flags_ = static_cast<ProtectionFlag>(
                    static_cast<uint32_t>(flags_) & ~static_cast<uint32_t>(flag));
                motor_ot_cnt_ = 0;
            }
        } else {
            motor_ot_cnt_ = 0;
        }
    }
}

void Protection::check_overcurrent(float current) {
    auto flag = ProtectionFlag::OverCurrent;
    float abs_i = current < 0 ? -current : current;
    if (abs_i > cfg_.overcurrent) {
        oc_cnt_++;
        oc_recover_cnt_ = 0;
        if (oc_cnt_ >= cfg_.fast_trigger_cnt) {
            flags_ = flags_ | flag;
        }
    } else {
        if (!!(flags_ & flag)) {
            oc_recover_cnt_++;
            if (oc_recover_cnt_ >= cfg_.fast_recover_cnt) {
                flags_ = static_cast<ProtectionFlag>(
                    static_cast<uint32_t>(flags_) & ~static_cast<uint32_t>(flag));
                oc_cnt_ = 0;
            }
        } else {
            oc_cnt_ = 0;
        }
    }
}

bool Protection::allow_enable() const {
    return flags_ == ProtectionFlag::None;
}

const char *Protection::flag_str() const {
    static char buf[64];
    buf[0] = '\0';
    auto f = static_cast<uint32_t>(flags_);
    if (f == 0) {
        strcpy(buf, "OK");
    } else {
        if (f & static_cast<uint32_t>(ProtectionFlag::BusOverV))    strcat(buf, "OV ");
        if (f & static_cast<uint32_t>(ProtectionFlag::BusUnderV))   strcat(buf, "UV ");
        if (f & static_cast<uint32_t>(ProtectionFlag::BoardOverT))  strcat(buf, "BOT ");
        if (f & static_cast<uint32_t>(ProtectionFlag::MotorOverT))  strcat(buf, "MOT ");
        if (f & static_cast<uint32_t>(ProtectionFlag::OverCurrent)) strcat(buf, "OC ");
    }
    return buf;
}
