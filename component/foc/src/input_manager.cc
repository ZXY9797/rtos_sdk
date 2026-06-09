#include <foc/input_manager.h>

namespace foc {

void InputManager::collect() {
    // 输入采集逻辑 — 由具体实现填充
    // 例如: ADC 读取油门、GPIO 读取按键
}

void InputManager::clear_requests() {
    enable_req_ = false;
    disable_req_ = false;
}

} // namespace foc
