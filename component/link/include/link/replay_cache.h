#pragma once

#include "frame.h"

#include <cstddef>
#include <cstdint>
#include <cstring>

#ifndef CONFIG_LINK_REPLAY_CACHE_SIZE
#define CONFIG_LINK_REPLAY_CACHE_SIZE 16
#endif
#ifndef CONFIG_LINK_MAX_FRAME_SIZE
#define CONFIG_LINK_MAX_FRAME_SIZE 512
#endif

namespace link {

static_assert(CONFIG_LINK_MAX_FRAME_SIZE > HEADER_SIZE + CRC_SIZE,
              "LINK frame must leave room for a payload");

enum class ReplayDecision : uint8_t {
    NewRequest = 0U,
    Duplicate,
    Conflict,
};

struct ReplayResult {
    ReplayDecision decision {ReplayDecision::NewRequest};
    uint8_t ack_status {0U};
};

/** Fixed-size replay cache. Router's process lock serializes all access. */
class ReplayCache {
public:
    [[nodiscard]] ReplayResult classify(const Frame& frame,
                                        uint32_t now_ms,
                                        uint32_t retention_ms) const
    {
        for (const Entry& entry : entries_) {
            if (!entry.used || (now_ms - entry.seen_ms) >= retention_ms
                || entry.sender != frame.sender_id
                || entry.sequence != frame.seq) {
                continue;
            }
            const bool exact = entry.receiver == frame.receiver_id
                && entry.command_set == frame.cmd_set
                && entry.command_id == frame.cmd_id
                && entry.command_type == normalized_command_type(frame)
                && entry.payload_size == frame.data_len
                && (frame.data_len == 0U
                    || (frame.data != nullptr
                        && std::memcmp(entry.payload, frame.data,
                                       frame.data_len) == 0));
            return {exact ? ReplayDecision::Duplicate
                          : ReplayDecision::Conflict,
                    entry.ack_status};
        }
        return {};
    }

    void remember(const Frame& frame, uint8_t ack_status, uint32_t now_ms)
    {
        constexpr size_t payload_capacity =
            CONFIG_LINK_MAX_FRAME_SIZE - HEADER_SIZE - CRC_SIZE;
        if (frame.data_len > payload_capacity
            || (frame.data_len > 0U && frame.data == nullptr)) {
            return;
        }
        Entry& entry = entries_[next_];
        entry.sender = frame.sender_id;
        entry.receiver = frame.receiver_id;
        entry.sequence = frame.seq;
        entry.command_set = frame.cmd_set;
        entry.command_id = frame.cmd_id;
        entry.command_type = normalized_command_type(frame);
        entry.payload_size = frame.data_len;
        if (frame.data_len > 0U && frame.data != nullptr) {
            std::memcpy(entry.payload, frame.data, frame.data_len);
        }
        entry.ack_status = ack_status;
        entry.seen_ms = now_ms;
        entry.used = true;
        next_ = (next_ + 1U) % CONFIG_LINK_REPLAY_CACHE_SIZE;
    }

    void clear()
    {
        for (Entry& entry : entries_) {
            entry = {};
        }
        next_ = 0U;
    }

private:
    struct Entry {
        uint8_t sender {};
        uint8_t receiver {};
        uint16_t sequence {};
        uint8_t command_set {};
        uint8_t command_id {};
        uint8_t command_type {};
        uint16_t payload_size {};
        uint8_t payload[CONFIG_LINK_MAX_FRAME_SIZE
                        - HEADER_SIZE - CRC_SIZE] {};
        uint8_t ack_status {};
        uint32_t seen_ms {};
        bool used {false};
    };

    static_assert(CONFIG_LINK_REPLAY_CACHE_SIZE > 0,
                  "LINK replay cache must contain at least one entry");
    [[nodiscard]] static uint8_t normalized_command_type(const Frame& frame)
    {
        return static_cast<uint8_t>(
            frame.cmd_type
            & ~static_cast<uint8_t>(
                CMD_TYPE_RETRANSMIT_MASK << CMD_TYPE_RETRANSMIT_SHIFT));
    }

    Entry entries_[CONFIG_LINK_REPLAY_CACHE_SIZE] {};
    size_t next_ {0U};
};

} // namespace link
