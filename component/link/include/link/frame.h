#pragma once

#include <cstddef>
#include <cstdint>

namespace link {

inline constexpr uint8_t SOF = 0xAAU;
inline constexpr uint8_t PROTOCOL_VERSION = 1U;
inline constexpr size_t HEADER_SIZE = 11U;
inline constexpr size_t CRC_SIZE = 2U;
inline constexpr size_t MIN_FRAME_SIZE = HEADER_SIZE + CRC_SIZE;

// ver_len: reserved[15:12], version[11:10], total frame length[9:0].
inline constexpr uint16_t VER_LEN_VER_MASK = 0x03U;
inline constexpr uint16_t VER_LEN_LEN_MASK = 0x03FFU;
inline constexpr uint16_t VER_LEN_RESERVED_MASK = 0xF000U;
inline constexpr int VER_LEN_VER_SHIFT = 10;

inline uint16_t pack_ver_len(uint8_t version, uint16_t total_len)
{
    return static_cast<uint16_t>(total_len & VER_LEN_LEN_MASK)
         | static_cast<uint16_t>((version & VER_LEN_VER_MASK) << VER_LEN_VER_SHIFT);
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

inline uint8_t bcc(const uint8_t* data, size_t len)
{
    uint8_t sum = 0U;
    if (data == nullptr) {
        return sum;
    }
    for (size_t i = 0U; i < len; ++i) {
        sum ^= data[i];
    }
    return sum;
}

uint16_t crc16_ccitt(const uint8_t* data, size_t len);

struct Handler {
    uint8_t cmd_set;
    uint8_t cmd_id;
    void (*cb)(const Frame& frame, void* arg);
    void* arg;
    uint8_t flags;
};

inline constexpr uint8_t HANDLER_REQUIRE_SECURE = 0x01U;

#define LINK_HANDLER(set, id, cb, arg) \
    __attribute__((used, section(".link_handler"))) \
    static const ::link::Handler _link_h_##set##_##id = {set, id, cb, arg, 0U}

#if defined(CONFIG_LINK_SECURITY)
#define LINK_SECURE_HANDLER(set, id, cb, arg) \
    __attribute__((used, section(".link_handler"))) \
    static const ::link::Handler _link_h_##set##_##id = { \
        set, id, cb, arg, ::link::HANDLER_REQUIRE_SECURE}
#else
#define LINK_SECURE_HANDLER(set, id, cb, arg) \
    static_assert(false, \
        "LINK_SECURE_HANDLER requires CONFIG_LINK_SECURITY=y")
#endif

} // namespace link
