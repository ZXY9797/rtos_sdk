#pragma once

#include <cstdint>

namespace hal {

/**
 * @brief 设备状态枚举
 */
enum class DeviceState : uint8_t {
	Created,      ///< 已创建，未初始化
	Initialized,  ///< 已初始化
	Open,         ///< 已打开
	Error,        ///< 错误状态
};

/**
 * @brief 设备基类 — 提供统一的状态管理和调试接口
 *
 * 可选继承，不影响现有 DeviceTrait 机制。
 * 驱动类可选择继承此基类以获得状态管理功能。
 *
 * 用法:
 *   class MyDriver : public hal::DeviceBase {
 *   public:
 *       int init(const Config &cfg) {
 *           // ... 初始化硬件 ...
 *           set_state(hal::DeviceState::Initialized);
 *           return 0;
 *       }
 *   };
 */
class DeviceBase {
public:
	constexpr DeviceBase() = default;

	/// 获取设备名称（从 DTS alias 注入）
	const char *name() const { return name_; }

	/// 获取设备状态
	DeviceState state() const { return state_; }

	/// 检查是否已初始化
	bool is_initialized() const {
		return state_ >= DeviceState::Initialized;
	}

	/// 检查是否已打开
	bool is_open() const {
		return state_ >= DeviceState::Open;
	}

	/// 检查设备是否就绪（已初始化即就绪）
	bool is_ready() const { return is_initialized(); }

protected:
	/// 设置设备名称（由 init 函数调用）
	void set_name(const char *name) { name_ = name; }

	/// 设置设备状态
	void set_state(DeviceState state) { state_ = state; }

private:
	const char *name_ = nullptr;
	DeviceState state_ = DeviceState::Created;
};

} // namespace hal
