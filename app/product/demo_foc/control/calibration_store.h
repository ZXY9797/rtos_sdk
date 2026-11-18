#pragma once
#include "control/motor_control.h"

namespace app {
class CalibrationStore {
public:
    bool mount();
    void load(CalibrationValues& values);
    bool read(CalibrationRecordId id, void* payload, size_t size);
    bool write(const RecordView& record);
    bool erase(CalibrationRecordId id);
    bool write_interaction(const uint8_t* bytes,size_t size);
    int read_interaction(uint8_t* bytes,size_t capacity);
private:
    uint32_t sequence_ {0U};
    uint8_t record_[algo::motor::CalibrationRecord::header_size +
                    algo::motor::CalibrationRecord::maximum_payload_size] {};
};
} // namespace app
