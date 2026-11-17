#include <runtime/execution.h>
#include <runtime/experimental/async_pool.h>
#include <runtime/output_latch.h>
#include <runtime/wire.h>
#include <drivers/dma_memory.h>
#include <ipc/experimental/timed_topic.h>
#include <cstdlib>

#define CHECK(x) do { if (!(x)) std::abort(); } while (false)

int main()
{
    ipc::experimental::TimedTopic<int> topic;
    ipc::experimental::Sample<int> sample;
    CHECK(topic.read(sample, 0U, 10U) == ipc::experimental::ReadStatus::Empty);
    CHECK(topic.publish({42, 1U, UINT32_MAX - 5U, 1U, true}));
    CHECK(topic.read(sample, 2U, 10U) == ipc::experimental::ReadStatus::Fresh && sample.value == 42);
    CHECK(topic.read(sample, 20U, 10U) == ipc::experimental::ReadStatus::Stale);

    runtime::experimental::AsyncPool<int, 1U> pending;
    unsigned completions = 0U;
    const auto callback = [](void* context, runtime::experimental::RequestToken,
                              runtime::experimental::Completion, const int&) { ++*static_cast<unsigned*>(context); };
    const auto first = pending.acquire(7, UINT32_MAX - 5U, 10U, callback, &completions);
    CHECK(pending.active(first) && !pending.expired(first, 2U) && pending.expired(first, 5U));
    CHECK(!pending.active(pending.acquire(9, 0U, 10U, callback, &completions)));
    CHECK(pending.complete(first, runtime::experimental::Completion::Success));
    const auto second = pending.acquire(9, 10U, 10U, callback, &completions);
    CHECK(!pending.complete(first, runtime::experimental::Completion::Failed));
    pending.cancel_all();
    CHECK(completions == 2U && !pending.active(second));

    runtime::OutputLatch<2U> latch;
    bool enabled = true;
    CHECK(latch.add(&enabled, [](void* p) { *static_cast<bool*>(p) = false; }) >= 0);
    latch.trip(17U); latch.trip(22U);
    CHECK(!enabled && latch.latched() && latch.reason() == 17U);
    CHECK(latch.add(&enabled, [](void*) {}) < 0);

    constexpr runtime::ExecutionSpec spec {"test", runtime::Trigger::External, 100U, 80U, 40U};
    static_assert(spec.valid());
    constexpr auto invalid_trigger = [](runtime::ExecutionSpec invalid) {
        invalid.trigger = static_cast<runtime::Trigger>(255U);
        return invalid;
    }(spec);
    constexpr auto unsupported_overflow = [](runtime::ExecutionSpec invalid) {
        invalid.overflow = static_cast<runtime::Overflow>(1U);
        return invalid;
    }(spec);
    static_assert(!invalid_trigger.valid());
    static_assert(!unsupported_overflow.valid());
    runtime::TimingMetrics metrics;
    metrics.record(spec, UINT32_MAX - 20U, 10U, 65U);
    const auto timing = metrics.snapshot();
    CHECK(timing.count == 1U && timing.max_execution_us == 55U);
    CHECK(timing.deadline_misses == 1U && timing.budget_misses == 1U);
    alignas(32) uint8_t buffer[64] {};
    const hal::DmaRegion region {reinterpret_cast<uintptr_t>(buffer),
                                 reinterpret_cast<uintptr_t>(buffer) + sizeof(buffer), 32U};
    CHECK(region.accepts(buffer, 64U) && !region.accepts(buffer + 1U, 32U));
    CHECK(!region.accepts(buffer, 65U) && !region.accepts(buffer, 33U));
    runtime::WireWriter wire(buffer, 4U);
    CHECK(wire.u32(0x12345678U) && buffer[0] == 0x78U && buffer[3] == 0x12U);
    CHECK(!wire.u8(0U) && wire.used() == 4U);
}
