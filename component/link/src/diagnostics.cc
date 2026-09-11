#include "link/diagnostics.h"

#include <algorithm>

namespace link {
namespace {

constexpr size_t kHeaderSize = 12U;
constexpr size_t kCounterCount = 15U;
constexpr size_t kCounterBytes = kCounterCount * sizeof(uint32_t);
constexpr size_t kLinkRecordSize = 24U;
constexpr size_t kMinimumSize = kHeaderSize + kCounterBytes;

void put_u16(uint8_t* output, uint16_t value)
{
    output[0] = static_cast<uint8_t>(value & 0xFFU);
    output[1] = static_cast<uint8_t>(value >> 8U);
}

void put_u32(uint8_t* output, uint32_t value)
{
    output[0] = static_cast<uint8_t>(value & 0xFFU);
    output[1] = static_cast<uint8_t>((value >> 8U) & 0xFFU);
    output[2] = static_cast<uint8_t>((value >> 16U) & 0xFFU);
    output[3] = static_cast<uint8_t>(value >> 24U);
}

void put_counter(uint8_t* output, size_t& offset, uint32_t value)
{
    put_u32(&output[offset], value);
    offset += sizeof(value);
}

void encode_counters(const DiagnosticCounters& counters,
                     uint8_t* output,
                     size_t& offset)
{
    put_counter(output, offset, counters.received_frames);
    put_counter(output, offset, counters.codec_errors);
    put_counter(output, offset, counters.forwarded_frames);
    put_counter(output, offset, counters.transmitted_frames);
    put_counter(output, offset, counters.security_drops);
    put_counter(output, offset, counters.duplicate_requests);
    put_counter(output, offset, counters.replay_conflicts);
    put_counter(output, offset, counters.ack_timeouts);
    put_counter(output, offset, counters.queued_requests);
    put_counter(output, offset, counters.completed_requests);
    put_counter(output, offset, counters.deferred_requests);
    put_counter(output, offset, counters.queue_overflows);
    put_counter(output, offset, counters.async_timeouts);
    put_counter(output, offset, counters.unhandled_requests);
    put_counter(output, offset, counters.transmit_failures);
}

void encode_link(const LinkDiagnosticCounters& link_counters,
                 uint8_t* output)
{
    put_u16(output, link_counters.link_id);
    output[2] = link_counters.is_ready ? 1U : 0U;
    output[3] = link_counters.process_divider;
    put_u32(&output[4], link_counters.received_bytes);
    put_u32(&output[8], link_counters.received_frames);
    put_u32(&output[12], link_counters.codec_errors);
    put_u32(&output[16], link_counters.transmitted_frames);
    put_u32(&output[20], link_counters.transmit_failures);
}

} // namespace

size_t encode_diagnostics(const DiagnosticSnapshot& snapshot,
                          uint8_t* destination,
                          size_t destination_size)
{
    if (destination == nullptr || destination_size < kMinimumSize) {
        return 0U;
    }
    const size_t available_records =
        (destination_size - kMinimumSize) / kLinkRecordSize;
    const size_t link_count = std::min(
        std::min(snapshot.link_count,
                 static_cast<size_t>(CONFIG_LINK_MAX_LINKS)),
        available_records);

    destination[0] = DIAGNOSTICS_WIRE_VERSION;
    destination[1] = snapshot.flags;
    destination[2] = static_cast<uint8_t>(link_count);
    destination[3] = static_cast<uint8_t>(std::min(
        snapshot.link_count, static_cast<size_t>(UINT8_MAX)));
    put_u16(&destination[4], snapshot.high_queue_depth);
    put_u16(&destination[6], snapshot.low_queue_depth);
    put_u16(&destination[8], snapshot.async_active);
    put_u16(&destination[10], 0U);

    size_t offset = kHeaderSize;
    encode_counters(snapshot.counters, destination, offset);
    for (size_t index = 0U; index < link_count; ++index) {
        encode_link(snapshot.links[index], &destination[offset]);
        offset += kLinkRecordSize;
    }
    return offset;
}

} // namespace link
