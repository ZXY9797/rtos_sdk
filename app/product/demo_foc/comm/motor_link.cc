#include "comm/motor_protocol.h"
#include "board/board_devices.h"
#include <link/uart_link.h>
#include <link/router.h>
#include <cstring>

namespace app {

namespace {
link::UartLink uart_link(board::console());
bool transport_started = false;
link::HandlerResult on_request(const link::Frame& frame, const link::CompletionToken& completion, void*)
{
    if (frame.data_len > sizeof(MotorRequest::data) ||
        frame.cmd_id > static_cast<unsigned>(MotorCommand::SupportedService))
        return link::HandlerResult::completed(2U);
    MotorRequest request;
    request.command = static_cast<MotorCommand>(frame.cmd_id);
    request.owner = frame.sender_id;
    request.completion = completion;
    request.size = frame.data_len;
    if (request.size) std::memcpy(request.data, frame.data, request.size);
    return enqueue_motor_request(request) ? link::HandlerResult::deferred() :
        link::HandlerResult::completed(link::HANDLER_STATUS_QUEUE_FULL);
}
LINK_HOST_ASYNC_HANDLER(0U, 0U, kMotorCommandSet, 0xFFU, 0U, 0U, on_request, nullptr, 5000U);
}

bool start_motor_transport()
{
    if (transport_started) return true;
    uart_link.set_id(2U);
    const link::RouteEntry routes[] {
        link::make_route(link::route_by_host(0x20U, 0xF0U).to(2U)),
    };
    auto& router = link::Router::instance();
    router.set_self_addr(0x10U);
    auto& can = board::main_can().native();
    constexpr hal::CanFilter filters[] {
        {0x000U, 0x7FFU}, {0x080U, 0x7FFU}, {0x600U, 0x780U},
#if defined(CONFIG_DEMO_FOC_JOINT_MOTION)
        {0x200U + CONFIG_DEMO_FOC_CANOPEN_NODE_ID, 0x7FFU},
        {0x300U + CONFIG_DEMO_FOC_CANOPEN_NODE_ID, 0x7FFU},
#endif
    };
    if (!can.is_started() &&
        can.set_standard_filters(filters, sizeof(filters) / sizeof(filters[0])) != hal::Status::Ok)
        return false;
    if (can.start() != hal::Status::Ok ||
        !router.set_routes(routes, sizeof(routes) / sizeof(routes[0])) ||
        !uart_link.is_ready()) return false;
    transport_started = true;
    return true;
}

bool motor_stream_ready(uint8_t owner)
{
    return (owner & 0xF0U) == 0x20U && uart_link.is_ready();
}

bool motor_log_backend(void*, const char* text, size_t size)
{
    if (!transport_started || !text || size > 496U) return false;
    link::SendRequest request;
    request.receiver = 0x20U;
    request.cmd_set = kMotorCommandSet;
    request.cmd_id = 0x83U;
    request.data = reinterpret_cast<const uint8_t*>(text);
    request.data_len = size;
    return link::Router::instance().send(request) >= 0;
}
} // namespace app
