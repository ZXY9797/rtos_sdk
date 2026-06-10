#include "link/codec.h"
#include <cstring>

namespace link {

void FrameCodec::reset() {
    widx_ = 0;
    frame_ = {};
    state_ = State::SearchSof;
    expected_len_ = 0;
    ver_len_lo_ = 0;
}

int FrameCodec::feed(uint8_t byte) {
    switch (state_) {
    case State::SearchSof:
        if (byte == SOF) {
            widx_ = 0;
            buf_[widx_++] = byte;
            state_ = State::FetchVerLen;
        }
        return 0;

    case State::FetchVerLen:
        buf_[widx_++] = byte;
        if (widx_ == 3) {
            // 收到 ver_len 两字节，检查 head_crc
            state_ = State::CheckHead;
        }
        return 0;

    case State::CheckHead:
        buf_[widx_++] = byte;
        // 验证前 3 字节 BCC
        if (bcc(buf_, 3) != byte) {
            reset();
            return -1;  // head CRC 错误
        }
        // 解析 ver_len
        {
            uint16_t ver_len = static_cast<uint16_t>(buf_[1]) | (static_cast<uint16_t>(buf_[2]) << 8);
            frame_.ver = unpack_ver(ver_len);
            frame_.len = unpack_len(ver_len);
        }
        // 校验帧长
        if (frame_.len < MIN_FRAME_SIZE || frame_.len > CONFIG_LINK_MAX_FRAME_SIZE) {
            reset();
            return -2;  // 帧长非法
        }
        expected_len_ = frame_.len;
        // 继续接收剩余头部字节
        state_ = State::FetchData;
        return 0;

    case State::FetchData:
        buf_[widx_++] = byte;
        if (widx_ >= expected_len_) {
            // 数据收完，校验尾部 CRC
            if (!check_crc()) {
                reset();
                return -3;  // CRC 错误
            }
            parse_header();
            return 1;  // 解出一帧
        }
        return 0;

    case State::CheckCrc:
        // 不应该到达这里
        reset();
        return -4;
    }

    return 0;
}

void FrameCodec::parse_header() {
    frame_.sof = buf_[0];
    frame_.cmd_type = buf_[4];
    frame_.sender_id = buf_[5];
    frame_.receiver_id = buf_[6];
    frame_.seq = static_cast<uint16_t>(buf_[7]) | (static_cast<uint16_t>(buf_[8]) << 8);
    frame_.cmd_set = buf_[9];
    frame_.cmd_id = buf_[10];

    size_t data_len = frame_.len - HEADER_SIZE - CRC_SIZE;
    frame_.data_len = static_cast<uint8_t>(data_len);
    frame_.data = (data_len > 0) ? (buf_ + HEADER_SIZE) : nullptr;
}

bool FrameCodec::check_crc() {
    if (widx_ < MIN_FRAME_SIZE) return false;
    size_t crc_area_len = expected_len_ - CRC_SIZE;
    uint16_t computed = crc16_ccitt(buf_, crc_area_len);
    uint16_t received = static_cast<uint16_t>(buf_[crc_area_len])
                      | (static_cast<uint16_t>(buf_[crc_area_len + 1]) << 8);
    return computed == received;
}

size_t FrameCodec::pack(uint8_t *buf, size_t buf_size,
                        const PackArgs &args,
                        const uint8_t *data, uint8_t data_len) {
    size_t total = HEADER_SIZE + data_len + CRC_SIZE;
    if (buf_size < total) return 0;

    buf[0] = SOF;
    uint16_t ver_len = pack_ver_len(1, static_cast<uint16_t>(total));
    buf[1] = ver_len & 0xFF;
    buf[2] = (ver_len >> 8) & 0xFF;
    buf[3] = bcc(buf, 3);
    buf[4] = pack_cmd_type(args.is_ack, args.ack_mode, args.enc_mode,
                           args.priority, args.retransmit);
    buf[5] = args.sender;
    buf[6] = args.receiver;
    buf[7] = args.seq & 0xFF;
    buf[8] = (args.seq >> 8) & 0xFF;
    buf[9] = args.cmd_set;
    buf[10] = args.cmd_id;
    if (data_len > 0 && data)
        memcpy(buf + HEADER_SIZE, data, data_len);

    uint16_t crc = crc16_ccitt(buf, HEADER_SIZE + data_len);
    buf[HEADER_SIZE + data_len] = crc & 0xFF;
    buf[HEADER_SIZE + data_len + 1] = (crc >> 8) & 0xFF;

    return total;
}

} // namespace link
