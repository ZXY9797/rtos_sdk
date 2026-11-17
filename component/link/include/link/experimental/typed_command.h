#pragma once
#include <link/frame.h>

namespace link::experimental {
// Schema owns an explicit wire codec; native struct layout is never serialized.
// Request and Response buffers remain caller-owned. Version is payload-local
// and does not change the existing Link wire protocol/version.
template <typename Schema>
struct TypedCommand {
    using Request = typename Schema::Request;
    using Response = typename Schema::Response;
    static size_t encode_request(const Request& value, uint8_t* out, size_t capacity) {
        if (out == nullptr || capacity < 1U) return 0U;
        const size_t count = Schema::encode_request(value, out + 1U, capacity - 1U);
        if (count == 0U || count > capacity - 1U) return 0U;
        out[0] = Schema::version;
        return count + 1U;
    }
    static bool decode_request(const Frame& frame, Request& request) {
        return !frame.is_ack() && frame.cmd_set == Schema::command_set && frame.cmd_id == Schema::command_id
            && frame.data != nullptr && frame.data_len > 0U && frame.data[0] == Schema::version
            && Schema::decode_request(frame.data + 1U, frame.data_len - 1U, request);
    }
    static size_t encode_response(const Response& value, uint8_t* out, size_t capacity) {
        if (out == nullptr || capacity < 1U) return 0U;
        const size_t count = Schema::encode_response(value, out + 1U, capacity - 1U);
        if (count == 0U || count > capacity - 1U) return 0U;
        out[0] = Schema::version;
        return count + 1U;
    }
    static bool decode_response(const Frame& frame, Response& response) {
        return frame.is_ack() && frame.cmd_set == Schema::command_set
            && frame.cmd_id == Schema::command_id && frame.data != nullptr
            && frame.data_len > 1U && frame.data[0] == HANDLER_STATUS_OK
            && frame.data[1] == Schema::version
            && Schema::decode_response(frame.data + 2U, frame.data_len - 2U, response);
    }
};
}
