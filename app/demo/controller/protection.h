#pragma once

#include <cstdint>

enum class ProtectionFlag : uint32_t {
    None        = 0,
    BusOverV    = (1 << 0),
    BusUnderV   = (1 << 1),
    BoardOverT  = (1 << 2),
    MotorOverT  = (1 << 3),
    OverCurrent = (1 << 4),
};

inline ProtectionFlag operator|(ProtectionFlag a, ProtectionFlag b) {
    return static_cast<ProtectionFlag>(
        static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}

inline ProtectionFlag operator&(ProtectionFlag a, ProtectionFlag b) {
    return static_cast<ProtectionFlag>(
        static_cast<uint32_t>(a) & static_cast<uint32_t>(b));
}

inline bool operator!(ProtectionFlag f) {
    return static_cast<uint32_t>(f) == 0;
}

struct ProtectionConfig {
    float bus_overvoltage {45.0f};
    float bus_undervoltage {12.0f};
    float bus_ov_recovery {42.0f};
    float bus_uv_recovery {15.0f};
    float board_overtemp {100.0f};
    float motor_overtemp {120.0f};
    float board_ot_recovery {50.0f};
    float motor_ot_recovery {80.0f};
    float overcurrent {25.0f};
    uint32_t fast_trigger_cnt {1};
    uint32_t fast_recover_cnt {5};
    uint32_t slow_trigger_cnt {2000};
    uint32_t slow_recover_cnt {2000};
};

class Protection {
public:
    void init(const ProtectionConfig &cfg);

    void fast_check(float vbus);
    void slow_check(float board_temp, float motor_temp, float phase_current);

    bool has_fault() const { return flags_ != ProtectionFlag::None; }
    ProtectionFlag flags() const { return flags_; }
    bool allow_enable() const;

    const char *flag_str() const;

private:
    ProtectionConfig cfg_;
    ProtectionFlag flags_ {ProtectionFlag::None};

    uint32_t bus_ov_cnt_ {0};
    uint32_t bus_uv_cnt_ {0};
    uint32_t board_ot_cnt_ {0};
    uint32_t motor_ot_cnt_ {0};
    uint32_t oc_cnt_ {0};

    uint32_t bus_ov_recover_cnt_ {0};
    uint32_t bus_uv_recover_cnt_ {0};
    uint32_t board_ot_recover_cnt_ {0};
    uint32_t motor_ot_recover_cnt_ {0};
    uint32_t oc_recover_cnt_ {0};

    void check_bus_overvoltage(float vbus);
    void check_bus_undervoltage(float vbus);
    void check_board_overtemp(float temp);
    void check_motor_overtemp(float temp);
    void check_overcurrent(float current);
};
