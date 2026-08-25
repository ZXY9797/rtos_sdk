#include "link/codec.h"
#include <cstring>

namespace link {

void FrameCodec::reset() {
    prepare_next_frame();
    frame_ = {};
}

void FrameCodec::prepare_next_frame() {
    widx_ = 0;
    state_ = State::SearchSof;
    expected_len_ = 0;
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
        if (widx_ >= sizeof(buf_)) {
            reset();
            return -2;
        }
        buf_[widx_++] = byte;
        if (widx_ == 3) {
            // 收到 ver_len 两字节，检查 head_crc
            state_ = State::CheckHead;
        }
        return 0;

    case State::CheckHead:
        if (widx_ >= sizeof(buf_)) {
            reset();
            return -2;
        }
        buf_[widx_++] = byte;
        // 验证前 3 字节 BCC
        if (bcc(buf_, 3) != byte) {
            reset();
            return -1;  // head CRC 错误
        }
        // 解析 ver_len
        {
            const uint16_t ver_len = static_cast<uint16_t>(buf_[1])
                | static_cast<uint16_t>(
                    static_cast<uint16_t>(buf_[2]) << 8U);
            if ((ver_len & VER_LEN_RESERVED_MASK) != 0U) {
                reset();
                return -2;
            }
            frame_.ver = unpack_ver(ver_len);
            frame_.len = unpack_len(ver_len);
        }
        // 校验帧长
        if (frame_.ver != PROTOCOL_VERSION
            || frame_.len < MIN_FRAME_SIZE
            || frame_.len > CONFIG_LINK_MAX_FRAME_SIZE) {
            reset();
            return -2;  // 帧长非法
        }
        expected_len_ = frame_.len;
        // 继续接收剩余头部字节
        state_ = State::FetchData;
        return 0;

    case State::FetchData:
        if (widx_ >= sizeof(buf_) || widx_ >= expected_len_) {
            reset();
            return -2;
        }
        buf_[widx_++] = byte;
        if (widx_ >= expected_len_) {
            // 数据收完，校验尾部 CRC
            if (!check_crc()) {
                reset();
                return -3;  // CRC 错误
            }
            if (!parse_header()) {
                reset();
                return -4;
            }
            prepare_next_frame();
            return 1;  // 解出一帧
        }
        return 0;

    default:
        // 不应该到达这里
        reset();
        return -4;
    }

    return 0;
}

bool FrameCodec::parse_header() {
    if ((buf_[4] & CMD_TYPE_RESERVED_MASK) != 0U) {
        return false;
    }
    frame_.sof = buf_[0];
    frame_.cmd_type = buf_[4];
    frame_.sender_id = buf_[5];
    frame_.receiver_id = buf_[6];
    frame_.seq = static_cast<uint16_t>(buf_[7]) | (static_cast<uint16_t>(buf_[8]) << 8);
    frame_.cmd_set = buf_[9];
    frame_.cmd_id = buf_[10];

    const size_t data_len = frame_.len - HEADER_SIZE - CRC_SIZE;
    frame_.data_len = static_cast<uint16_t>(data_len);
    frame_.data = (data_len > 0) ? (buf_ + HEADER_SIZE) : nullptr;
    return true;
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
                        const uint8_t *data, size_t data_len) {
    if (!buf || (data_len > 0U && !data)) {
        return 0U;
    }
    constexpr size_t max_payload =
        CONFIG_LINK_MAX_FRAME_SIZE - HEADER_SIZE - CRC_SIZE;
    if (data_len > max_payload
        || static_cast<uint8_t>(args.ack_mode)
            > static_cast<uint8_t>(AckMode::Progress)
        || static_cast<uint8_t>(args.enc_mode)
            > static_cast<uint8_t>(EncMode::ChaCha20)
        || static_cast<uint8_t>(args.priority)
            > static_cast<uint8_t>(Priority::High)) {
        return 0U;
    }
    const size_t total = HEADER_SIZE + data_len + CRC_SIZE;
    if (total > CONFIG_LINK_MAX_FRAME_SIZE ||
        total > VER_LEN_LEN_MASK || buf_size < total) {
        return 0U;
    }

    buf[0] = SOF;
    const uint16_t ver_len = pack_ver_len(
        PROTOCOL_VERSION, static_cast<uint16_t>(total));
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
    if (data_len > 0U) {
        std::memmove(buf + HEADER_SIZE, data, data_len);
    }

    uint16_t crc = crc16_ccitt(buf, HEADER_SIZE + data_len);
    buf[HEADER_SIZE + data_len] = crc & 0xFF;
    buf[HEADER_SIZE + data_len + 1] = (crc >> 8) & 0xFF;

    return total;
}

} // namespace link
