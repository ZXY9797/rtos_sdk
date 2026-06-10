#pragma once

#include "frame.h"
#include "codec.h"
#include "link.h"
#include <cstddef>
#include <cstdint>

#ifndef CONFIG_LINK_MAX_LINKS
#define CONFIG_LINK_MAX_LINKS 8
#endif

#ifndef CONFIG_LINK_MAX_ROUTES
#define CONFIG_LINK_MAX_ROUTES 16
#endif

#ifndef CONFIG_LINK_MAX_PENDING
#define CONFIG_LINK_MAX_PENDING 8
#endif

namespace link {

// ── 路由模式 ──

enum class RouteMode : uint8_t {
    ByHost,  // 匹配 receiver 的 host_id → 转发到 link_id
    Direct,  // 不看 receiver，直接转发到 link_id（交换机/桥接模式）
};

// ── 单条路由规则 ──

struct RouteEntry {
    RouteMode mode;
    uint8_t  match_addr;   // ByHost: host_id; Direct: 来源 link_id (0=任意)
    uint8_t  mask;         // 0xFF 精确, 0xF0 同类型广播, 0x00 全广播
    uint8_t  out_cnt;      // 输出链路数量 (1~4)
    uint16_t out_links[4]; // 输出链路 ID 列表（多路由 round-robin）
};

// ── 流式路由构建器 ──

struct RouteBuilder {
    RouteMode mode;
    uint8_t  match_addr;
    uint8_t  mask;
    uint16_t out_links[4];
    uint8_t  out_cnt {0};

    RouteBuilder &to(uint16_t link_id) {
        if (out_cnt < 4) out_links[out_cnt++] = link_id;
        return *this;
    }
};

inline RouteBuilder route_by_host(uint8_t host_id, uint8_t mask = 0xF0) {
    return { RouteMode::ByHost, host_id, mask, {}, 0 };
}

inline RouteBuilder route_by_host_idx(uint8_t addr, uint8_t mask = 0xFF) {
    return { RouteMode::ByHost, addr, mask, {}, 0 };
}

inline RouteBuilder route_direct(uint16_t from_link_id = 0) {
    return { RouteMode::Direct, static_cast<uint8_t>(from_link_id), 0xFF, {}, 0 };
}

inline RouteEntry make_route(const RouteBuilder &b) {
    RouteEntry e {};
    e.mode = b.mode;
    e.match_addr = b.match_addr;
    e.mask = b.mask;
    e.out_cnt = b.out_cnt;
    for (uint8_t i = 0; i < b.out_cnt; i++)
        e.out_links[i] = b.out_links[i];
    return e;
}

// ── TxPending: 待确认帧 ──

struct TxPending {
    uint16_t seq;
    uint8_t  ack_mode;
    uint32_t send_tick;      // 首次发送时刻 (ms)
    uint8_t  retry_cnt;      // 已重试次数
    uint8_t  out_link_idx;   // 目标链路索引
    uint8_t  raw_frame[CONFIG_LINK_MAX_FRAME_SIZE];
    uint16_t raw_len;
    bool     used {false};
};

// ── Router 类 ──

class Router {
public:
    using TimeoutCallback = void (*)(uint16_t seq, uint8_t receiver,
                                     uint8_t cmd_set, uint8_t cmd_id, void *arg);

    static Router &instance();

    // 设置路由表（业务层调用）
    void set_routes(const RouteEntry *entries, size_t count);

    // 设置本机地址
    void set_self_addr(uint8_t addr) { self_addr_ = addr; }

    // 设置超时回调（可选）
    void set_on_timeout(TimeoutCallback cb, void *arg = nullptr) { on_timeout_ = cb; on_timeout_arg_ = arg; }

    // 周期处理：接收 + 解包 + 路由 + ACK + 超时检查
    void process();

    // 发送帧
    int send(uint8_t receiver, uint8_t cmd_set, uint8_t cmd_id,
             const uint8_t *data, uint8_t data_len,
             uint8_t ack_mode = 0, uint8_t enc_mode = 0,
             uint8_t priority = 0);

    // 查找链路
    Link *find_link(uint16_t link_id);
    Link *find_link_by_index(size_t idx) { return (idx < link_cnt_) ? links_[idx] : nullptr; }

    // 链路数
    size_t link_count() const { return link_cnt_; }

    // 初始化（SYS_INIT 调用）
    void init();

private:
    Router() = default;

    // 链路
    Link *links_[CONFIG_LINK_MAX_LINKS] {};
    size_t link_cnt_ {0};
    FrameCodec codecs_[CONFIG_LINK_MAX_LINKS] {};

    // 路由表
    const RouteEntry *routes_ {nullptr};
    size_t route_cnt_ {0};

    // 本机
    uint8_t self_addr_ {0};
    uint16_t tx_seq_ {0};

    // 回调表（.link_handler section）
    const Handler *handlers_ {nullptr};
    size_t handler_cnt_ {0};

    // 超时回调
    TimeoutCallback on_timeout_ {nullptr};
    void *on_timeout_arg_ {nullptr};

    // 待确认队列
    TxPending pending_[CONFIG_LINK_MAX_PENDING] {};

    // 路由查找
    Link *route_lookup(uint8_t receiver, uint16_t in_link_id);
    Link *select_out_link(const RouteEntry &entry);

    // 帧分发
    void dispatch(Link *src, const Frame &frame);

    // ACK 处理
    void handle_ack(const Frame &frame);
    void send_ack(Link *out, const Frame &req, uint8_t status, const uint8_t *data, uint8_t data_len);

    // 超时检查
    void check_timeout();

    // TxPending 操作
    int add_pending(uint16_t seq, uint8_t ack_mode, uint8_t link_idx,
                    const uint8_t *raw, uint16_t raw_len);
    void remove_pending(uint16_t seq);

    // 获取当前 tick (ms)
    static uint32_t get_tick_ms();
};

} // namespace link
