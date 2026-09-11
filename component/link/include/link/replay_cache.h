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
#ifndef CONFIG_LINK_REPLAY_RESPONSE_SIZE
#define CONFIG_LINK_REPLAY_RESPONSE_SIZE 320
#endif

namespace link {

static_assert(CONFIG_LINK_MAX_FRAME_SIZE > HEADER_SIZE + CRC_SIZE,
              "LINK frame must leave room for a payload");
inline constexpr size_t LINK_MAX_RESPONSE_SIZE =
    CONFIG_LINK_MAX_FRAME_SIZE - HEADER_SIZE - CRC_SIZE - 1U;
inline constexpr size_t LINK_REPLAY_RESPONSE_CAPACITY =
    CONFIG_LINK_REPLAY_RESPONSE_SIZE < LINK_MAX_RESPONSE_SIZE
        ? CONFIG_LINK_REPLAY_RESPONSE_SIZE : LINK_MAX_RESPONSE_SIZE;

enum class ReplayDecision : uint8_t {
    NewRequest = 0U,
    Duplicate,
    Conflict,
};

struct ReplayResult {
    ReplayDecision decision {ReplayDecision::NewRequest};
    bool response_available {false};
    uint8_t ack_status {0U};
    uint16_t response_size {0U};
};

/** Fixed-size replay cache. The caller serializes all access. */
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
            return {
                exact ? ReplayDecision::Duplicate
                      : ReplayDecision::Conflict,
                exact && entry.response_available,
                entry.ack_status,
                entry.response_size,
            };
        }
        return {};
    }

    [[nodiscard]] bool remember(const Frame& frame,
                                bool response_available,
                                uint8_t ack_status,
                                const uint8_t* response_data,
                                size_t response_size,
                                uint32_t now_ms,
                                uint32_t retention_ms)
    {
        constexpr size_t payload_capacity =
            CONFIG_LINK_MAX_FRAME_SIZE - HEADER_SIZE - CRC_SIZE;
        if (frame.data_len > payload_capacity
            || (frame.data_len > 0U && frame.data == nullptr)
            || response_size > LINK_REPLAY_RESPONSE_CAPACITY
            || (response_size > 0U && response_data == nullptr)
            || retention_ms == 0U) {
            return false;
        }
        // A sender/sequence pair is the replay identity during the retention
        // window. Reusing it after expiry replaces the stale record so a
        // later asynchronous completion cannot update an older entry.
        Entry* entry = find_sequence_entry(frame.sender_id, frame.seq);
        if (entry == nullptr) {
            for (size_t offset = 0U;
                 offset < CONFIG_LINK_REPLAY_CACHE_SIZE; ++offset) {
                const size_t index =
                    (next_ + offset) % CONFIG_LINK_REPLAY_CACHE_SIZE;
                if (!entries_[index].used
                    || (now_ms - entries_[index].seen_ms)
                        >= retention_ms) {
                    entry = &entries_[index];
                    next_ = (index + 1U) % CONFIG_LINK_REPLAY_CACHE_SIZE;
                    break;
                }
            }
        }
        if (entry == nullptr) {
            return false;
        }
        entry->sender = frame.sender_id;
        entry->receiver = frame.receiver_id;
        entry->sequence = frame.seq;
        entry->command_set = frame.cmd_set;
        entry->command_id = frame.cmd_id;
        entry->command_type = normalized_command_type(frame);
        entry->payload_size = frame.data_len;
        if (frame.data_len > 0U && frame.data != nullptr) {
            std::memcpy(entry->payload, frame.data, frame.data_len);
        }
        entry->response_available = response_available;
        entry->ack_status = ack_status;
        entry->response_size = static_cast<uint16_t>(response_size);
        if (response_size > 0U) {
            std::memcpy(entry->response, response_data, response_size);
        }
        entry->seen_ms = now_ms;
        entry->used = true;
        return true;
    }

    [[nodiscard]] bool update_response(const CompletionToken& token,
                                       uint8_t ack_status,
                                       const uint8_t* response_data,
                                       size_t response_size,
                                       uint32_t now_ms)
    {
        if (response_size > LINK_REPLAY_RESPONSE_CAPACITY
            || (response_size > 0U && response_data == nullptr)) {
            return false;
        }
        for (Entry& entry : entries_) {
            if (entry.used && entry.sender == token.sender
                && entry.receiver == token.receiver
                && entry.sequence == token.sequence
                && entry.command_set == token.command_set
                && entry.command_id == token.command_id
                && entry.command_type == pack_cmd_type(
                    false, token.request_ack_mode, token.encryption_mode,
                    token.priority, false)) {
                entry.response_available = true;
                entry.ack_status = ack_status;
                entry.response_size = static_cast<uint16_t>(response_size);
                if (response_size > 0U) {
                    std::memcpy(entry.response, response_data, response_size);
                }
                entry.seen_ms = now_ms;
                return true;
            }
        }
        return false;
    }

    [[nodiscard]] bool copy_response(const Frame& frame,
                                     uint8_t* destination,
                                     size_t destination_capacity,
                                     size_t& response_size) const
    {
        const Entry* entry = find_exact_entry(frame);
        if (entry == nullptr || !entry->response_available
            || entry->response_size > destination_capacity
            || (entry->response_size > 0U && destination == nullptr)) {
            return false;
        }
        response_size = entry->response_size;
        if (entry->response_size > 0U) {
            std::memcpy(destination, entry->response, entry->response_size);
        }
        return true;
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
        uint16_t response_size {};
        uint8_t response[LINK_REPLAY_RESPONSE_CAPACITY] {};
        uint32_t seen_ms {};
        bool response_available {false};
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

    [[nodiscard]] Entry* find_exact_entry(const Frame& frame)
    {
        for (Entry& entry : entries_) {
            if (entry.used && entry.sender == frame.sender_id
                && entry.receiver == frame.receiver_id
                && entry.sequence == frame.seq
                && entry.command_set == frame.cmd_set
                && entry.command_id == frame.cmd_id
                && entry.command_type == normalized_command_type(frame)
                && entry.payload_size == frame.data_len
                && (frame.data_len == 0U
                    || (frame.data != nullptr
                        && std::memcmp(entry.payload, frame.data,
                                       frame.data_len) == 0))) {
                return &entry;
            }
        }
        return nullptr;
    }

    [[nodiscard]] Entry* find_sequence_entry(uint8_t sender,
                                             uint16_t sequence)
    {
        for (Entry& entry : entries_) {
            if (entry.used && entry.sender == sender
                && entry.sequence == sequence) {
                return &entry;
            }
        }
        return nullptr;
    }

    [[nodiscard]] const Entry* find_exact_entry(const Frame& frame) const
    {
        for (const Entry& entry : entries_) {
            if (entry.used && entry.sender == frame.sender_id
                && entry.receiver == frame.receiver_id
                && entry.sequence == frame.seq
                && entry.command_set == frame.cmd_set
                && entry.command_id == frame.cmd_id
                && entry.command_type == normalized_command_type(frame)
                && entry.payload_size == frame.data_len
                && (frame.data_len == 0U
                    || (frame.data != nullptr
                        && std::memcmp(entry.payload, frame.data,
                                       frame.data_len) == 0))) {
                return &entry;
            }
        }
        return nullptr;
    }

    Entry entries_[CONFIG_LINK_REPLAY_CACHE_SIZE] {};
    size_t next_ {0U};
};

} // namespace link
