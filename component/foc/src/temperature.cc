#include <foc/temperature.h>
#include <cmath>

namespace foc {

TemperatureEstimator::TemperatureEstimator(const Config &cfg) : cfg_(cfg) {}

void TemperatureEstimator::update(uint16_t adc_value) {
    if (adc_value >= 4095) return; // 满量程 = 开路

    // ADC 值 → 电压
    float v_adc = static_cast<float>(adc_value) * cfg_.v_ref / 4096.0f;

    // 分压计算 NTC 电阻
    float r_ntc = cfg_.r_series * v_adc / (cfg_.v_ref - v_adc);

    // Steinhart-Hart 简化: 1/T = 1/T0 + (1/B)*ln(R/R0)
    const float T0 = 298.15f; // 25°C = 298.15K
    float ln_ratio = logf(r_ntc / cfg_.r_ntc_25c);
    float inv_T = (1.0f / T0) + ln_ratio / cfg_.beta;
    float T_kelvin = 1.0f / inv_T;

    celsius_ = T_kelvin - 273.15f;
}

} // namespace foc
