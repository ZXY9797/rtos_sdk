#include "algo/ntc_sensor.h"
#include <cmath>

void NtcSensor::init(const NtcConfig &cfg, const NtcPoint *table, uint32_t table_size) {
    cfg_ = cfg;
    table_ = table;
    table_size_ = table_size;
}

float NtcSensor::raw_to_temp(uint16_t raw) const {
    float voltage = static_cast<float>(raw) * cfg_.v_ref / 4096.0f;
    return voltage_to_temp(voltage);
}

float NtcSensor::voltage_to_temp(float voltage) const {
    if (!table_ || table_size_ < 2) return 25.0f;

    float r_ntc;
    if (voltage < 0.001f) {
        r_ntc = table_[table_size_ - 1].resistance_kohm * 1000.0f;
    } else if (voltage > cfg_.v_ref - 0.001f) {
        r_ntc = table_[0].resistance_kohm * 1000.0f;
    } else {
        r_ntc = cfg_.divider_resistance * voltage / (cfg_.v_ref - voltage);
    }

    float r_kohm = r_ntc / 1000.0f;

    if (r_kohm >= table_[0].resistance_kohm) {
        return table_[0].temp_c;
    }
    if (r_kohm <= table_[table_size_ - 1].resistance_kohm) {
        return table_[table_size_ - 1].temp_c;
    }

    for (uint32_t i = 0; i < table_size_ - 1; i++) {
        float r_hi = table_[i].resistance_kohm;
        float r_lo = table_[i + 1].resistance_kohm;
        if (r_kohm <= r_hi && r_kohm >= r_lo) {
            float t = (r_kohm - r_hi) / (r_lo - r_hi);
            return table_[i].temp_c + t * (table_[i + 1].temp_c - table_[i].temp_c);
        }
    }

    return 25.0f;
}
