#pragma once
#include <cstddef>
#include <cstdint>

namespace algo::motor {

struct TemperatureProtectConfig {
    bool enable {true};
    float board_over_temp_c {100.0F};
    float board_recover_temp_c {50.0F};
    float motor_over_temp_c {120.0F};
    float motor_recover_temp_c {80.0F};
    uint32_t trigger_count {2000U};
    uint32_t recover_count {2000U};
    float motor_temp_open_voltage_v {3.15F};
};

struct VbusProtectConfig {
    bool enable {true};
    float over_voltage_v {45.0F};
    float over_recover_voltage_v {42.0F};
    float under_voltage_v {12.0F};
    float under_recover_voltage_v {15.0F};
    uint32_t trigger_count {1U};
    uint32_t recover_count {5U};
};

struct TemperatureLookupPoint {
    float temperature_c {0.0F};
    float resistance_kohm {0.0F};
};

struct TemperatureSample {
    bool valid {false};
    uint8_t present_mask {0U};
    uint8_t valid_mask {0U};
    float board_temperature_c {0.0F};
    float motor_temperature_c {0.0F};
    float voltage_v[3] {};
    float temperature_c[3] {};
};

class Protection {
public:
    bool configure(const TemperatureProtectConfig& temperature, const VbusProtectConfig& bus);
    void reset();
    void update_temperature(float board_c, float motor_c, bool valid);
    void update_bus(float calibrated_voltage_v, bool valid);
    uint32_t fault_mask() const;
    bool active() const { return fault_mask() != 0U; }
    static bool temperature(float adc_voltage_v, const TemperatureLookupPoint* table,
                            size_t count, float& temperature_c);
    static TemperatureSample sample_temperatures(const float* adc_voltage_v, size_t channels,
        const TemperatureLookupPoint* table, size_t count, float motor_open_voltage_v = 3.15F);

private:
    struct Debounce {
        bool active {false};
        uint32_t trigger_count {0U};
        uint32_t recover_count {0U};
        void update(bool trigger, bool recover, uint32_t trigger_threshold,
                    uint32_t recover_threshold);
    };
    TemperatureProtectConfig temperature_ {};
    VbusProtectConfig bus_ {};
    Debounce board_ {}, motor_ {}, over_ {}, under_ {};
};

}  // namespace algo::motor
