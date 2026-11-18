#include <algo/motor/protection.h>
#include <cmath>

namespace algo::motor {

bool Protection::configure(const TemperatureProtectConfig& temperature,
                           const VbusProtectConfig& bus)
{
    if (!std::isfinite(temperature.board_over_temp_c) ||
        !std::isfinite(temperature.board_recover_temp_c) ||
        !std::isfinite(temperature.motor_over_temp_c) ||
        !std::isfinite(temperature.motor_recover_temp_c) ||
        temperature.board_recover_temp_c >= temperature.board_over_temp_c ||
        temperature.motor_recover_temp_c >= temperature.motor_over_temp_c ||
        !std::isfinite(bus.over_voltage_v) || !std::isfinite(bus.over_recover_voltage_v) ||
        !std::isfinite(bus.under_voltage_v) || !std::isfinite(bus.under_recover_voltage_v) ||
        bus.over_recover_voltage_v >= bus.over_voltage_v ||
        bus.under_recover_voltage_v <= bus.under_voltage_v) return false;
    temperature_ = temperature;
    bus_ = bus;
    reset();
    return true;
}

void Protection::reset()
{
    board_ = motor_ = over_ = under_ = {};
}

void Protection::Debounce::update(bool trigger, bool recover,
                                  uint32_t trigger_threshold, uint32_t recover_threshold)
{
    if (!trigger_threshold) trigger_threshold = 1U;
    if (!recover_threshold) recover_threshold = 1U;
    if (!active) {
        if (trigger) { if (trigger_count < trigger_threshold) ++trigger_count; }
        else trigger_count = 0U;
        recover_count = 0U;
        if (trigger_count >= trigger_threshold) { active = true; recover_count = 0U; }
    } else {
        if (recover) { if (recover_count < recover_threshold) ++recover_count; }
        else recover_count = 0U;
        trigger_count = 0U;
        if (recover_count >= recover_threshold) { active = false; recover_count = 0U; }
    }
}

void Protection::update_temperature(float board_c, float motor_c, bool valid)
{
    // Invalid samples neither recover nor age a valid protection state.
    if (!valid || !std::isfinite(board_c) || !std::isfinite(motor_c)) return;
    if (!temperature_.enable) { board_ = motor_ = {}; return; }
    board_.update(board_c >= temperature_.board_over_temp_c,
        board_c <= temperature_.board_recover_temp_c,
        temperature_.trigger_count, temperature_.recover_count);
    motor_.update(motor_c >= temperature_.motor_over_temp_c,
        motor_c <= temperature_.motor_recover_temp_c,
        temperature_.trigger_count, temperature_.recover_count);
}

void Protection::update_bus(float calibrated_voltage_v, bool valid)
{
    if (!bus_.enable) { over_ = under_ = {}; return; }
    if (!valid || !std::isfinite(calibrated_voltage_v)) return;
    // Protect on calibrated raw Vbus before display filtering.
    over_.update(calibrated_voltage_v >= bus_.over_voltage_v,
        calibrated_voltage_v <= bus_.over_recover_voltage_v,
        bus_.trigger_count, bus_.recover_count);
    under_.update(calibrated_voltage_v <= bus_.under_voltage_v,
        calibrated_voltage_v >= bus_.under_recover_voltage_v,
        bus_.trigger_count, bus_.recover_count);
}

uint32_t Protection::fault_mask() const
{
    return (board_.active ? 1U << 2U : 0U) | (motor_.active ? 1U << 3U : 0U) |
           (over_.active ? 1U << 4U : 0U) | (under_.active ? 1U << 5U : 0U);
}

bool Protection::temperature(float voltage, const TemperatureLookupPoint* table,
                             size_t count, float& output)
{
    if (!std::isfinite(voltage) || voltage <= 0.0F || voltage >= 3.3F ||
        !table || count < 2U) return false;
    for (size_t i = 0; i < count; ++i) {
        if (!std::isfinite(table[i].temperature_c) ||
            !std::isfinite(table[i].resistance_kohm) || table[i].resistance_kohm <= 0.0F ||
            (i && table[i-1].resistance_kohm <= table[i].resistance_kohm)) return false;
    }
    const float resistance = voltage / (3.3F - voltage) * 10.0F;
    if (resistance >= table[0].resistance_kohm) output = table[0].temperature_c;
    else if (resistance <= table[count-1].resistance_kohm) output = table[count-1].temperature_c;
    else for (size_t i = 0; i + 1 < count; ++i) {
        if (resistance <= table[i].resistance_kohm && resistance >= table[i+1].resistance_kohm) {
            const float ratio = (resistance - table[i].resistance_kohm) /
                (table[i+1].resistance_kohm - table[i].resistance_kohm);
            output = table[i].temperature_c + ratio *
                (table[i+1].temperature_c - table[i].temperature_c);
            break;
        }
    }
    return std::isfinite(output);
}

TemperatureSample Protection::sample_temperatures(const float* voltages, size_t channels,
    const TemperatureLookupPoint* table, size_t count, float motor_open_voltage_v)
{
    TemperatureSample result;
    if (!voltages || channels < 2U || channels > 3U) return result;
    if (!std::isfinite(motor_open_voltage_v) || motor_open_voltage_v <= 0.0F ||
        motor_open_voltage_v >= 3.3F) motor_open_voltage_v = 3.15F;
    result.present_mask = static_cast<uint8_t>((1U << channels) - 1U);
    for (size_t i = 0; i < channels; ++i) result.voltage_v[i] = voltages[i];
    bool all_valid = temperature(voltages[0], table, count, result.board_temperature_c);
    if (all_valid) {
        result.valid_mask = 1U;
        result.temperature_c[0] = result.board_temperature_c;
    }
    bool motor_valid = false;
    for (size_t i = 1; i < channels; ++i) {
        if (voltages[i] >= motor_open_voltage_v) continue;
        float value = 0;
        if (!temperature(voltages[i], table, count, value)) { all_valid = false; continue; }
        result.valid_mask |= static_cast<uint8_t>(1U << i);
        result.temperature_c[i] = value;
        if (!motor_valid || value > result.motor_temperature_c) result.motor_temperature_c = value;
        motor_valid = true;
    }
    result.valid = all_valid && motor_valid;
    return result;
}

}  // namespace algo::motor
