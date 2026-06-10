#pragma once

#include <ble/ble_stack.h>
#include <cstdint>

namespace ble {

/// BleDevice — 设备树驱动包装器
///
/// 在 initcall 中存储 DTS 配置，应用层调用 init() 完成协议栈初始化。
/// 与 MotorDevice 不同，BLE 需要应用层事件回调，因此不能完全自动初始化。
///
/// 用法:
///   auto &ble = device_get(ble0);
///   ble.init(on_ble_event, nullptr);
///   ble.stack().adv_start();
class BleDevice {
public:
    BleDevice() = default;

    /// 由 gen_device_traits.py 生成的 initcall 调用
    /// 存储 DTS 配置，不启动协议栈
    int init(const StackConfig &cfg) {
        cfg_ = cfg;
        return 0;
    }

    /// 应用层调用：启动 BLE 协议栈
    int init(EventCallback cb, void *user_data = nullptr) {
        return static_cast<int>(stack_.init(cfg_, cb, user_data));
    }

    /// 获取 BleStack 引用（init 后可用）
    BleStack &stack() { return stack_; }
    const BleStack &stack() const { return stack_; }

    /// 获取存储的配置
    const StackConfig &config() const { return cfg_; }

private:
    StackConfig cfg_{};
    BleStack stack_;
};

} // namespace ble
