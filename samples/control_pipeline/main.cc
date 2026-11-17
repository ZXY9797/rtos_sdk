#include <algo/pid_controller.h>
#include <ipc/experimental/timed_topic.h>
#include <runtime/experimental/async_pool.h>
#include <runtime/execution.h>
#include <runtime/output_latch.h>
#include <runtime/wire.h>
#include <link/codec.h>
#include <link/experimental/typed_command.h>
#include <chrono>
#include <cstdlib>
#include <fstream>

#define CHECK(x) do { if (!(x)) std::abort(); } while (false)

struct VoltageSchema {
    struct Request { uint32_t millivolts {0U}; };
    struct Response { uint32_t applied {0U}; };
    static constexpr uint8_t version = 1U, command_set = 0x70U, command_id = 1U;
    static size_t encode_request(const Request& request, uint8_t* out, size_t size) {
        runtime::WireWriter writer(out, size);
        return request.millivolts <= 12000U && writer.u32(request.millivolts) ? 4U : 0U;
    }
    static bool decode_request(const uint8_t* in, size_t size, Request& request) {
        if (size != 4U) return false;
        request.millivolts = static_cast<uint32_t>(in[0]) | static_cast<uint32_t>(in[1]) << 8U
            | static_cast<uint32_t>(in[2]) << 16U | static_cast<uint32_t>(in[3]) << 24U;
        return request.millivolts <= 12000U;
    }
    static size_t encode_response(const Response& value, uint8_t* out, size_t size) {
        return encode_request({value.applied}, out, size);
    }
    static bool decode_response(const uint8_t* in, size_t size, Response& response) {
        Request value;
        if (!decode_request(in, size, value)) return false;
        response.applied = value.millivolts;
        return true;
    }
};

uint32_t now_us() {
    return static_cast<uint32_t>(std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
}

int main(int argc, char** argv)
{
    ipc::experimental::TimedTopic<float> measurements;
    runtime::experimental::AsyncPool<VoltageSchema::Request, 2U> operations;
    runtime::TimingMetrics timing;
    constexpr runtime::ExecutionSpec spec {"host_pipeline", runtime::Trigger::Periodic,
                                          1000U, 1000U, 800U};
    PidController controller;
    controller.init({2.0F, 0.1F, 0.0F, 0.001F, 5.0F, 12.0F});
    float plant = 0.0F;
    unsigned completed = 0U;
    for (uint32_t i = 1U; i <= 1000U; ++i) {
        const uint32_t captured = now_us();
        CHECK(measurements.publish({plant, i, captured, 1U, true}));
        ipc::experimental::Sample<float> sample;
        CHECK(measurements.read(sample, now_us(), 100000U) == ipc::experimental::ReadStatus::Fresh);
        const uint32_t started = now_us();
        const float output = controller.update(1.0F, sample.value, 0.001F);
        VoltageSchema::Request demand {static_cast<uint32_t>((output > 0.0F ? output : 0.0F) * 1000.0F)};
        uint8_t payload[8] {}, raw[64] {};
        const size_t payload_size = link::experimental::TypedCommand<VoltageSchema>::encode_request(demand, payload, sizeof(payload));
        CHECK(payload_size == 5U);
        const link::PackArgs args {0x11U, 0x12U, VoltageSchema::command_set, VoltageSchema::command_id};
        const size_t size = link::FrameCodec::pack(raw, sizeof(raw), args, payload, payload_size);
        CHECK(size > 0U);
        link::FrameCodec codec;
        int decoded = 0;
        for (size_t byte = 0U; byte < size; ++byte) decoded = codec.feed(raw[byte]);
        CHECK(decoded > 0);
        VoltageSchema::Request received;
        CHECK(link::experimental::TypedCommand<VoltageSchema>::decode_request(codec.frame(), received));
        const auto token = operations.acquire(received, captured, 1000U,
            [](void* context, runtime::experimental::RequestToken, runtime::experimental::Completion result,
               const VoltageSchema::Request&) {
                CHECK(result == runtime::experimental::Completion::Success);
                ++*static_cast<unsigned*>(context);
            }, &completed);
        CHECK(operations.complete(token, runtime::experimental::Completion::Success));
        uint8_t reply[8] {};
        reply[0] = link::HANDLER_STATUS_OK;
        CHECK(link::experimental::TypedCommand<VoltageSchema>::encode_response({received.millivolts},
            reply + 1U, sizeof(reply) - 1U) == 5U);
        link::Frame answer;
        answer.cmd_type = link::pack_cmd_type(true, link::AckMode::Finish,
            link::EncMode::None, link::Priority::Low, false);
        answer.cmd_set = VoltageSchema::command_set;
        answer.cmd_id = VoltageSchema::command_id;
        answer.data = reply; answer.data_len = 6U;
        VoltageSchema::Response applied;
        CHECK(link::experimental::TypedCommand<VoltageSchema>::decode_response(answer, applied));
        CHECK(applied.applied == received.millivolts);
        CHECK(!link::experimental::TypedCommand<VoltageSchema>::decode_request(answer, received));
        reply[1] = 99U;
        CHECK(!link::experimental::TypedCommand<VoltageSchema>::decode_response(answer, applied));
        plant += (static_cast<float>(received.millivolts) / 1000.0F - plant) * 0.01F;
        timing.record(spec, captured, started, now_us());
    }
    CHECK(completed == 1000U);
    runtime::OutputLatch<1U> safety;
    bool output_enabled = true;
    CHECK(safety.add(&output_enabled, [](void* p) { *static_cast<bool*>(p) = false; }) >= 0);
    safety.trip(1U);
    CHECK(!output_enabled && safety.latched());
    const auto stats = timing.snapshot();
    std::ofstream report(argc > 1 ? argv[1] : "benchmark.json");
    report << "{\"environment\":\"host simulation; no board timing proof\","
           << "\"compiler\":\"" HOST_COMPILER "\",\"samples\":" << stats.count
           << ",\"max_execution_us\":" << stats.max_execution_us
           << ",\"max_latency_us\":" << stats.max_latency_us
           << ",\"deadline_misses\":" << stats.deadline_misses
           << ",\"budget_misses\":" << stats.budget_misses << "}\n";
    CHECK(report.good());
}
