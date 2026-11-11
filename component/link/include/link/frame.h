#pragma once

#include <cstddef>
#include <cstdint>

namespace link {

inline constexpr uint8_t SOF = 0xAAU;
inline constexpr uint8_t PROTOCOL_VERSION = 1U;
inline constexpr size_t HEADER_SIZE = 11U;
inline constexpr size_t CRC_SIZE = 2U;
inline constexpr size_t MIN_FRAME_SIZE = HEADER_SIZE + CRC_SIZE;

// ver_len: reserved[15:14], version[13:10], total frame length[9:0].
inline constexpr uint16_t VER_LEN_VER_MASK = 0x0FU;
inline constexpr uint16_t VER_LEN_LEN_MASK = 0x03FFU;
inline constexpr uint16_t VER_LEN_RESERVED_MASK = 0xC000U;
inline constexpr int VER_LEN_VER_SHIFT = 10;

static_assert(PROTOCOL_VERSION <= VER_LEN_VER_MASK);

// Returns zero for values that cannot be represented by the wire fields.
inline uint16_t pack_ver_len(uint8_t version, uint16_t total_len)
{
    if (version > VER_LEN_VER_MASK || total_len > VER_LEN_LEN_MASK) {
        return 0U;
    }
    return total_len | static_cast<uint16_t>(version << VER_LEN_VER_SHIFT);
}

inline uint8_t unpack_ver(uint16_t ver_len)
{
    return static_cast<uint8_t>((ver_len >> VER_LEN_VER_SHIFT) & VER_LEN_VER_MASK);
}

inline uint16_t unpack_len(uint16_t ver_len)
{
    return static_cast<uint16_t>(ver_len & VER_LEN_LEN_MASK);
}

enum class AckMode : uint8_t {
    No = 0U,
    Now = 1U,
    Finish = 2U,
    Progress = 3U,
};

enum class EncMode : uint8_t {
    None = 0U,
    Aes128 = 1U,
    Aes256 = 2U,
    ChaCha20 = 3U,
};

enum class Priority : uint8_t {
    Low = 0U,
    High = 1U,
};

inline constexpr uint8_t CMD_TYPE_IS_ACK_MASK = 0x01U;
inline constexpr uint8_t CMD_TYPE_ACK_MODE_MASK = 0x03U;
inline constexpr uint8_t CMD_TYPE_ENC_MODE_MASK = 0x03U;
inline constexpr uint8_t CMD_TYPE_PRIORITY_MASK = 0x01U;
inline constexpr uint8_t CMD_TYPE_RETRANSMIT_MASK = 0x01U;
inline constexpr uint8_t CMD_TYPE_RESERVED_MASK = 0x80U;

inline constexpr int CMD_TYPE_ACK_MODE_SHIFT = 1;
inline constexpr int CMD_TYPE_ENC_MODE_SHIFT = 3;
inline constexpr int CMD_TYPE_PRIORITY_SHIFT = 5;
inline constexpr int CMD_TYPE_RETRANSMIT_SHIFT = 6;

inline uint8_t pack_cmd_type(bool is_ack, AckMode ack_mode, EncMode enc_mode,
                             Priority priority, bool retransmit)
{
    return static_cast<uint8_t>((is_ack ? 1U : 0U)
         | (static_cast<uint8_t>(ack_mode) << CMD_TYPE_ACK_MODE_SHIFT)
         | (static_cast<uint8_t>(enc_mode) << CMD_TYPE_ENC_MODE_SHIFT)
         | (static_cast<uint8_t>(priority) << CMD_TYPE_PRIORITY_SHIFT)
         | ((retransmit ? 1U : 0U) << CMD_TYPE_RETRANSMIT_SHIFT));
}

inline bool cmd_type_is_ack(uint8_t value)
{
    return (value & CMD_TYPE_IS_ACK_MASK) != 0U;
}

inline AckMode cmd_type_ack_mode(uint8_t value)
{
    return static_cast<AckMode>((value >> CMD_TYPE_ACK_MODE_SHIFT) & CMD_TYPE_ACK_MODE_MASK);
}

inline EncMode cmd_type_enc_mode(uint8_t value)
{
    return static_cast<EncMode>((value >> CMD_TYPE_ENC_MODE_SHIFT) & CMD_TYPE_ENC_MODE_MASK);
}

inline Priority cmd_type_priority(uint8_t value)
{
    return static_cast<Priority>((value >> CMD_TYPE_PRIORITY_SHIFT) & CMD_TYPE_PRIORITY_MASK);
}

inline bool cmd_type_retransmit(uint8_t value)
{
    return ((value >> CMD_TYPE_RETRANSMIT_SHIFT) & CMD_TYPE_RETRANSMIT_MASK) != 0U;
}

inline constexpr uint8_t ADDR_BROADCAST = 0x00U;
inline constexpr uint8_t ADDR_RESERVED = 0xFFU;

inline bool is_unicast_addr(uint8_t addr)
{
    return addr != ADDR_BROADCAST && addr != ADDR_RESERVED;
}

inline uint8_t make_addr(uint8_t host_id, uint8_t host_idx)
{
    return static_cast<uint8_t>(((host_id & 0x0FU) << 4U) | (host_idx & 0x0FU));
}

inline uint8_t addr_host_id(uint8_t addr)
{
    return static_cast<uint8_t>((addr >> 4U) & 0x0FU);
}

inline uint8_t addr_host_idx(uint8_t addr)
{
    return static_cast<uint8_t>(addr & 0x0FU);
}

struct Frame {
    uint8_t sof {};
    uint8_t ver {};
    uint16_t len {};
    uint8_t cmd_type {};
    uint8_t sender_id {};
    uint8_t receiver_id {};
    uint16_t seq {};
    uint8_t cmd_set {};
    uint8_t cmd_id {};
    const uint8_t* data {nullptr};
    uint16_t data_len {};

    [[nodiscard]] bool is_ack() const { return cmd_type_is_ack(cmd_type); }
    [[nodiscard]] AckMode ack_mode() const { return cmd_type_ack_mode(cmd_type); }
    [[nodiscard]] EncMode enc_mode() const { return cmd_type_enc_mode(cmd_type); }
    [[nodiscard]] Priority priority() const { return cmd_type_priority(cmd_type); }
    [[nodiscard]] bool retransmit() const { return cmd_type_retransmit(cmd_type); }
};

// CRC-8/SMBUS: poly=0x07, init=0, refin=false, refout=false, xorout=0.
// Covers SOF and ver_len. Check("123456789") = 0xF4.
uint8_t crc8_smbus(const uint8_t* data, size_t len);

// CRC-16/IBM-3740 (CCITT-FALSE): poly=0x1021, init=0xFFFF,
// refin=false, refout=false, xorout=0. Check("123456789") = 0x29B1.
// Covers all encoded header and payload bytes; stored low byte first.
uint16_t crc16_ccitt(const uint8_t* data, size_t len);

inline constexpr uint8_t HANDLER_STATUS_OK = 0x00U;
inline constexpr uint8_t HANDLER_STATUS_IN_PROGRESS = 0x01U;
inline constexpr uint8_t HANDLER_STATUS_INVALID_RESULT = 0xFAU;
inline constexpr uint8_t HANDLER_STATUS_TIMEOUT = 0xFBU;
inline constexpr uint8_t HANDLER_STATUS_QUEUE_FULL = 0xFCU;
inline constexpr uint8_t HANDLER_STATUS_REPLAY_CONFLICT = 0xFDU;
inline constexpr uint8_t HANDLER_STATUS_AUTH_REQUIRED = 0xFEU;
inline constexpr uint8_t HANDLER_STATUS_NOT_FOUND = 0xFFU;

/**
 * Correlates one deferred handler completion with its request.
 *
 * The value may be copied to a handler-owned static queue. Router validates
 * every field before accepting a completion, so callers must treat it as an
 * opaque value and must not modify it. A token becomes invalid after its
 * handler completes, times out, or Router is deinitialized.
 */
struct CompletionToken {
    uint32_t id {};
    uint32_t router_epoch {};
    uint16_t sequence {};
    uint16_t source_link_id {};
    uint8_t source_link_index {};
    uint8_t sender {};
    uint8_t receiver {};
    uint8_t command_set {};
    uint8_t command_id {};
    AckMode request_ack_mode {AckMode::No};
    EncMode encryption_mode {EncMode::None};
    Priority priority {Priority::Low};

    [[nodiscard]] bool is_valid() const
    {
        return id != 0U && router_epoch != 0U;
    }
};

enum class HandlerDisposition : uint8_t {
    Completed = 0U,
    Deferred = 1U,
};

struct HandlerResult {
    HandlerDisposition disposition {HandlerDisposition::Completed};
    uint8_t status {HANDLER_STATUS_OK};
    const uint8_t* data {nullptr};
    size_t data_len {0U};

    [[nodiscard]] static HandlerResult completed(
        uint8_t result_status = HANDLER_STATUS_OK,
        const uint8_t* result_data = nullptr,
        size_t result_data_len = 0U)
    {
        return {
            HandlerDisposition::Completed,
            result_status,
            result_data,
            result_data_len,
        };
    }

    [[nodiscard]] static HandlerResult deferred()
    {
        return {HandlerDisposition::Deferred, HANDLER_STATUS_IN_PROGRESS,
                nullptr, 0U};
    }
};

using HandlerCallback = void (*)(const Frame& frame, void* arg);
using AsyncHandlerCallback = HandlerResult (*)(
    const Frame& frame, const CompletionToken& token, void* arg);

struct Handler {
    uint8_t receiver {};
    uint8_t receiver_mask {};
    uint8_t cmd_set {};
    uint8_t cmd_set_mask {};
    uint8_t cmd_id {};
    uint8_t cmd_id_mask {};
    HandlerCallback cb {nullptr};
    AsyncHandlerCallback async_cb {nullptr};
    void* arg {nullptr};
    uint32_t finish_timeout_ms {};
    uint8_t flags {};
};

inline constexpr uint8_t HANDLER_REQUIRE_SECURE = 0x01U;

#define LINK_DETAIL_CONCAT_INNER(a, b) a##b
#define LINK_DETAIL_CONCAT(a, b) LINK_DETAIL_CONCAT_INNER(a, b)

#define LINK_DETAIL_SYNC_HANDLER( \
    host, host_mask, set, set_mask, id, id_mask, callback, callback_arg, \
    handler_flags, unique) \
    __attribute__((used, section(".link_handler"))) \
    static const ::link::Handler LINK_DETAIL_CONCAT( \
        _link_handler_, unique) = { \
            host, host_mask, set, set_mask, id, id_mask, callback, nullptr, \
            callback_arg, 0U, handler_flags}

#define LINK_DETAIL_ASYNC_HANDLER(host, host_mask, set, set_mask, id, id_mask, \
                                  callback, callback_arg, timeout_ms, \
                                  handler_flags, unique) \
    __attribute__((used, section(".link_handler"))) \
    static const ::link::Handler LINK_DETAIL_CONCAT( \
        _link_handler_, unique) = { \
            host, host_mask, set, set_mask, id, id_mask, nullptr, callback, \
            callback_arg, timeout_ms, handler_flags}

#define LINK_HANDLER(set, id, cb, arg) \
    LINK_DETAIL_SYNC_HANDLER(0U, 0U, set, 0xFFU, id, 0xFFU, cb, arg, 0U, \
                             __COUNTER__)

#define LINK_HOST_HANDLER( \
    host, host_mask, set, set_mask, id, id_mask, cb, arg) \
    LINK_DETAIL_SYNC_HANDLER(host, host_mask, set, set_mask, id, id_mask, cb, \
                             arg, 0U, __COUNTER__)

#define LINK_ASYNC_HANDLER(set, id, cb, arg, timeout_ms) \
    LINK_DETAIL_ASYNC_HANDLER(0U, 0U, set, 0xFFU, id, 0xFFU, cb, arg, \
                              timeout_ms, 0U, __COUNTER__)

#define LINK_HOST_ASYNC_HANDLER(host, host_mask, set, set_mask, id, id_mask, \
                                cb, arg, timeout_ms) \
    LINK_DETAIL_ASYNC_HANDLER(host, host_mask, set, set_mask, id, id_mask, cb, \
                              arg, timeout_ms, 0U, __COUNTER__)

#if defined(CONFIG_LINK_SECURITY)
#define LINK_SECURE_HANDLER(set, id, cb, arg) \
    LINK_DETAIL_SYNC_HANDLER(0U, 0U, set, 0xFFU, id, 0xFFU, cb, arg, \
                             ::link::HANDLER_REQUIRE_SECURE, __COUNTER__)

#define LINK_SECURE_ASYNC_HANDLER(set, id, cb, arg, timeout_ms) \
    LINK_DETAIL_ASYNC_HANDLER( \
        0U, 0U, set, 0xFFU, id, 0xFFU, cb, arg, timeout_ms, \
        ::link::HANDLER_REQUIRE_SECURE, __COUNTER__)

#define LINK_SECURE_HOST_HANDLER(host, host_mask, set, set_mask, id, id_mask, \
                                 cb, arg) \
    LINK_DETAIL_SYNC_HANDLER( \
        host, host_mask, set, set_mask, id, id_mask, cb, arg, \
        ::link::HANDLER_REQUIRE_SECURE, __COUNTER__)

#define LINK_SECURE_HOST_ASYNC_HANDLER( \
    host, host_mask, set, set_mask, id, id_mask, cb, arg, timeout_ms) \
    LINK_DETAIL_ASYNC_HANDLER( \
        host, host_mask, set, set_mask, id, id_mask, cb, arg, timeout_ms, \
        ::link::HANDLER_REQUIRE_SECURE, __COUNTER__)
#else
#define LINK_SECURE_HANDLER(set, id, cb, arg) \
    static_assert(false, \
        "LINK_SECURE_HANDLER requires CONFIG_LINK_SECURITY=y")

#define LINK_SECURE_ASYNC_HANDLER(set, id, cb, arg, timeout_ms) \
    static_assert(false, \
        "LINK_SECURE_ASYNC_HANDLER requires CONFIG_LINK_SECURITY=y")

#define LINK_SECURE_HOST_HANDLER(host, host_mask, set, set_mask, id, id_mask, \
                                 cb, arg) \
    static_assert(false, \
        "LINK_SECURE_HOST_HANDLER requires CONFIG_LINK_SECURITY=y")

#define LINK_SECURE_HOST_ASYNC_HANDLER( \
    host, host_mask, set, set_mask, id, id_mask, cb, arg, timeout_ms) \
    static_assert(false, \
        "LINK_SECURE_HOST_ASYNC_HANDLER requires CONFIG_LINK_SECURITY=y")
#endif

} // namespace link
