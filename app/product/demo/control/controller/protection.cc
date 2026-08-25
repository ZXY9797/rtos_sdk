#include "protection.h"

void Protection::init(const ProtectionConfig &cfg) {
    cfg_ = cfg;
    flags_.store(0U, std::memory_order_release);
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
            flags_.fetch_or(static_cast<uint32_t>(flag), std::memory_order_acq_rel);
        }
    } else if (vbus < cfg_.bus_ov_recovery) {
        if (!!(flags() & flag)) {
            bus_ov_recover_cnt_++;
            if (bus_ov_recover_cnt_ >= cfg_.fast_recover_cnt) {
                flags_.fetch_and(~static_cast<uint32_t>(flag),
                                 std::memory_order_acq_rel);
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
            flags_.fetch_or(static_cast<uint32_t>(flag), std::memory_order_acq_rel);
        }
    } else if (vbus > cfg_.bus_uv_recovery) {
        if (!!(flags() & flag)) {
            bus_uv_recover_cnt_++;
            if (bus_uv_recover_cnt_ >= cfg_.fast_recover_cnt) {
                flags_.fetch_and(~static_cast<uint32_t>(flag),
                                 std::memory_order_acq_rel);
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
            flags_.fetch_or(static_cast<uint32_t>(flag), std::memory_order_acq_rel);
        }
    } else if (temp < cfg_.board_ot_recovery) {
        if (!!(flags() & flag)) {
            board_ot_recover_cnt_++;
            if (board_ot_recover_cnt_ >= cfg_.slow_recover_cnt) {
                flags_.fetch_and(~static_cast<uint32_t>(flag),
                                 std::memory_order_acq_rel);
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
            flags_.fetch_or(static_cast<uint32_t>(flag), std::memory_order_acq_rel);
        }
    } else if (temp < cfg_.motor_ot_recovery) {
        if (!!(flags() & flag)) {
            motor_ot_recover_cnt_++;
            if (motor_ot_recover_cnt_ >= cfg_.slow_recover_cnt) {
                flags_.fetch_and(~static_cast<uint32_t>(flag),
                                 std::memory_order_acq_rel);
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
            flags_.fetch_or(static_cast<uint32_t>(flag), std::memory_order_acq_rel);
        }
    } else {
        if (!!(flags() & flag)) {
            oc_recover_cnt_++;
            if (oc_recover_cnt_ >= cfg_.fast_recover_cnt) {
                flags_.fetch_and(~static_cast<uint32_t>(flag),
                                 std::memory_order_acq_rel);
                oc_cnt_ = 0;
            }
        } else {
            oc_cnt_ = 0;
        }
    }
}

bool Protection::allow_enable() const {
    return !has_fault();
}

const char *Protection::flag_str() const {
    static constexpr const char *kFlagNames[32] = {
        "OK", "OV", "UV", "OV UV", "BOT", "OV BOT", "UV BOT",
        "OV UV BOT", "MOT", "OV MOT", "UV MOT", "OV UV MOT",
        "BOT MOT", "OV BOT MOT", "UV BOT MOT", "OV UV BOT MOT",
        "OC", "OV OC", "UV OC", "OV UV OC", "BOT OC", "OV BOT OC",
        "UV BOT OC", "OV UV BOT OC", "MOT OC", "OV MOT OC",
        "UV MOT OC", "OV UV MOT OC", "BOT MOT OC", "OV BOT MOT OC",
        "UV BOT MOT OC", "OV UV BOT MOT OC",
    };
    const uint32_t value = flags_.load(std::memory_order_acquire);
    return kFlagNames[value & 0x1FU];
}
