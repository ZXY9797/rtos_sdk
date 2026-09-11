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
    CHECK(cache.remember(frame, true, 0x5AU, nullptr, 0U, 10U,
                         retention_ms));
    const link::ReplayResult duplicate =
        cache.classify(frame, 11U, retention_ms);
    CHECK(duplicate.decision == link::ReplayDecision::Duplicate);
    CHECK(duplicate.response_available);
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
    CHECK(cache.remember(first, true, 0U, nullptr, 0U, 20U,
                         retention_ms));

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
    CHECK(cache.remember(frame, true, 0U, nullptr, 0U, UINT32_MAX - 20U,
                         retention_ms));

    CHECK(cache.classify(frame, 5U, retention_ms).decision
          == link::ReplayDecision::Duplicate);
    CHECK(cache.classify(frame, 2000U, retention_ms).decision
          == link::ReplayDecision::NewRequest);
}

void test_reused_sequence_replaces_expired_payload()
{
    constexpr uint32_t retention_ms = 1000U;
    const std::array<uint8_t, 1U> first_payload {9U};
    const std::array<uint8_t, 1U> second_payload {8U};
    const link::Frame first = make_frame(
        first_payload.data(), first_payload.size());
    const link::Frame second = make_frame(
        second_payload.data(), second_payload.size());
    link::ReplayCache cache;
    CHECK(cache.remember(first, true, 0U, nullptr, 0U, 10U,
                         retention_ms));

    CHECK(cache.classify(second, 1010U, retention_ms).decision
          == link::ReplayDecision::NewRequest);
    CHECK(cache.remember(second, false, 0U, nullptr, 0U, 1010U,
                         retention_ms));
    CHECK(cache.classify(first, 1011U, retention_ms).decision
          == link::ReplayDecision::Conflict);
    CHECK(cache.classify(second, 1011U, retention_ms).decision
          == link::ReplayDecision::Duplicate);
}

void test_in_progress_request_is_not_replied_until_response_exists()
{
    constexpr uint32_t retention_ms = 1000U;
    const std::array<uint8_t, 1U> payload {4U};
    const link::Frame frame = make_frame(payload.data(), payload.size());
    link::ReplayCache cache;
    CHECK(cache.remember(frame, false, link::HANDLER_STATUS_OK, nullptr, 0U,
                         30U, retention_ms));

    link::ReplayResult replay = cache.classify(frame, 31U, retention_ms);
    CHECK(replay.decision == link::ReplayDecision::Duplicate);
    CHECK(!replay.response_available);

    link::CompletionToken token {};
    token.sender = frame.sender_id;
    token.receiver = frame.receiver_id;
    token.sequence = frame.seq;
    token.command_set = frame.cmd_set;
    token.command_id = frame.cmd_id;
    const std::array<uint8_t, 2U> response {8U, 9U};
    CHECK(cache.update_response(token, 0x42U, response.data(),
                                response.size(), 32U));
    replay = cache.classify(frame, 33U, retention_ms);
    CHECK(replay.response_available);
    CHECK(replay.ack_status == 0x42U);
    std::array<uint8_t, 2U> copied {};
    size_t copied_size = 0U;
    CHECK(cache.copy_response(frame, copied.data(), copied.size(),
                              copied_size));
    CHECK(copied_size == response.size());
    CHECK(copied == response);
}

void test_unexpired_entries_are_never_evicted()
{
    constexpr uint32_t retention_ms = 1000U;
    link::ReplayCache cache;
    for (size_t index = 0U;
         index < CONFIG_LINK_REPLAY_CACHE_SIZE; ++index) {
        link::Frame frame = make_frame(nullptr, 0U);
        frame.seq = static_cast<uint16_t>(index);
        CHECK(cache.remember(frame, true, link::HANDLER_STATUS_OK,
                             nullptr, 0U, 10U,
                             retention_ms));
    }

    link::Frame extra = make_frame(nullptr, 0U);
    extra.seq = static_cast<uint16_t>(CONFIG_LINK_REPLAY_CACHE_SIZE);
    CHECK(!cache.remember(extra, true, link::HANDLER_STATUS_OK,
                          nullptr, 0U, 20U,
                          retention_ms));
    CHECK(cache.remember(extra, true, link::HANDLER_STATUS_OK,
                         nullptr, 0U, 1010U,
                         retention_ms));
}

} // namespace

int main()
{
    test_duplicate_replays_previous_status();
    test_sequence_collision_is_rejected();
    test_sequence_can_be_reused_after_retention();
    test_reused_sequence_replaces_expired_payload();
    test_in_progress_request_is_not_replied_until_response_exists();
    test_unexpired_entries_are_never_evicted();
    std::puts("Link replay-cache tests passed");
    return 0;
}
