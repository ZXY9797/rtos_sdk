#include <link/codec.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>

#define CHECK(condition)                                                     \
    do {                                                                     \
        if (!(condition)) {                                                  \
            std::fprintf(stderr, "CHECK failed at %s:%d: %s\n",           \
                         __FILE__, __LINE__, #condition);                    \
            std::abort();                                                    \
        }                                                                    \
    } while (false)

namespace {

int feed_frame(link::FrameCodec& codec, const uint8_t* raw, size_t length)
{
    int result = 0;
    for (size_t index = 0U; index < length; ++index) {
        result = codec.feed(raw[index]);
        if (result != 0) {
            break;
        }
    }
    return result;
}

void update_crc(uint8_t* raw, size_t length)
{
    const size_t crc_offset = length - link::CRC_SIZE;
    const uint16_t crc = link::crc16_ccitt(raw, crc_offset);
    raw[crc_offset] = static_cast<uint8_t>(crc & 0xFFU);
    raw[crc_offset + 1U] = static_cast<uint8_t>(crc >> 8U);
}

void test_round_trip()
{
    const std::array<uint8_t, 5U> payload {1U, 2U, 3U, 4U, 5U};
    link::PackArgs args {};
    args.sender = 0x11U;
    args.receiver = 0x22U;
    args.cmd_set = 3U;
    args.cmd_id = 7U;
    args.ack_mode = link::AckMode::Finish;
    args.priority = link::Priority::High;
    args.seq = 0x1234U;

    std::array<uint8_t, CONFIG_LINK_MAX_FRAME_SIZE> raw {};
    const size_t length = link::FrameCodec::pack(
        raw.data(), raw.size(), args, payload.data(), payload.size());
    CHECK(length == link::MIN_FRAME_SIZE + payload.size());

    link::FrameCodec codec;
    CHECK(feed_frame(codec, raw.data(), length) == 1);
    const link::Frame& frame = codec.frame();
    CHECK(frame.sender_id == args.sender);
    CHECK(frame.receiver_id == args.receiver);
    CHECK(frame.seq == args.seq);
    CHECK(frame.ack_mode() == args.ack_mode);
    CHECK(frame.priority() == args.priority);
    CHECK(frame.data_len == payload.size());
    for (size_t index = 0U; index < payload.size(); ++index) {
        CHECK(frame.data[index] == payload[index]);
    }
}

void test_v1_wire_compatibility_vector()
{
    static_assert(link::PROTOCOL_VERSION == 1U);
    static_assert(link::HEADER_SIZE == 11U);
    static_assert(link::CRC_SIZE == 2U);

    constexpr std::array<uint8_t, 5U> payload {
        1U, 2U, 3U, 4U, 5U,
    };
    constexpr std::array<uint8_t, 18U> expected {
        0xAAU, 0x12U, 0x04U, 0xBCU, 0x24U, 0x11U,
        0x22U, 0x34U, 0x12U, 0x03U, 0x07U, 0x01U,
        0x02U, 0x03U, 0x04U, 0x05U, 0x7BU, 0x31U,
    };
    link::PackArgs args {};
    args.sender = 0x11U;
    args.receiver = 0x22U;
    args.cmd_set = 0x03U;
    args.cmd_id = 0x07U;
    args.ack_mode = link::AckMode::Finish;
    args.priority = link::Priority::High;
    args.seq = 0x1234U;

    std::array<uint8_t, CONFIG_LINK_MAX_FRAME_SIZE> raw {};
    const size_t length = link::FrameCodec::pack(
        raw.data(), raw.size(), args, payload.data(), payload.size());

    CHECK(length == expected.size());
    for (size_t index = 0U; index < expected.size(); ++index) {
        if (raw[index] != expected[index]) {
            std::fprintf(stderr,
                         "v1 byte %zu: expected 0x%02x, got 0x%02x\n",
                         index, expected[index], raw[index]);
            std::abort();
        }
    }
}

void test_v1_legacy_crc_vector()
{
    // Link v1 historically shipped this lookup table. The result intentionally
    // differs from the CRC-16/CCITT-FALSE check value 0x29B1; changing it would
    // silently break existing v1 peers.
    constexpr std::array<uint8_t, 9U> check {
        '1', '2', '3', '4', '5', '6', '7', '8', '9',
    };
    CHECK(link::crc16_ccitt(check.data(), check.size()) == 0xA9B1U);
}

void test_v1_ack_wire_compatibility_vector()
{
    constexpr std::array<uint8_t, 1U> response {
        link::HANDLER_STATUS_OK,
    };
    constexpr std::array<uint8_t, 14U> expected {
        0xAAU, 0x0EU, 0x04U, 0xA0U, 0x01U, 0x22U, 0x11U,
        0x34U, 0x12U, 0x03U, 0x07U, 0x00U, 0x8DU, 0xFAU,
    };
    link::PackArgs args {};
    args.sender = 0x22U;
    args.receiver = 0x11U;
    args.cmd_set = 0x03U;
    args.cmd_id = 0x07U;
    args.is_ack = true;
    args.seq = 0x1234U;

    std::array<uint8_t, CONFIG_LINK_MAX_FRAME_SIZE> raw {};
    const size_t length = link::FrameCodec::pack(
        raw.data(), raw.size(), args, response.data(), response.size());

    CHECK(length == expected.size());
    CHECK(std::equal(expected.begin(), expected.end(), raw.begin()));
}

void test_corruption_and_bounds()
{
    link::PackArgs args {};
    std::array<uint8_t, CONFIG_LINK_MAX_FRAME_SIZE> raw {};
    CHECK(link::FrameCodec::pack(nullptr, 0U, args, nullptr, 0U) == 0U);
    CHECK(link::FrameCodec::pack(raw.data(), link::MIN_FRAME_SIZE - 1U,
                                 args, nullptr, 0U) == 0U);

    const size_t length = link::FrameCodec::pack(
        raw.data(), raw.size(), args, nullptr, 0U);
    CHECK(length == link::MIN_FRAME_SIZE);
    raw[length - 1U] ^= 0x5AU;
    link::FrameCodec codec;
    CHECK(feed_frame(codec, raw.data(), length) == -3);

    codec.reset();
    raw[3] ^= 1U;
    CHECK(feed_frame(codec, raw.data(), 4U) == -1);
}

void test_protocol_validation()
{
    link::PackArgs args {};
    std::array<uint8_t, CONFIG_LINK_MAX_FRAME_SIZE> raw {};
    const size_t length = link::FrameCodec::pack(
        raw.data(), raw.size(), args, nullptr, 0U);
    CHECK(length == link::MIN_FRAME_SIZE);

    raw[2] |= 0x10U;
    raw[3] = link::bcc(raw.data(), 3U);
    link::FrameCodec codec;
    CHECK(feed_frame(codec, raw.data(), 4U) == -2);

    CHECK(link::FrameCodec::pack(
        raw.data(), raw.size(), args, nullptr, 0U) == length);
    const uint16_t version_two = link::pack_ver_len(
        2U, static_cast<uint16_t>(length));
    raw[1] = static_cast<uint8_t>(version_two & 0xFFU);
    raw[2] = static_cast<uint8_t>(version_two >> 8U);
    raw[3] = link::bcc(raw.data(), 3U);
    codec.reset();
    CHECK(feed_frame(codec, raw.data(), 4U) == -2);

    CHECK(link::FrameCodec::pack(
        raw.data(), raw.size(), args, nullptr, 0U) == length);
    raw[4] |= link::CMD_TYPE_RESERVED_MASK;
    update_crc(raw.data(), length);
    codec.reset();
    CHECK(feed_frame(codec, raw.data(), length) == -4);

    args.ack_mode = static_cast<link::AckMode>(0xFFU);
    CHECK(link::FrameCodec::pack(
        raw.data(), raw.size(), args, nullptr, 0U) == 0U);
}

void test_in_place_payload()
{
    link::PackArgs args {};
    std::array<uint8_t, CONFIG_LINK_MAX_FRAME_SIZE> raw {};
    raw[link::HEADER_SIZE] = 0x12U;
    raw[link::HEADER_SIZE + 1U] = 0x34U;
    const size_t length = link::FrameCodec::pack(
        raw.data(), raw.size(), args, &raw[link::HEADER_SIZE], 2U);
    CHECK(length == link::MIN_FRAME_SIZE + 2U);
    link::FrameCodec codec;
    CHECK(feed_frame(codec, raw.data(), length) == 1);
    CHECK(codec.frame().data_len == 2U);
    CHECK(codec.frame().data[0] == 0x12U);
    CHECK(codec.frame().data[1] == 0x34U);
}

void test_address_contract()
{
    CHECK(!link::is_unicast_addr(link::ADDR_BROADCAST));
    CHECK(!link::is_unicast_addr(link::ADDR_RESERVED));
    CHECK(link::is_unicast_addr(0x01U));
    CHECK(link::is_unicast_addr(0xFEU));
}

} // namespace

int main()
{
    test_round_trip();
    test_v1_wire_compatibility_vector();
    test_v1_legacy_crc_vector();
    test_v1_ack_wire_compatibility_vector();
    test_corruption_and_bounds();
    test_protocol_validation();
    test_in_place_payload();
    test_address_contract();
    std::puts("Link codec tests passed");
    return 0;
}
