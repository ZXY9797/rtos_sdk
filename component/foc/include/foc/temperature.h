#pragma once

#include <cstdint>

namespace foc {

class TemperatureEstimator {
public:
    struct Config {
        float r_ntc_25c {10000.0f};   // NTC 25°C 电阻
        float beta {3950.0f};          // NTC B 值
        float r_series {10000.0f};     // 串联电阻
        float v_ref {3.3f};            // 参考电压
    };

    TemperatureEstimator();
    explicit TemperatureEstimator(const Config &cfg);

    // 更新温度估算
    // adc_value: ADC 原始值 (12-bit)
    void update(uint16_t adc_value);

    // 温度输出 (°C)
    float celsius() const { return celsius_; }

private:
    Config cfg_;
    float celsius_ {25.0f};
};

} // namespace foc
