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

uint8_t reference_crc8(const uint8_t* data, size_t length)
{
    uint8_t crc = 0U;
    for (size_t index = 0U; index < length; ++index) {
        crc ^= data[index];
        for (uint8_t bit = 0U; bit < 8U; ++bit) {
            if ((crc & 0x80U) != 0U) {
                crc = static_cast<uint8_t>((crc << 1U) ^ 0x07U);
            } else {
                crc = static_cast<uint8_t>(crc << 1U);
            }
        }
    }
    return crc;
}

uint16_t reference_crc16(const uint8_t* data, size_t length)
{
    uint16_t crc = 0xFFFFU;
    for (size_t index = 0U; index < length; ++index) {
        crc ^= static_cast<uint16_t>(data[index]) << 8U;
        for (uint8_t bit = 0U; bit < 8U; ++bit) {
            if ((crc & 0x8000U) != 0U) {
                crc = static_cast<uint16_t>((crc << 1U) ^ 0x1021U);
            } else {
                crc = static_cast<uint16_t>(crc << 1U);
            }
        }
    }
    return crc;
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

void test_v1_wire_vector()
{
    static_assert(link::PROTOCOL_VERSION == 1U);
    static_assert(link::HEADER_SIZE == 11U);
    static_assert(link::CRC_SIZE == 2U);

    constexpr std::array<uint8_t, 5U> payload {
        1U, 2U, 3U, 4U, 5U,
    };
    constexpr std::array<uint8_t, 18U> expected {
        0xAAU, 0x12U, 0x04U, 0xAEU, 0x24U, 0x11U,
        0x22U, 0x34U, 0x12U, 0x03U, 0x07U, 0x01U,
        0x02U, 0x03U, 0x04U, 0x05U, 0x3FU, 0xD6U,
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

void test_standard_crc_vectors()
{
    constexpr std::array<uint8_t, 9U> check {
        '1', '2', '3', '4', '5', '6', '7', '8', '9',
    };
    CHECK(link::crc8_smbus(check.data(), check.size()) == 0xF4U);
    CHECK(link::crc16_ccitt(check.data(), check.size()) == 0x29B1U);
    CHECK(link::crc8_smbus(nullptr, 0U) == 0U);
    CHECK(link::crc16_ccitt(nullptr, 0U) == 0xFFFFU);

    // Exercise every byte value and every two-byte sequence against the
    // independent bit-at-a-time references, including all table transitions.
    std::array<uint8_t, 2U> input {};
    for (uint32_t value = 0U; value <= UINT16_MAX; ++value) {
        input[0] = static_cast<uint8_t>(value >> 8U);
        input[1] = static_cast<uint8_t>(value);
        CHECK(link::crc8_smbus(input.data(), input.size())
              == reference_crc8(input.data(), input.size()));
        CHECK(link::crc16_ccitt(input.data(), input.size())
              == reference_crc16(input.data(), input.size()));
    }
}

void test_v1_ack_wire_vector()
{
    constexpr std::array<uint8_t, 1U> response {
        link::HANDLER_STATUS_OK,
    };
    constexpr std::array<uint8_t, 14U> expected {
        0xAAU, 0x0EU, 0x04U, 0x05U, 0x01U, 0x22U, 0x11U,
        0x34U, 0x12U, 0x03U, 0x07U, 0x00U, 0x3BU, 0x71U,
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

    link::FrameCodec codec;
    for (uint8_t reserved : {0x40U, 0x80U}) {
        CHECK(link::FrameCodec::pack(
            raw.data(), raw.size(), args, nullptr, 0U) == length);
        raw[2] |= reserved;
        raw[3] = link::crc8_smbus(raw.data(), 3U);
        codec.reset();
        CHECK(feed_frame(codec, raw.data(), 4U) == -2);
    }

    CHECK(link::FrameCodec::pack(
        raw.data(), raw.size(), args, nullptr, 0U) == length);
    for (uint8_t version = 0U; version <= link::VER_LEN_VER_MASK; ++version) {
        if (version == link::PROTOCOL_VERSION) {
            continue;
        }
        const uint16_t ver_len = link::pack_ver_len(
            version, static_cast<uint16_t>(length));
        raw[1] = static_cast<uint8_t>(ver_len);
        raw[2] = static_cast<uint8_t>(ver_len >> 8U);
        raw[3] = link::crc8_smbus(raw.data(), 3U);
        codec.reset();
        CHECK(feed_frame(codec, raw.data(), 4U) == -2);
    }

    for (uint16_t invalid_length : {
             static_cast<uint16_t>(link::MIN_FRAME_SIZE - 1U),
             static_cast<uint16_t>(CONFIG_LINK_MAX_FRAME_SIZE + 1U)}) {
        const uint16_t ver_len = link::pack_ver_len(
            link::PROTOCOL_VERSION, invalid_length);
        raw[1] = static_cast<uint8_t>(ver_len);
        raw[2] = static_cast<uint8_t>(ver_len >> 8U);
        raw[3] = link::crc8_smbus(raw.data(), 3U);
        codec.reset();
        CHECK(feed_frame(codec, raw.data(), 4U) == -2);
    }

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

void test_version_and_length_fields()
{
    constexpr std::array<uint16_t, 4U> lengths {13U, 255U, 256U, 1023U};
    for (uint8_t version = 0U; version <= 15U; ++version) {
        for (uint16_t length : lengths) {
            const uint16_t word = link::pack_ver_len(version, length);
            CHECK(link::unpack_ver(word) == version);
            CHECK(link::unpack_len(word) == length);
            CHECK((word & link::VER_LEN_RESERVED_MASK) == 0U);
        }
    }
    CHECK(link::pack_ver_len(15U, 1023U) == 0x3FFFU);
    CHECK(link::pack_ver_len(16U, 13U) == 0U);
    CHECK(link::pack_ver_len(1U, 1024U) == 0U);
}

void test_maximum_frame()
{
    constexpr size_t capacity =
        CONFIG_LINK_MAX_FRAME_SIZE - link::MIN_FRAME_SIZE;
    std::array<uint8_t, capacity + 1U> payload {};
    for (size_t index = 0U; index < payload.size(); ++index) {
        payload[index] = static_cast<uint8_t>(index);
    }
    std::array<uint8_t, CONFIG_LINK_MAX_FRAME_SIZE> raw {};
    link::PackArgs args {};
    const size_t length = link::FrameCodec::pack(
        raw.data(), raw.size(), args, payload.data(), capacity);
    CHECK(length == raw.size());
    CHECK(raw[3] == reference_crc8(raw.data(), 3U));
    const uint16_t expected = reference_crc16(
        raw.data(), length - link::CRC_SIZE);
    CHECK(raw[length - 2U] == static_cast<uint8_t>(expected));
    CHECK(raw[length - 1U] == static_cast<uint8_t>(expected >> 8U));

    link::FrameCodec codec;
    CHECK(feed_frame(codec, raw.data(), length) == 1);
    CHECK(codec.frame().data_len == capacity);
    CHECK(std::equal(payload.begin(), payload.begin() + capacity,
                     codec.frame().data));
    CHECK(link::FrameCodec::pack(raw.data(), raw.size(), args,
                                 payload.data(), capacity + 1U) == 0U);
}

void test_header_crc_detects_xor_cancellation()
{
    // Both prefixes have XOR 0x8E, although their v1 lengths are 32 and 289.
    constexpr std::array<uint8_t, 3U> first {0xAAU, 0x20U, 0x04U};
    constexpr std::array<uint8_t, 3U> changed {0xAAU, 0x21U, 0x05U};
    CHECK(link::crc8_smbus(first.data(), first.size()) == 0x7DU);
    CHECK(link::crc8_smbus(changed.data(), changed.size()) == 0x6FU);
    link::FrameCodec codec;
    CHECK(feed_frame(codec, changed.data(), changed.size()) == 0);
    CHECK(codec.feed(0x7DU) == -1);

    // Check every one- and two-bit corruption of the three-byte prefix and
    // its CRC8 byte, including errors in the check byte itself.
    constexpr std::array<uint8_t, 4U> valid {0xAAU, 0x20U, 0x04U, 0x7DU};
    for (size_t bit = 0U; bit < valid.size() * 8U; ++bit) {
        auto raw = valid;
        raw[bit / 8U] ^= static_cast<uint8_t>(1U << (bit % 8U));
        CHECK(link::crc8_smbus(raw.data(), 3U) != raw[3]);
        for (size_t other = bit + 1U; other < valid.size() * 8U; ++other) {
            auto pair = raw;
            pair[other / 8U] ^= static_cast<uint8_t>(1U << (other % 8U));
            CHECK(link::crc8_smbus(pair.data(), 3U) != pair[3]);
        }
    }
}

void test_legacy_frames_are_rejected()
{
    std::array<uint8_t, 18U> legacy {
        0xAAU, 0x12U, 0x04U, 0xBCU, 0x24U, 0x11U,
        0x22U, 0x34U, 0x12U, 0x03U, 0x07U, 0x01U,
        0x02U, 0x03U, 0x04U, 0x05U, 0x7BU, 0x31U,
    };
    link::FrameCodec codec;
    CHECK(feed_frame(codec, legacy.data(), legacy.size()) == -1);

    // A new header alone must not enable a fallback to the historical CRC16.
    legacy[3] = link::crc8_smbus(legacy.data(), 3U);
    codec.reset();
    CHECK(feed_frame(codec, legacy.data(), legacy.size()) == -3);
}

void test_frame_bit_errors_and_recovery()
{
    constexpr std::array<uint8_t, 5U> payload {1U, 2U, 3U, 4U, 5U};
    std::array<uint8_t, 18U> valid {};
    link::PackArgs args {};
    CHECK(link::FrameCodec::pack(valid.data(), valid.size(), args,
                                 payload.data(), payload.size()) == valid.size());
    for (size_t bit = 0U; bit < valid.size() * 8U; ++bit) {
        auto raw = valid;
        raw[bit / 8U] ^= static_cast<uint8_t>(1U << (bit % 8U));
        link::FrameCodec single_codec;
        CHECK(feed_frame(single_codec, raw.data(), raw.size()) != 1);
        for (size_t other = bit + 1U; other < valid.size() * 8U; ++other) {
            auto pair = raw;
            pair[other / 8U] ^= static_cast<uint8_t>(1U << (other % 8U));
            link::FrameCodec pair_codec;
            CHECK(feed_frame(pair_codec, pair.data(), pair.size()) != 1);
        }
    }

    link::FrameCodec codec;
    auto corrupted = valid;
    corrupted.back() ^= 1U;
    CHECK(feed_frame(codec, corrupted.data(), corrupted.size()) == -3);
    CHECK(feed_frame(codec, valid.data(), valid.size()) == 1);
    CHECK(feed_frame(codec, valid.data(), valid.size()) == 1);

    CHECK(feed_frame(codec, valid.data(), 3U) == 0);
    codec.reset();
    CHECK(feed_frame(codec, valid.data(), valid.size()) == 1);
}

} // namespace

int main()
{
    test_round_trip();
    test_v1_wire_vector();
    test_standard_crc_vectors();
    test_v1_ack_wire_vector();
    test_corruption_and_bounds();
    test_protocol_validation();
    test_in_place_payload();
    test_address_contract();
    test_version_and_length_fields();
    test_maximum_frame();
    test_header_crc_detects_xor_cancellation();
    test_legacy_frames_are_rejected();
    test_frame_bit_errors_and_recovery();
    std::puts("Link codec tests passed");
    return 0;
}
