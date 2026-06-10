#pragma once

#include <cstdint>

struct NtcPoint {
    float temp_c;
    float resistance_kohm;
};

struct NtcConfig {
    float divider_resistance {10000.0f};  // 分压电阻 (Ω)
    float v_ref {3.3f};                   // ADC 参考电压
};

class NtcSensor {
public:
    void init(const NtcConfig &cfg, const NtcPoint *table, uint32_t table_size);

    // 从 ADC 电压值转换为温度 (°C)
    float voltage_to_temp(float voltage) const;

    // 从 ADC 原始值转换 (12-bit)
    float raw_to_temp(uint16_t raw) const;

    uint32_t table_size() const { return table_size_; }

private:
    NtcConfig cfg_;
    const NtcPoint *table_ {nullptr};
    uint32_t table_size_ {0};
};
