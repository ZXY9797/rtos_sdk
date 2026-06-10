#include "link/router.h"
#include "link/link.h"
#include <init.h>
#include <osal.h>
#include <cstring>

namespace link {

// ── .link_handler section 边界符号 ──

extern "C" {
    extern const Handler __link_handler_start;
    extern const Handler __link_handler_end;
}

// ── 单例 ──

Router &Router::instance() {
    static Router r;
    return r;
}

// ── SYS_INIT 初始化 ──

void Router::init() {
    // 从全局注册表拷贝已注册的链路
    link_cnt_ = g_link_registry.cnt;
    for (size_t i = 0; i < link_cnt_; i++)
        links_[i] = g_link_registry.links[i];

    // 加载 .link_handler section
    handlers_ = &__link_handler_start;
    handler_cnt_ = static_cast<size_t>(&__link_handler_end - &__link_handler_start);
}

// ── 路由表 ──

void Router::set_routes(const RouteEntry *entries, size_t count) {
    routes_ = entries;
    route_cnt_ = count;
}

// ── 链路查找 ──

Link *Router::find_link(uint16_t link_id) {
    for (size_t i = 0; i < link_cnt_; i++) {
        if (links_[i] && links_[i]->id() == link_id)
            return links_[i];
    }
    return nullptr;
}

// ── 路由查找 ──

Link *Router::route_lookup(uint8_t receiver, uint16_t in_link_id) {
    for (size_t i = 0; i < route_cnt_; i++) {
        auto &r = routes_[i];
        if (r.mode == RouteMode::Direct) {
            if (r.match_addr == 0 || r.match_addr == in_link_id)
                return select_out_link(r);
        } else {
            if ((receiver & r.mask) == (r.match_addr & r.mask))
                return select_out_link(r);
        }
    }
    return nullptr;
}

// ── 多路由 round-robin ──

Link *Router::select_out_link(const RouteEntry &entry) {
    static uint8_t rr_idx = 0;
    for (uint8_t i = 0; i < entry.out_cnt; i++) {
        uint16_t lid = entry.out_links[(rr_idx + i) % entry.out_cnt];
        Link *l = find_link(lid);
        if (l && l->is_ready()) {
            rr_idx = (rr_idx + i + 1) % entry.out_cnt;
            return l;
        }
    }
    rr_idx++;
    return (entry.out_cnt > 0) ? find_link(entry.out_links[0]) : nullptr;
}

// ── 主循环 ──

void Router::process() {
    uint8_t byte;

    for (size_t i = 0; i < link_cnt_; i++) {
        if (!links_[i] || !links_[i]->is_ready()) continue;

        while (links_[i]->recv(&byte, 1) == 1) {
            int ret = codecs_[i].feed(byte);
            if (ret > 0) {
                const auto &f = codecs_[i].frame();

                // 本机帧检查
                bool for_me = (f.receiver_id == self_addr_)
                           || (f.receiver_id == ADDR_BROADCAST);

                if (!for_me) {
                    // 路由转发
                    Link *out = route_lookup(f.receiver_id, links_[i]->id());
                    if (out) {
                        out->send(codecs_[i].raw(), codecs_[i].raw_len());
                    }
                } else if (f.is_ack()) {
                    // ACK 帧处理
                    handle_ack(f);
                } else {
                    // 普通帧 → 回调分发
                    dispatch(links_[i], f);
                }
            } else if (ret < 0) {
                // 解包错误，状态机已 reset
            }
        }
    }

    // 超时检查
    check_timeout();
}

// ── 帧分发 ──

void Router::dispatch(Link *src, const Frame &frame) {
    for (size_t i = 0; i < handler_cnt_; i++) {
        auto &h = handlers_[i];
        if (h.cmd_set == frame.cmd_set && h.cmd_id == frame.cmd_id) {
            if (h.cb) h.cb(frame, h.arg);
            return;
        }
    }
}

// ── 发送 ──

int Router::send(uint8_t receiver, uint8_t cmd_set, uint8_t cmd_id,
                 const uint8_t *data, uint8_t data_len,
                 uint8_t ack_mode, uint8_t enc_mode,
                 uint8_t priority) {
    // 路由查找
    Link *out = route_lookup(receiver, 0);
    if (!out) return -1;

    PackArgs args {};
    args.sender = self_addr_;
    args.receiver = receiver;
    args.cmd_set = cmd_set;
    args.cmd_id = cmd_id;
    args.ack_mode = static_cast<AckMode>(ack_mode);
    args.enc_mode = static_cast<EncMode>(enc_mode);
    args.priority = static_cast<Priority>(priority);
    args.seq = tx_seq_++;

    uint8_t frame_buf[CONFIG_LINK_MAX_FRAME_SIZE];
    size_t frame_len = FrameCodec::pack(frame_buf, sizeof(frame_buf),
                                        args, data, data_len);
    if (frame_len == 0) return -2;

    int sent = out->send(frame_buf, frame_len);

    // 需要 ACK 时入待确认队列
    if (ack_mode != 0 && sent > 0) {
        // 找到目标链路索引
        uint8_t link_idx = 0;
        for (size_t i = 0; i < link_cnt_; i++) {
            if (links_[i] == out) { link_idx = static_cast<uint8_t>(i); break; }
        }
        add_pending(args.seq, ack_mode, link_idx, frame_buf, static_cast<uint16_t>(frame_len));
    }

    return sent;
}

// ── ACK 处理 ──

void Router::handle_ack(const Frame &frame) {
    remove_pending(frame.seq);
}

void Router::send_ack(Link *out, const Frame &req, uint8_t status,
                      const uint8_t *data, uint8_t data_len) {
    PackArgs args {};
    args.sender = self_addr_;
    args.receiver = req.sender_id;
    args.cmd_set = req.cmd_set;
    args.cmd_id = req.cmd_id;
    args.ack_mode = AckMode::No;
    args.is_ack = true;
    args.seq = req.seq;

    // 构建 ACK data: [status | original data...]
    uint8_t ack_data[CONFIG_LINK_MAX_FRAME_SIZE - HEADER_SIZE - CRC_SIZE];
    size_t ack_data_len = 1 + data_len;
    if (ack_data_len > sizeof(ack_data)) ack_data_len = sizeof(ack_data);
    ack_data[0] = status;
    if (data_len > 0 && data)
        memcpy(ack_data + 1, data, data_len);

    uint8_t frame_buf[CONFIG_LINK_MAX_FRAME_SIZE];
    size_t frame_len = FrameCodec::pack(frame_buf, sizeof(frame_buf),
                                        args, ack_data, static_cast<uint8_t>(ack_data_len));
    if (frame_len > 0)
        out->send(frame_buf, frame_len);
}

// ── TxPending 管理 ──

int Router::add_pending(uint16_t seq, uint8_t ack_mode, uint8_t link_idx,
                        const uint8_t *raw, uint16_t raw_len) {
    for (int i = 0; i < CONFIG_LINK_MAX_PENDING; i++) {
        if (!pending_[i].used) {
            pending_[i].seq = seq;
            pending_[i].ack_mode = ack_mode;
            pending_[i].send_tick = get_tick_ms();
            pending_[i].retry_cnt = 0;
            pending_[i].out_link_idx = link_idx;
            pending_[i].raw_len = raw_len;
            if (raw_len <= CONFIG_LINK_MAX_FRAME_SIZE)
                memcpy(pending_[i].raw_frame, raw, raw_len);
            pending_[i].used = true;
            return i;
        }
    }
    return -1;
}

void Router::remove_pending(uint16_t seq) {
    for (int i = 0; i < CONFIG_LINK_MAX_PENDING; i++) {
        if (pending_[i].used && pending_[i].seq == seq) {
            pending_[i].used = false;
            return;
        }
    }
}

// ── 超时检查 ──

void Router::check_timeout() {
    uint32_t now = get_tick_ms();

    for (int i = 0; i < CONFIG_LINK_MAX_PENDING; i++) {
        if (!pending_[i].used) continue;

        uint32_t timeout_ms;
        uint8_t max_retry;

        switch (static_cast<AckMode>(pending_[i].ack_mode)) {
        case AckMode::Now:      timeout_ms = 10;    max_retry = 3; break;
        case AckMode::Finish:   timeout_ms = 500;   max_retry = 2; break;
        case AckMode::Progress: timeout_ms = 2000;  max_retry = 1; break;
        default: pending_[i].used = false; continue;
        }

        if (now - pending_[i].send_tick < timeout_ms) continue;

        if (pending_[i].retry_cnt >= max_retry) {
            // 超时，移除待确认条目
            pending_[i].used = false;
            continue;
        }

        // 重传：设置 retransmit 位
        if (pending_[i].raw_len > HEADER_SIZE) {
            pending_[i].raw_frame[4] |= (1 << CMD_TYPE_RETRANSMIT_SHIFT);
        }

        Link *out = links_[pending_[i].out_link_idx];
        if (out && out->is_ready()) {
            out->send(pending_[i].raw_frame, pending_[i].raw_len);
        }

        pending_[i].retry_cnt++;
        pending_[i].send_tick = now;
    }
}

// ── 系统 tick ──

uint32_t Router::get_tick_ms() {
    return osal::Kernel::uptime_ms();
}

} // namespace link

// ── Link 处理任务 ──

static void link_process_entry(void *, const osal::PeriodicStats &) {
    auto &router = link::Router::instance();
    // 轮询所有链路（如 CAN 分片重组）
    for (size_t i = 0; i < router.link_count(); i++) {
        auto *l = router.find_link_by_index(i);
        if (l) l->poll();
    }
    router.process();
}

// ── SYS_INIT 自动初始化 ──

static int link_sys_init(void) {
    link::Router::instance().init();

    auto *thread = osal::PeriodicThread::create(
        "link", link_process_entry, nullptr,
        CONFIG_LINK_PROCESS_STACK, CONFIG_LINK_PROCESS_PRIO,
        CONFIG_LINK_PROCESS_HZ, osal::PeriodicTrigger::Tick);
    if (thread) (void)thread->startup();

    return 0;
}
SYS_INIT(link_sys_init, INITCALL_LEVEL_POST_KERNEL, 20);
