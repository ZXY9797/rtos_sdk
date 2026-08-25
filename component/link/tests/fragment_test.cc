#include <link/fragment.h>

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <vector>

#define CHECK(condition)                                                     \
    do {                                                                     \
        if (!(condition)) {                                                  \
            std::fprintf(stderr, "CHECK failed at %s:%d: %s\n",           \
                         __FILE__, __LINE__, #condition);                    \
            std::abort();                                                    \
        }                                                                    \
    } while (false)

namespace {

using Packet = std::vector<uint8_t>;

void test_streaming_read()
{
    std::array<uint8_t, 200> payload {};
    for (size_t i = 0U; i < payload.size(); ++i) {
        payload[i] = static_cast<uint8_t>(i);
    }

    std::vector<Packet> packets;
    const int sent = link::Fragmenter::send<20U>(
        payload.data(), payload.size(), 20U,
        [&packets](const uint8_t *data, size_t len) {
            packets.emplace_back(data, data + len);
            return true;
        });
    CHECK(sent == static_cast<int>(payload.size()));

    link::Reassembler reassembler;
    for (const Packet& packet : packets) {
        (void)link::Fragmenter::recv(
            packet.data(), packet.size(), reassembler);
    }

    std::array<uint8_t, 200> output {};
    for (size_t i = 0U; i < output.size(); ++i) {
        CHECK(reassembler.read(&output[i], 1U) == 1);
    }
    CHECK(reassembler.read(output.data(), output.size()) == 0);
    CHECK(output == payload);
}

void test_out_of_order_is_discarded()
{
    link::Reassembler reassembler;
    const uint8_t second[] = {0x81U, 0x22U};
    CHECK(!link::Fragmenter::recv(second, sizeof(second), reassembler));
    uint8_t output = 0U;
    CHECK(reassembler.read(&output, 1U) == 0);

    const uint8_t complete[] = {0x80U, 0x33U};
    CHECK(link::Fragmenter::recv(complete, sizeof(complete), reassembler));
    CHECK(reassembler.read(&output, 1U) == 1);
    CHECK(output == 0x33U);
}

void test_fragment_index_cannot_wrap()
{
    link::Reassembler reassembler;
    const uint8_t payload = 0x5AU;
    for (uint16_t index = 0U; index < 127U; ++index) {
        CHECK(reassembler.append_fragment(
            static_cast<uint8_t>(index), &payload, 1U, false));
    }
    CHECK(!reassembler.append_fragment(127U, &payload, 1U, false));
    CHECK(reassembler.append_fragment(0U, &payload, 1U, true));
    uint8_t output = 0U;
    CHECK(reassembler.read(&output, 1U) == 1);
    CHECK(output == payload);
}

} // namespace

int main()
{
    test_streaming_read();
    test_out_of_order_is_discarded();
    test_fragment_index_cannot_wrap();
    std::puts("Link fragmentation tests passed");
    return 0;
}
