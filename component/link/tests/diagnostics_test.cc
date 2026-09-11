#include <link/diagnostics.h>

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

uint16_t get_u16(const uint8_t* input)
{
    return static_cast<uint16_t>(input[0])
        | static_cast<uint16_t>(static_cast<uint16_t>(input[1]) << 8U);
}

uint32_t get_u32(const uint8_t* input)
{
    return static_cast<uint32_t>(input[0])
        | (static_cast<uint32_t>(input[1]) << 8U)
        | (static_cast<uint32_t>(input[2]) << 16U)
        | (static_cast<uint32_t>(input[3]) << 24U);
}

void test_snapshot_encoding_is_versioned_and_little_endian()
{
    link::DiagnosticSnapshot snapshot {};
    snapshot.flags = link::DIAGNOSTICS_FLAG_SECURITY;
    snapshot.high_queue_depth = 2U;
    snapshot.low_queue_depth = 3U;
    snapshot.async_active = 1U;
    snapshot.counters.received_frames = 0x12345678U;
    snapshot.counters.transmit_failures = 9U;
    snapshot.link_count = 1U;
    snapshot.links[0].link_id = 0x3412U;
    snapshot.links[0].is_ready = true;
    snapshot.links[0].process_divider = 5U;
    snapshot.links[0].received_bytes = 100U;
    snapshot.links[0].transmit_failures = 7U;
    std::array<uint8_t, 128U> encoded {};

    const size_t size = link::encode_diagnostics(
        snapshot, encoded.data(), encoded.size());

    CHECK(size == 96U);
    CHECK(encoded[0] == link::DIAGNOSTICS_WIRE_VERSION);
    CHECK(encoded[1] == link::DIAGNOSTICS_FLAG_SECURITY);
    CHECK(encoded[2] == 1U);
    CHECK(encoded[3] == 1U);
    CHECK(get_u16(&encoded[4]) == 2U);
    CHECK(get_u16(&encoded[6]) == 3U);
    CHECK(get_u32(&encoded[12]) == 0x12345678U);
    CHECK(get_u32(&encoded[68]) == 9U);
    CHECK(get_u16(&encoded[72]) == 0x3412U);
    CHECK(encoded[74] == 1U);
    CHECK(encoded[75] == 5U);
    CHECK(get_u32(&encoded[76]) == 100U);
    CHECK(get_u32(&encoded[92]) == 7U);
}

void test_encoder_never_emits_partial_link_record()
{
    link::DiagnosticSnapshot snapshot {};
    snapshot.link_count = 2U;
    std::array<uint8_t, 95U> encoded {};

    const size_t size = link::encode_diagnostics(
        snapshot, encoded.data(), encoded.size());

    CHECK(size == 72U);
    CHECK(encoded[2] == 0U);
    CHECK(encoded[3] == 2U);
    CHECK(link::encode_diagnostics(snapshot, encoded.data(), 71U) == 0U);
    CHECK(link::encode_diagnostics(snapshot, nullptr, encoded.size()) == 0U);
}

} // namespace

int main()
{
    test_snapshot_encoding_is_versioned_and_little_endian();
    test_encoder_never_emits_partial_link_record();
    std::puts("Link diagnostics tests passed");
    return 0;
}
