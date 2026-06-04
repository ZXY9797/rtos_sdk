#pragma once

#include <devicetree.h>

#ifdef __cplusplus
namespace hal {

/**
 * @brief 设备驱动特征 — 主模板
 *
 * 每个设备树节点对应的驱动特化由 gen_device_traits.py 自动生成。
 * 主模板未定义 type，使用未注册的 ordinal 会导致编译错误。
 */
template <int Ord>
struct DeviceTrait;

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

} // namespace hal
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
