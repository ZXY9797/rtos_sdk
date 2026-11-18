#include <foc/data_logger.h>

namespace foc {

void DataLogger::log(float id, float iq, float speed, float vbus, float temp) {
    Frame &f = buffer_[write_idx_];
    f.id = id;
    f.iq = iq;
    f.speed = speed;
    f.vbus = vbus;
    f.temp = temp;
    f.timestamp = 0; // 由调用者填充 tick

    write_idx_ = (write_idx_ + 1) % kBufferSize;
}

} // namespace foc
