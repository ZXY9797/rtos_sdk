#pragma once

#include <devicetree.h>
#include <device_base.h>
#include <cstddef>
#include <type_traits>

#ifdef __cplusplus
namespace hal {

/**
 * @brief 设备驱动特征 — 主模板
 *
 * 每个设备树节点对应的驱动特化由 gen_device_traits.py 自动生成。
 * 主模板提供 static_assert，使用未注册的 ordinal 会给出清晰的错误信息。
 */
template <int Ord>
struct DeviceTrait {
#ifdef __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wtautological-compare"
#endif
    static_assert(Ord != Ord,  // 依赖模板参数，仅实例化时触发
        "DeviceTrait<Ord> not specialized. "
        "Check that the device is enabled in DTS (status = \"okay\") "
        "and the binding YAML has a cxx-driver section.");
#ifdef __GNUC__
#pragma GCC diagnostic pop
#endif
};

/**
 * @brief 统一设备模板 — 编译期自动分发
 *
 * 通过 ordinal 查找对应的驱动类型。
 * 零运行时开销，完全编译期解析。
 *
 * 用法:
 *   using Led   = Device<DT_ORD(DT_ALIAS(led0))>;
 *   using Motor = Device<DT_ORD(DT_ALIAS(motor0))>;
 */
template <int Ord>
using Device = typename DeviceTrait<Ord>::type;

/// 编译期设备获取 — 前向声明（定义在 drivers_generated.h 中）
template <int Ord>
inline auto &device_get();

/**
 * @brief 设备信息结构 — 用于运行时枚举
 */
struct DeviceInfo {
    int ord;                ///< 设备 ordinal
    const char *alias;      ///< 设备别名（如 "uart0"）
    const char *type_name;  ///< 驱动类型名（如 "Uart"）
    void *instance;         ///< 设备实例指针（DeviceBase* 如果继承了）
    bool (*is_ready)(void *inst);  ///< 类型擦除的就绪检查（可为 nullptr）
};

/**
 * @brief 获取设备注册表
 *
 * 返回编译期生成的设备信息数组，用于运行时枚举和调试。
 *
 * 用法:
 *   size_t count;
 *   const auto *registry = hal::get_device_registry(&count);
 *   for (size_t i = 0; i < count; i++) {
 *       printf("Device: %s (ord=%d, type=%s)\n",
 *              registry[i].alias, registry[i].ord, registry[i].type_name);
 *   }
 */
const DeviceInfo *get_device_registry(size_t *count);

/// 检查设备是否就绪（编译期分派，零开销）
template <int Ord>
inline bool device_is_ready() {
    auto &dev = device_get<Ord>();
    if constexpr (std::is_base_of_v<DeviceBase,
                     std::remove_reference_t<decltype(dev)>>) {
        return dev.is_ready();
    }
    return true;
}

} // namespace hal

/// 宏: device_is_ready(uart0) → hal::device_is_ready<ORD>()
#define device_is_ready(alias) \
    hal::device_is_ready<DT_ORD(DT_ALIAS(alias))>()

#endif

/**
 * @brief 从设备树节点 ID 获取 ordinal（数值）
 *
 * DT_ORD(node_id) 展开为 node_id_ORD，是一个编译期整数，
 * 可以安全用作模板参数。
 *
 * 需要间接宏展开：## 运算符会阻止参数展开，
 * _DT_ORD_EXPAND 确保 DT_ALIAS 等宏先展开再拼接。
 */
#define DT_ORD(node_id) _DT_ORD_EXPAND(node_id)
#define _DT_ORD_EXPAND(node_id) (node_id##_ORD)
