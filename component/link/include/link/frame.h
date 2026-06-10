#pragma once

#include <cstdint>
#include <cstddef>
#include <cstring>

namespace link {

// ── 常量 ──

inline constexpr uint8_t SOF = 0xAA;
inline constexpr size_t HEADER_SIZE = 11;      // SOF + ver_len(2) + head_crc + cmd_type + sender + receiver + seq(2) + cmd_set + cmd_id
inline constexpr size_t CRC_SIZE = 2;
inline constexpr size_t MIN_FRAME_SIZE = HEADER_SIZE + CRC_SIZE;  // 无数据帧

// ── ver_len 位域 ──
// bit:  15..12    11..10    9..0
//       resv(4)   ver(2)    len(10)

inline constexpr uint16_t VER_LEN_VER_MASK   = 0x03;
inline constexpr uint16_t VER_LEN_LEN_MASK   = 0x3FF;
inline constexpr int      VER_LEN_VER_SHIFT  = 10;
inline constexpr int      VER_LEN_LEN_SHIFT  = 0;

inline uint16_t pack_ver_len(uint8_t ver, uint16_t total_len) {
    return (static_cast<uint16_t>(total_len) & VER_LEN_LEN_MASK)
         | (static_cast<uint16_t>(ver & VER_LEN_VER_MASK) << VER_LEN_VER_SHIFT);
}

inline uint8_t unpack_ver(uint16_t ver_len) {
    return static_cast<uint8_t>((ver_len >> VER_LEN_VER_SHIFT) & VER_LEN_VER_MASK);
}

inline uint16_t unpack_len(uint16_t ver_len) {
    return ver_len & VER_LEN_LEN_MASK;
}

// ── cmd_type 位域 ──
// bit:  7       6           5          4..3       2..1        0
//       resv(1)  retransmit(1)  priority(1)  enc_mode(2)  ack_mode(2)  is_ack(1)

enum class AckMode : uint8_t {
    No       = 0,  // 无需应答
    Now      = 1,  // 收到立即应答
    Finish   = 2,  // 执行完成后应答
    Progress = 3,  // 分阶段应答
};

enum class EncMode : uint8_t {
    None     = 0,
    Aes128   = 1,
    Aes256   = 2,
    ChaCha20 = 3,
};

enum class Priority : uint8_t {
    Low  = 0,
    High = 1,
};

inline constexpr uint8_t CMD_TYPE_IS_ACK_MASK     = 0x01;
inline constexpr uint8_t CMD_TYPE_ACK_MODE_MASK   = 0x03;
inline constexpr uint8_t CMD_TYPE_ENC_MODE_MASK   = 0x03;
inline constexpr uint8_t CMD_TYPE_PRIORITY_MASK   = 0x01;
inline constexpr uint8_t CMD_TYPE_RETRANSMIT_MASK = 0x01;

inline constexpr int CMD_TYPE_ACK_MODE_SHIFT   = 1;
inline constexpr int CMD_TYPE_ENC_MODE_SHIFT   = 3;
inline constexpr int CMD_TYPE_PRIORITY_SHIFT   = 5;
inline constexpr int CMD_TYPE_RETRANSMIT_SHIFT = 6;

inline uint8_t pack_cmd_type(bool is_ack, AckMode ack_mode, EncMode enc_mode,
                              Priority priority, bool retransmit) {
    return (is_ack ? 1 : 0)
         | (static_cast<uint8_t>(ack_mode) << CMD_TYPE_ACK_MODE_SHIFT)
         | (static_cast<uint8_t>(enc_mode) << CMD_TYPE_ENC_MODE_SHIFT)
         | (static_cast<uint8_t>(priority) << CMD_TYPE_PRIORITY_SHIFT)
         | ((retransmit ? 1 : 0) << CMD_TYPE_RETRANSMIT_SHIFT);
}

inline bool     cmd_type_is_ack(uint8_t ct)     { return ct & CMD_TYPE_IS_ACK_MASK; }
inline AckMode  cmd_type_ack_mode(uint8_t ct)   { return static_cast<AckMode>((ct >> CMD_TYPE_ACK_MODE_SHIFT) & CMD_TYPE_ACK_MODE_MASK); }
inline EncMode  cmd_type_enc_mode(uint8_t ct)   { return static_cast<EncMode>((ct >> CMD_TYPE_ENC_MODE_SHIFT) & CMD_TYPE_ENC_MODE_MASK); }
inline Priority cmd_type_priority(uint8_t ct)   { return static_cast<Priority>((ct >> CMD_TYPE_PRIORITY_SHIFT) & CMD_TYPE_PRIORITY_MASK); }
inline bool     cmd_type_retransmit(uint8_t ct) { return (ct >> CMD_TYPE_RETRANSMIT_SHIFT) & CMD_TYPE_RETRANSMIT_MASK; }

// ── 地址编码 ──
// bit:  7..4      3..0
//       host_id(4)  host_idx(4)

inline constexpr uint8_t ADDR_BROADCAST = 0x00;  // 广播
inline constexpr uint8_t ADDR_RESERVED  = 0xFF;  // 保留

inline uint8_t make_addr(uint8_t host_id, uint8_t host_idx) {
    return ((host_id & 0x0F) << 4) | (host_idx & 0x0F);
}

inline uint8_t addr_host_id(uint8_t addr)   { return (addr >> 4) & 0x0F; }
inline uint8_t addr_host_idx(uint8_t addr)  { return addr & 0x0F; }

// ── 帧结构体 ──

struct Frame {
    uint8_t  sof;           // 0xAA
    uint8_t  ver;           // 版本
    uint16_t len;           // 总帧长
    uint8_t  cmd_type;      // is_ack | ack_mode | enc_mode | priority | retransmit
    uint8_t  sender_id;     // 高4位=host_id, 低4位=host_idx
    uint8_t  receiver_id;   // 高4位=host_id, 低4位=host_idx
    uint16_t seq;           // 消息序列号
    uint8_t  cmd_set;       // 命令集
    uint8_t  cmd_id;        // 命令ID
    const uint8_t *data;    // 指向帧内数据（不拥有内存）
    uint8_t  data_len;      // 数据长度

    // 便捷访问器
    bool     is_ack()     const { return cmd_type_is_ack(cmd_type); }
    AckMode  ack_mode()   const { return cmd_type_ack_mode(cmd_type); }
    EncMode  enc_mode()   const { return cmd_type_enc_mode(cmd_type); }
    Priority priority()   const { return cmd_type_priority(cmd_type); }
    bool     retransmit() const { return cmd_type_retransmit(cmd_type); }
};

// ── BCC 校验 ──

inline uint8_t bcc(const uint8_t *data, size_t len) {
    uint8_t sum = 0;
    for (size_t i = 0; i < len; i++)
        sum ^= data[i];
    return sum;
}

// ── CRC-16/CCITT ──

uint16_t crc16_ccitt(const uint8_t *data, size_t len);

// ── Handler 回调 ──

struct Handler {
    uint8_t cmd_set;
    uint8_t cmd_id;
    void (*cb)(const Frame &frame, void *arg);
    void *arg;
};

// ── 回调注册宏 ──

#define LINK_HANDLER(set, id, cb, arg) \
    __attribute__((used, section(".link_handler"))) \
    static const ::link::Handler _link_h_##set##_##id = { set, id, cb, arg }

} // namespace link
