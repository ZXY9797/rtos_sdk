#pragma once

#include "frame.h"

#ifndef CONFIG_LINK_MAX_FRAME_SIZE
#define CONFIG_LINK_MAX_FRAME_SIZE 512
#endif

namespace link {

struct PackArgs {
    uint8_t  sender;
    uint8_t  receiver;
    uint8_t  cmd_set;
    uint8_t  cmd_id;
    AckMode  ack_mode   {AckMode::No};
    EncMode  enc_mode   {EncMode::None};
    Priority priority   {Priority::Low};
    bool     is_ack     {false};
    bool     retransmit {false};
    uint16_t seq        {0};
};

class FrameCodec {
public:
    // 从字节流解包一帧（非阻塞）
    // 返回: >0=解出一帧, 0=数据不足, <0=错误
    int feed(uint8_t byte);

    // 获取解出的帧
    const Frame &frame() const { return frame_; }
    const uint8_t *raw() const { return buf_; }
    size_t raw_len() const { return frame_.len; }

    // 打包一帧到 buf，返回帧总长
    static size_t pack(uint8_t *buf, size_t buf_size,
                       const PackArgs &args,
                       const uint8_t *data, uint8_t data_len);

    // 重置状态机
    void reset();

private:
    uint8_t buf_[CONFIG_LINK_MAX_FRAME_SIZE];
    size_t widx_ {0};
    Frame frame_ {};

    enum class State : uint8_t { SearchSof, FetchVerLen, CheckHead, FetchData, CheckCrc };
    State state_ {State::SearchSof};

    // 临时变量
    uint8_t  ver_len_lo_ {0};
    uint16_t expected_len_ {0};

    void parse_header();
    bool check_crc();
};

} // namespace link
