#include <osal/event_bits.h>
#include <cstdlib>
#include "test_check.h"

int main()
{
    osal::detail::EventBits<2> events;
    uint32_t result = 0U;
    // A arrives first; B publishes the final bit. A clearing the event must
    // not force B to wait for the next generation of the barrier.
    const int a = events.begin(3U, true, true, nullptr, result, 1U, true);
    CHECK(a >= 0);
    CHECK(events.begin(3U, true, true, nullptr, result, 2U, true) == -1);
    CHECK(result == 3U && events.finish(a) == 3U && events.get() == 0U);

    // All matching waiters observe the publication before any clear-on-exit.
    const int first = events.begin(1U, false, true, nullptr, result);
    const int second = events.begin(1U, false, true, nullptr, result);
    CHECK(events.begin(2U, false, false, nullptr, result) == -2);
    events.set(5U);
    unsigned notifications = 0U;
    events.notify([&notifications](void*) { ++notifications; });
    events.notify([&notifications](void*) { ++notifications; });
    CHECK(notifications == 2U);
    CHECK(events.finish(first) == 5U && events.finish(second) == 5U);
    CHECK(events.get() == 4U && events.clear(4U) == 4U && events.get() == 0U);

    const int timed_out = events.begin(3U, true, true, nullptr, result);
    events.set(1U);
    CHECK(events.finish(timed_out) == 1U); // timeout returns current snapshot
    CHECK(events.idle());
    CHECK(!events.valid(0x01000000U));
    return 0;
}
