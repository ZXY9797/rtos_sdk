#include <link/router.h>

#include <cstdio>
#include <cstdlib>
#include <type_traits>

#define CHECK(condition)                                                     \
    do {                                                                     \
        if (!(condition)) {                                                  \
            std::fprintf(stderr, "CHECK failed at %s:%d: %s\n",           \
                         __FILE__, __LINE__, #condition);                    \
            std::abort();                                                    \
        }                                                                    \
    } while (false)

namespace {

class TestLink final : public link::Link {
public:
    int send(const uint8_t*, size_t len) override
    {
        return static_cast<int>(len);
    }
    int recv(uint8_t*, size_t) override { return 0; }
    bool is_ready() const override { return true; }
};

void noop(const link::Frame&, void*)
{
}

void legacy_timeout(uint16_t, uint8_t, uint8_t, uint8_t, void*)
{
}

void extended_timeout(uint16_t, uint8_t, uint8_t, uint8_t,
                      link::AckMode, void*)
{
}

static_assert(std::is_same_v<decltype(&legacy_timeout),
                             link::Router::TimeoutCallback>);
static_assert(std::is_same_v<decltype(&extended_timeout),
                             link::Router::TimeoutCallbackEx>);

link::Handler make_handler(uint8_t receiver,
                           uint8_t receiver_mask,
                           uint8_t command_id,
                           uint8_t command_id_mask)
{
    return {
        receiver,
        receiver_mask,
        0x20U,
        0xFFU,
        command_id,
        command_id_mask,
        noop,
        nullptr,
        nullptr,
        0U,
        0U,
    };
}

void test_handler_matching_prefers_more_specific_mask()
{
    const link::Handler family = make_handler(0U, 0U, 0x10U, 0xF0U);
    const link::Handler exact = make_handler(0x21U, 0xFFU, 0x12U, 0xFFU);
    link::Frame frame {};
    frame.receiver_id = 0x21U;
    frame.cmd_set = 0x20U;
    frame.cmd_id = 0x12U;

    CHECK(link::handler_matches(family, frame));
    CHECK(link::handler_matches(exact, frame));
    CHECK(link::handler_specificity(exact)
          > link::handler_specificity(family));
    CHECK(!link::handlers_ambiguous(family, exact));
    CHECK(link::handlers_ambiguous(
        exact, make_handler(0x21U, 0xFFU, 0x12U, 0xFFU)));
}

void test_route_scores_select_static_longest_match()
{
    const link::RouteEntry fallback =
        link::make_route(link::route_direct().to(1U));
    const link::RouteEntry host =
        link::make_route(link::route_by_host(0x20U, 0xF0U).to(2U));
    const link::RouteEntry endpoint =
        link::make_route(link::route_by_host_idx(0x23U).to(3U));

    CHECK(link::route_match_score(fallback, 0x23U, 0U) == 0);
    CHECK(link::route_match_score(host, 0x23U, 0U)
          < link::route_match_score(endpoint, 0x23U, 0U));
    CHECK(link::route_match_score(host, 0x31U, 0U) < 0);
    CHECK(!link::route_entries_ambiguous(host, endpoint));
    CHECK(link::route_entries_ambiguous(
        host, link::make_route(
            link::route_by_host(0x20U, 0xF0U).to(4U))));
}

void test_link_process_divider_rejects_event_mode_without_wakeup()
{
    TestLink physical_link;
    CHECK(physical_link.process_divider() == 1U);
    CHECK(!physical_link.set_process_divider(0U));
    CHECK(physical_link.process_divider() == 1U);
    CHECK(physical_link.set_process_divider(5U));
    CHECK(physical_link.process_divider() == 5U);
}

void test_v1_ack_policy_uses_request_mode_and_status()
{
    using Disposition = link::AckResponseDisposition;

    CHECK(link::classify_ack_response(
        link::AckMode::Now, link::AckMode::No,
        link::HANDLER_STATUS_OK) == Disposition::Terminal);
    CHECK(link::classify_ack_response(
        link::AckMode::Progress, link::AckMode::No,
        link::HANDLER_STATUS_IN_PROGRESS) == Disposition::Accepted);
    CHECK(link::classify_ack_response(
        link::AckMode::Progress, link::AckMode::No,
        link::HANDLER_STATUS_OK) == Disposition::Terminal);
    CHECK(link::classify_ack_response(
        link::AckMode::Finish, link::AckMode::No,
        link::HANDLER_STATUS_NOT_FOUND) == Disposition::Terminal);

    CHECK(link::classify_ack_response(
        link::AckMode::Now, link::AckMode::Finish,
        link::HANDLER_STATUS_OK) == Disposition::Invalid);
    CHECK(link::classify_ack_response(
        link::AckMode::Now, link::AckMode::No,
        link::HANDLER_STATUS_IN_PROGRESS) == Disposition::Invalid);
    CHECK(link::classify_ack_response(
        link::AckMode::Finish, link::AckMode::Progress,
        link::HANDLER_STATUS_OK) == Disposition::Invalid);
    CHECK(link::classify_ack_response(
        link::AckMode::Finish, link::AckMode::No,
        link::HANDLER_STATUS_IN_PROGRESS) == Disposition::Invalid);
    CHECK(link::classify_ack_response(
        link::AckMode::No, link::AckMode::No,
        link::HANDLER_STATUS_OK) == Disposition::Invalid);
}

} // namespace

int main()
{
    test_handler_matching_prefers_more_specific_mask();
    test_route_scores_select_static_longest_match();
    test_link_process_divider_rejects_event_mode_without_wakeup();
    test_v1_ack_policy_uses_request_mode_and_status();
    std::puts("Link router-policy tests passed");
    return 0;
}
