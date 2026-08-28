#include <sensor_core.h>
#include <irq.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>

namespace {

bool dispatch_notifications = true;

#define TEST_ASSERT(condition)               \
    do {                                     \
        if (!(condition)) {                  \
            std::abort();                    \
        }                                    \
    } while (false)

} // namespace

namespace osal {

PeriodicThread::PeriodicThread() = default;
PeriodicThread::~PeriodicThread() = default;

bool PeriodicThread::start(const PeriodicThreadConfig& config)
{
    if (config.entry == nullptr || config.frequency_hz == 0U) {
        return false;
    }
    entry_ = config.entry;
    param_ = config.context;
    frequency_hz_ = config.frequency_hz;
    stack_size_bytes_ = config.stack_size_bytes;
    started_ = true;
    return true;
}

int PeriodicThread::startup()
{
    running_.store(started_, std::memory_order_release);
    return started_ ? 0 : -1;
}

int PeriodicThread::stop()
{
    running_.store(false, std::memory_order_release);
    return 0;
}

bool PeriodicThread::shutdown(Milliseconds)
{
    destroy();
    return true;
}

void PeriodicThread::destroy()
{
    running_.store(false, std::memory_order_release);
    started_ = false;
    entry_ = nullptr;
    param_ = nullptr;
}

void PeriodicThread::callEntry()
{
    if (entry_ == nullptr) {
        return;
    }
    const PeriodicStats stats {
        sequence_.fetch_add(1U, std::memory_order_relaxed) + 1U,
        missed_.load(std::memory_order_relaxed),
    };
    entry_(param_, stats);
}

int PeriodicThread::notify_from_isr(uint32_t)
{
    if (!running_.load(std::memory_order_acquire)) {
        return -1;
    }
    if (dispatch_notifications) {
        callEntry();
    }
    return 0;
}

StackStats PeriodicThread::stack_stats() const
{
    return {true, stack_size_bytes_, stack_size_bytes_};
}

uint32_t Kernel::uptime_ms()
{
    return 100U;
}

} // namespace osal

namespace hal {

IrqGuard::IrqGuard() = default;
IrqGuard::~IrqGuard() = default;

} // namespace hal

namespace {

struct TestFrame {
    SensorBatchCore::FrameHeader header {};
    uint32_t source_a {0U};
    uint32_t source_b {0U};
};

class FakeTimer final : public osal::IrqTimer {
public:
    bool enable_update_irq(IrqCallback callback, void* argument) override
    {
        callback_ = callback;
        argument_ = argument;
        return true;
    }

    void fire()
    {
        TEST_ASSERT(callback_ != nullptr);
        callback_(argument_);
    }

private:
    IrqCallback callback_ {nullptr};
    void* argument_ {nullptr};
};

class FakeSource {
public:
    static bool prepare(
        void* context, SensorBatchCore::CompletionFn completion,
        void* completion_argument)
    {
        auto* self = static_cast<FakeSource*>(context);
        self->completion_ = completion;
        self->completion_argument_ = completion_argument;
        return true;
    }

    static bool start(void* context, void* destination)
    {
        auto* self = static_cast<FakeSource*>(context);
        TEST_ASSERT(self->pending_ == nullptr);
        self->pending_ = static_cast<uint32_t*>(destination);
        *self->pending_ = self->next_value_++;
        return true;
    }

    static void stop(void* context)
    {
        auto* self = static_cast<FakeSource*>(context);
        self->pending_ = nullptr;
        self->completion_ = nullptr;
        self->completion_argument_ = nullptr;
    }

    void complete(SensorBatchCore::SourceResult result)
    {
        TEST_ASSERT(pending_ != nullptr);
        pending_ = nullptr;
        completion_(completion_argument_, result);
    }

private:
    SensorBatchCore::CompletionFn completion_ {nullptr};
    void* completion_argument_ {nullptr};
    uint32_t* pending_ {nullptr};
    uint32_t next_value_ {1U};
};

struct BatchCapture {
    std::array<TestFrame, 2U> frames {};
    const void* source_pointer {nullptr};
    size_t calls {0U};

    static void consume(
        void* context, const SensorBatchCore::BatchView& batch)
    {
        auto* self = static_cast<BatchCapture*>(context);
        TEST_ASSERT(batch.frame_count == self->frames.size());
        TEST_ASSERT(batch.frame_size == sizeof(TestFrame));
        const auto* source = static_cast<const TestFrame*>(batch.frames);
        self->source_pointer = batch.frames;
        self->frames[0] = source[0];
        self->frames[1] = source[1];
        self->calls++;
    }
};

struct CoreFixture {
    FakeTimer timer {};
    FakeSource source_a {};
    FakeSource source_b {};
    BatchCapture capture {};
    std::array<TestFrame, 2U> buffer_a {};
    std::array<TestFrame, 2U> buffer_b {};
    alignas(std::max_align_t) std::array<std::byte, 1024U> stack {};
    SensorBatchCore core {};

    SensorBatchCore::Config config()
    {
        SensorBatchCore::Config result {};
        result.timer = &timer;
        result.entry = BatchCapture::consume;
        result.entry_context = &capture;
        result.source_count = 2U;
        result.sources[0] = {
            FakeSource::prepare, FakeSource::start, FakeSource::stop,
            &source_a, offsetof(TestFrame, source_a), sizeof(uint32_t),
        };
        result.sources[1] = {
            FakeSource::prepare, FakeSource::start, FakeSource::stop,
            &source_b, offsetof(TestFrame, source_b), sizeof(uint32_t),
        };
        result.buffers = {buffer_a.data(), buffer_b.data()};
        result.frame_size = sizeof(TestFrame);
        result.buffer_capacity = buffer_a.size();
        result.buffer_bytes = sizeof(buffer_a);
        result.sample_frequency_hz = 4U;
        result.consumer_frequency_hz = 2U;
        result.stack_buffer = stack.data();
        result.stack_size = stack.size();
        return result;
    }

    void complete_frame(SensorBatchCore::SourceResult source_b_result)
    {
        timer.fire();
        source_a.complete(SensorBatchCore::SourceResult::Success);
        source_b.complete(source_b_result);
    }
};

void test_multi_source_batch_and_pointer_delivery()
{
    CoreFixture fixture;
    TEST_ASSERT(fixture.core.configure(fixture.config()));
    TEST_ASSERT(fixture.core.start() == 0);

    fixture.complete_frame(SensorBatchCore::SourceResult::Success);
    fixture.complete_frame(SensorBatchCore::SourceResult::Success);

    TEST_ASSERT(fixture.capture.calls == 1U);
    TEST_ASSERT(fixture.capture.source_pointer == fixture.buffer_a.data());
    TEST_ASSERT(fixture.capture.frames[0].header.sequence == 1U);
    TEST_ASSERT(fixture.capture.frames[1].header.sequence == 2U);
    TEST_ASSERT(
        fixture.capture.frames[0].header.capture_time_us == 100000U);
    TEST_ASSERT(
        fixture.capture.frames[1].header.capture_time_us == 350000U);
    TEST_ASSERT(fixture.capture.frames[0].header.valid_sources == 0x03U);
    TEST_ASSERT(fixture.capture.frames[1].header.valid_sources == 0x03U);
    TEST_ASSERT(fixture.core.stop() == 0);
}

void test_overrun_and_source_error_diagnostics()
{
    CoreFixture fixture;
    TEST_ASSERT(fixture.core.configure(fixture.config()));
    TEST_ASSERT(fixture.core.start() == 0);

    fixture.timer.fire();
    fixture.timer.fire();
    fixture.source_a.complete(SensorBatchCore::SourceResult::Success);
    fixture.source_b.complete(SensorBatchCore::SourceResult::Failure);
    fixture.complete_frame(SensorBatchCore::SourceResult::Success);
    SensorBatchCore::Diagnostics diagnostics =
        fixture.core.diagnostics();
    TEST_ASSERT(diagnostics.trigger_overrun_count == 1U);
    TEST_ASSERT(diagnostics.source_error_count == 1U);

    dispatch_notifications = false;
    for (size_t frame = 0U; frame < 4U; ++frame) {
        fixture.complete_frame(SensorBatchCore::SourceResult::Success);
    }
    diagnostics = fixture.core.diagnostics();
    TEST_ASSERT(diagnostics.batch_overrun_count == 1U);
    TEST_ASSERT(fixture.core.stop() == 0);
    dispatch_notifications = true;
}

} // namespace

int main()
{
    test_multi_source_batch_and_pointer_delivery();
    test_overrun_and_source_error_diagnostics();
    return 0;
}
