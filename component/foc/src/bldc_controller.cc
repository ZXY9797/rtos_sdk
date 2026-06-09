#include <foc/bldc_controller.h>

namespace foc {

// Hall 状态 → 桥臂驱动表 (U+, U-, V+, V-, W+, W-)
// 每行: {U_duty, V_duty, W_duty} 其中 duty = 0/period/高阻
static void hall_to_phase(uint8_t hall, bool fwd, uint32_t duty,
                          uint32_t &du, uint32_t &dv, uint32_t &dw) {
    du = dv = dw = 0;
    if (hall == 0 || hall == 7) return;

    struct CommEntry { int u, v, w; };
    // 正转换表: Hall: 1→3→2→6→4→5
    static const CommEntry table_fwd[8] = {
        {0, 0, 0},     // 0: invalid
        {1, -1, 0},    // 1: U+, W-
        {-1, 0, 1},    // 2: V+, U-
        {1, 0, -1},    // 3: U+, V-
        {0, 1, -1},    // 4: W+, V-
        {-1, 1, 0},    // 5: V+, W-
        {0, -1, 1},    // 6: W+, U-
        {0, 0, 0},     // 7: invalid
    };
    // 反转换表: 交换 U/V 相
    static const CommEntry table_rev[8] = {
        {0, 0, 0},
        {0, -1, 1},    // 1: W+, U-
        {1, 0, -1},    // 2: U+, V-
        {-1, 1, 0},    // 3: V+, W-
        {-1, 0, 1},    // 4: V+, U-
        {0, 1, -1},    // 5: W+, V-
        {1, -1, 0},    // 6: U+, W-
        {0, 0, 0},
    };

    const CommEntry *table = fwd ? table_fwd : table_rev;
    auto set_phase = [&](int val) -> uint32_t {
        return (val > 0) ? duty : 0;
    };

    du = set_phase(table[hall].u);
    dv = set_phase(table[hall].v);
    dw = set_phase(table[hall].w);
}

void BLDCController::commutate(uint8_t hall_state, uint32_t duty, uint32_t /*period*/,
                                uint32_t &duty_u, uint32_t &duty_v, uint32_t &duty_w) {
    if (!enabled_) {
        duty_u = duty_v = duty_w = 0;
        return;
    }
    hall_to_phase(hall_state, forward_, duty, duty_u, duty_v, duty_w);
}

} // namespace foc
