#pragma once

#include <cstddef>
#include <cstdint>

#ifndef CONFIG_LINK_MAX_LINKS
#define CONFIG_LINK_MAX_LINKS 8
#endif

namespace link {

class Link {
public:
    using PollFunc = void (*)(void *arg);

    virtual ~Link() = default;

    // 发送原始字节流（完整帧已打包好）
    virtual int send(const uint8_t *data, size_t len) = 0;

    // 接收原始字节流（非阻塞，返回读取字节数）
    virtual int recv(uint8_t *buf, size_t max_len) = 0;

    // 链路是否就绪
    virtual bool is_ready() const = 0;

    // 周期轮询回调（如 CAN 分片重组），由子类构造时注册
    void set_poll(PollFunc fn, void *arg = nullptr) { poll_fn_ = fn; poll_arg_ = arg; }
    void poll() { if (poll_fn_) poll_fn_(poll_arg_); }

    // link_id（业务层指定的物理端口 ID）
    uint16_t id() const { return id_; }
    void set_id(uint16_t id) { id_ = id; }

protected:
    uint16_t id_ {0};
    PollFunc poll_fn_ {nullptr};
    void *poll_arg_ {nullptr};
};

// ── 链路注册表 ──
// 全局 Link 实例在 main() 前动态初始化，构造函数调用 register_link()
// Router 在 POST_KERNEL SYS_INIT 时从注册表读取

struct LinkRegistry {
    Link *links[CONFIG_LINK_MAX_LINKS] {};
    size_t cnt {0};
};

// 全局注册表实例（非 static 局部，避免初始化顺序问题）
inline LinkRegistry g_link_registry;

// 注册函数（inline，Link 子类构造时调用）
inline bool register_link(Link *link) {
    if (g_link_registry.cnt >= CONFIG_LINK_MAX_LINKS) return false;
    g_link_registry.links[g_link_registry.cnt++] = link;
    return true;
}

} // namespace link
