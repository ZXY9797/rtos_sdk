#pragma once

#include <cstdint>

namespace app {

struct JointFeedbackSample {
    float position_rad {};
    float velocity_rad_s {};
    float current_a {};
    float bus_voltage_v {};
    uint32_t sample_us {};
    uint32_t sequence {};
    uint32_t faults {};
    bool valid {};
    bool output_enabled {};
};

} // namespace app
