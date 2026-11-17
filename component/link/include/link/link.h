#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>

#ifndef CONFIG_LINK_MAX_LINKS
#define CONFIG_LINK_MAX_LINKS 8
#endif

namespace link {

/** Physical transport boundary used by Router. */
class Link {
public:
    using PollFunc = void (*)(void* arg);

    virtual ~Link() = default;

    /**
     * Sends one complete encoded frame.
     *
     * The implementation must consume or copy data before returning. It must
     * also have a bounded completion time so a failed transport cannot hang
     * the Router task.
     */
    virtual int send(const uint8_t* data, size_t len) = 0;

    /** Non-blocking raw-byte receive; returns the number of bytes consumed. */
    virtual int recv(uint8_t* buffer, size_t capacity) = 0;

    /** Returns true while the transport is able to send and receive. */
    virtual bool is_ready() const = 0;

    // Called by the serialized Router owner during shutdown/reinitialization.
    virtual void reset_session() {}

    /** Registers init-time transport maintenance such as CAN reassembly. */
    void set_poll(PollFunc function, void* arg = nullptr)
    {
        poll_fn_ = function;
        poll_arg_ = arg;
    }

    void poll()
    {
        if (poll_fn_ != nullptr) {
            poll_fn_(poll_arg_);
        }
    }

    /**
     * Divides CONFIG_LINK_PROCESS_HZ for this physical link.
     *
     * One drains the link every Router cycle; larger values trade latency for
     * fewer wakeups and larger batches. Event-only mode is intentionally not
     * represented until Link exposes an ISR-safe Router wakeup contract.
     */
    [[nodiscard]] bool set_process_divider(uint8_t divider)
    {
        if (divider == 0U) {
            return false;
        }
        process_divider_.store(divider, std::memory_order_release);
        return true;
    }

    [[nodiscard]] uint8_t process_divider() const
    {
        return process_divider_.load(std::memory_order_acquire);
    }

    [[nodiscard]] uint16_t id() const
    {
        return id_.load(std::memory_order_acquire);
    }

    void set_id(uint16_t id)
    {
        id_.store(id, std::memory_order_release);
    }

protected:
    std::atomic<uint16_t> id_ {0U};
    PollFunc poll_fn_ {nullptr};
    void* poll_arg_ {nullptr};
    std::atomic<uint8_t> process_divider_ {1U};
};

/** Fixed registry populated by Link instances before Router initialization. */
struct LinkRegistry {
    Link* links[CONFIG_LINK_MAX_LINKS] {};
    size_t cnt {0U};
};

inline LinkRegistry g_link_registry;

/** Registers one unique, caller-owned Link instance during system init. */
inline bool register_link(Link* physical_link)
{
    if (physical_link == nullptr
        || g_link_registry.cnt >= CONFIG_LINK_MAX_LINKS) {
        return false;
    }
    for (size_t index = 0U; index < g_link_registry.cnt; ++index) {
        if (g_link_registry.links[index] == physical_link) {
            return false;
        }
    }
    g_link_registry.links[g_link_registry.cnt++] = physical_link;
    return true;
}

} // namespace link
