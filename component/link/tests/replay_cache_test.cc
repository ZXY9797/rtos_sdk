#include <link/replay_cache.h>

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

link::Frame make_frame(const uint8_t* data, uint16_t data_size)
{
    link::Frame frame {};
    frame.sender_id = 0x11U;
    frame.receiver_id = 0x22U;
    frame.seq = 0x1234U;
    frame.cmd_set = 3U;
    frame.cmd_id = 7U;
    frame.data = data;
    frame.data_len = data_size;
    return frame;
}

void test_duplicate_replays_previous_status()
{
    constexpr uint32_t retention_ms = 1000U;
    const std::array<uint8_t, 3U> payload {1U, 2U, 3U};
    const link::Frame frame = make_frame(payload.data(), payload.size());
    link::ReplayCache cache;

    CHECK(cache.classify(frame, 10U, retention_ms).decision
          == link::ReplayDecision::NewRequest);
    cache.remember(frame, 0x5AU, 10U);
    const link::ReplayResult duplicate =
        cache.classify(frame, 11U, retention_ms);
    CHECK(duplicate.decision == link::ReplayDecision::Duplicate);
    CHECK(duplicate.ack_status == 0x5AU);
}

void test_sequence_collision_is_rejected()
{
    constexpr uint32_t retention_ms = 1000U;
    const std::array<uint8_t, 2U> first_payload {1U, 2U};
    const std::array<uint8_t, 2U> changed_payload {1U, 3U};
    const link::Frame first = make_frame(
        first_payload.data(), first_payload.size());
    link::Frame changed = make_frame(
        changed_payload.data(), changed_payload.size());
    link::ReplayCache cache;
    cache.remember(first, 0U, 20U);

    CHECK(cache.classify(changed, 21U, retention_ms).decision
          == link::ReplayDecision::Conflict);
    changed.cmd_id++;
    CHECK(cache.classify(changed, 22U, retention_ms).decision
          == link::ReplayDecision::Conflict);
    changed = first;
    changed.cmd_type = link::pack_cmd_type(
        false, link::AckMode::Finish, link::EncMode::None,
        link::Priority::Low, false);
    CHECK(cache.classify(changed, 23U, retention_ms).decision
          == link::ReplayDecision::Conflict);
    changed.cmd_type = first.cmd_type | static_cast<uint8_t>(
        link::CMD_TYPE_RETRANSMIT_MASK
        << link::CMD_TYPE_RETRANSMIT_SHIFT);
    CHECK(cache.classify(changed, 24U, retention_ms).decision
          == link::ReplayDecision::Duplicate);
}

void test_sequence_can_be_reused_after_retention()
{
    constexpr uint32_t retention_ms = 1000U;
    const std::array<uint8_t, 1U> payload {9U};
    const link::Frame frame = make_frame(payload.data(), payload.size());
    link::ReplayCache cache;
    cache.remember(frame, 0U, UINT32_MAX - 20U);

    CHECK(cache.classify(frame, 5U, retention_ms).decision
          == link::ReplayDecision::Duplicate);
    CHECK(cache.classify(frame, 2000U, retention_ms).decision
          == link::ReplayDecision::NewRequest);
}

} // namespace

int main()
{
    test_duplicate_replays_previous_status();
    test_sequence_collision_is_rejected();
    test_sequence_can_be_reused_after_retention();
    std::puts("Link replay-cache tests passed");
    return 0;
}
