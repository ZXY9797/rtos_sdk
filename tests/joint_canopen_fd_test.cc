#include "comm/joint_canopen_fd.h"

#include <cstdio>
#include <cstdlib>
#include <limits>
#include <vector>

#define CHECK(condition) do { \
    if (!(condition)) { \
        std::fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #condition); \
        std::abort(); \
    } \
} while (0)

namespace {

using app::JointCanopenFd;
using app::JointDriveAction;
using app::JointDriveTarget;
using app::JointFeedbackSample;

constexpr uint8_t kNode = 5U;
constexpr uint32_t kFeedback = 0x180U + kNode;

struct Transport {
    std::vector<hal::CanFrame> frames;
    bool busy {};
};

bool send(void* context, const hal::CanFrame& frame)
{
    auto& transport = *static_cast<Transport*>(context);
    if (transport.busy) return false;
    transport.frames.push_back(frame);
    return true;
}

hal::CanFrame frame(uint32_t id, uint8_t length)
{
    hal::CanFrame result;
    result.id = id;
    result.size = length;
    result.fd = true;
    return result;
}

uint32_t read32(const uint8_t* bytes)
{
    return uint32_t(bytes[0]) | uint32_t(bytes[1]) << 8U |
           uint32_t(bytes[2]) << 16U | uint32_t(bytes[3]) << 24U;
}

void nmt(JointCanopenFd& joint, Transport& transport, uint8_t command, uint32_t time)
{
    auto message = frame(0U, 2U);
    message.data[0] = command;
    message.data[1] = kNode;
    joint.process(message, {}, time, send, &transport);
}

void sync(JointCanopenFd& joint, Transport& transport, uint32_t time, uint8_t counter,
          const JointFeedbackSample& sample)
{
    auto message = frame(0x080U, 1U);
    message.data[0] = counter;
    joint.process(message, sample, time, send, &transport);
}

JointFeedbackSample sample(uint32_t time)
{
    JointFeedbackSample result;
    result.position_rad = -0.5F;
    result.velocity_rad_s = 2.0F;
    result.current_a = 0.25F;
    result.bus_voltage_v = 36.0F;
    result.sequence = 123U;
    result.sample_us = time;
    result.valid = true;
    return result;
}

void check_feedback()
{
    JointCanopenFd joint(kNode);
    Transport transport;
    transport.busy = true;
    joint.service(0U, send, &transport);
    nmt(joint, transport, 1U, 100U);
    CHECK(transport.frames.empty());
    transport.busy = false;
    joint.service(1000U, send, &transport);
    CHECK(transport.frames.size() == 1U && transport.frames.back().id == 0x705U &&
           transport.frames.back().fd && transport.frames.back().data[0] == 0U);
    sync(joint, transport, 2000U, 240U, sample(1900U));
    CHECK(transport.frames.size() == 1U);
    nmt(joint, transport, 1U, 2200U);
    sync(joint, transport, 4000U, 240U, sample(3900U));
    CHECK(transport.frames.size() == 2U);
    const auto first = transport.frames.back();
    CHECK(first.id == kFeedback && first.fd && first.brs && first.size == 32U);
    CHECK(first.data[0] == 0x40U && first.data[30] == 240U && first.data[31] == 0U);
    CHECK(read32(&first.data[2]) == static_cast<uint32_t>(-500000));
    CHECK(read32(&first.data[6]) == 2000000U && read32(&first.data[10]) == 250U);
    CHECK(read32(&first.data[14]) == 36000U && read32(&first.data[18]) == 3900U);
    sync(joint, transport, 4500U, 240U, sample(4400U));
    CHECK(transport.frames.size() == 2U);
    sync(joint, transport, 6000U, 1U, sample(5900U));
    CHECK(transport.frames.size() == 3U);
    sync(joint, transport, 8000U, 2U, sample(5900U));
    auto invalid = sample(9900U);
    invalid.position_rad = std::numeric_limits<float>::infinity();
    sync(joint, transport, 10000U, 3U, invalid);
    CHECK(transport.frames.size() == 3U);
    transport.busy = true;
    sync(joint, transport, 12000U, 4U, sample(11900U));
    transport.busy = false;
    joint.service(13000U, send, &transport);
    CHECK(transport.frames.size() == 3U);
    nmt(joint, transport, 2U, 13500U);
    sync(joint, transport, 14000U, 5U, sample(13900U));
    CHECK(transport.frames.size() == 3U);
    joint.service(101000U, send, &transport);
    CHECK(transport.frames.size() == 4U && transport.frames.back().data[0] == 4U);
}

void check_usdo()
{
    JointCanopenFd joint(kNode);
    Transport transport;
    joint.service(0U, send, &transport);
    auto upload = frame(0x67FU, 6U);
    upload.data[0] = kNode;
    upload.data[1] = 0x11U;
    upload.data[2] = 1U;
    upload.data[3] = 2U;
    upload.data[4] = 0x00U;
    upload.data[5] = 0x1AU;
    joint.process(upload, {}, 1000U, send, &transport);
    const auto response = transport.frames.back();
    CHECK(response.id == 0x585U && response.data[0] == 127U && response.data[1] == 0x31U);
    CHECK(response.data[2] == 1U && response.data[7] == 4U);
    CHECK(read32(&response.data[8]) == 0x60640020U);
    upload.data[1] = 0x01U;
    joint.process(upload, {}, 2000U, send, &transport);
    CHECK(transport.frames.back().data[1] == 0x7FU && transport.frames.back().data[6] == 0x32U);
    upload.data[0] = 6U;
    joint.process(upload, {}, 3000U, send, &transport);
    CHECK(transport.frames.size() == 3U);
}

unsigned apply_count = 0;
unsigned disable_count = 0;
bool drive(JointDriveAction action, const JointDriveTarget& order, uint32_t)
{
    if (action == JointDriveAction::Apply) {
        CHECK(order.mode == 8U);
        ++apply_count;
    }
    if (action == JointDriveAction::Disable) ++disable_count;
    return true;
}

void check_drive()
{
    JointCanopenFd joint(kNode, drive);
    Transport transport;
    joint.service(0U, send, &transport);
    nmt(joint, transport, 1U, 1000U);
    auto pdo = frame(0x205U, 16U);
    pdo.data[2] = 8U;
    pdo.data[0] = 0x0FU;
    joint.process(pdo, sample(1200U), 1300U, send, &transport);
    sync(joint, transport, 2000U, 1U, sample(1900U));
    CHECK(apply_count == 0U);
    pdo.data[0] = 0x06U;
    joint.process(pdo, {}, 3100U, send, &transport);
    sync(joint, transport, 4000U, 2U, sample(3900U));
    CHECK(transport.frames.back().data[0] == 0x21U);
    pdo.data[0] = 0x07U;
    joint.process(pdo, {}, 5100U, send, &transport);
    sync(joint, transport, 6000U, 3U, sample(5900U));
    CHECK(transport.frames.back().data[0] == 0x23U);
    pdo.data[0] = 0x0FU;
    joint.process(pdo, {}, 7100U, send, &transport);
    CHECK(apply_count == 0U);
    sync(joint, transport, 8000U, 4U, sample(7900U));
    CHECK(apply_count == 1U && transport.frames.back().data[0] == 0x23U);
    pdo.data[0] = 0x0FU;
    joint.process(pdo, {}, 9100U, send, &transport);
    auto powered = sample(9900U);
    powered.output_enabled = true;
    sync(joint, transport, 10000U, 5U, powered);
    CHECK(apply_count == 2U && transport.frames.back().data[0] == 0x27U);
    joint.service(15999U, send, &transport);
    CHECK(disable_count == 0U);
    joint.service(16000U, send, &transport);
    CHECK(disable_count == 1U);
    joint.process(pdo, {}, 17100U, send, &transport);
    sync(joint, transport, 18000U, 6U, sample(17900U));
    CHECK(apply_count == 2U && transport.frames.back().data[0] == 0x08U);
    nmt(joint, transport, 2U, 18100U);
    nmt(joint, transport, 1U, 18200U);
    pdo.data[0] = 0x06U;
    joint.process(pdo, {}, 19100U, send, &transport);
    sync(joint, transport, 20000U, 7U, sample(19900U));
    CHECK(apply_count == 2U && transport.frames.back().data[0] == 0x08U);
}

void check_sync_cadence()
{
    JointCanopenFd joint(kNode);
    Transport transport;
    constexpr uint32_t start = UINT32_MAX - 4000U;
    joint.service(start, send, &transport);
    nmt(joint, transport, 1U, start);
    for (unsigned cycle = 1U; cycle <= 500U; ++cycle) {
        const uint32_t stamp = start + cycle * 2000U;
        const auto counter = static_cast<uint8_t>((cycle - 1U) % 240U + 1U);
        sync(joint, transport, stamp, counter, sample(stamp - 250U));
        CHECK(transport.frames.size() == cycle + 1U);
        CHECK(transport.frames.back().data[30] == counter);
    }
}

} // namespace

int main()
{
    check_feedback();
    check_usdo();
    check_drive();
    check_sync_cadence();
}
