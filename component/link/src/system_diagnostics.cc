#include <link/frame.h>
#include <system/diagnostics.h>

namespace {
uint8_t response[CONFIG_LINK_MAX_FRAME_SIZE - link::HEADER_SIZE - 3U] {};
link::HandlerResult system_snapshot(const link::Frame& frame,
                                   const link::CompletionToken&, void*)
{
    if (frame.data_len != 0U) return link::HandlerResult::completed(link::HANDLER_STATUS_INVALID_RESULT);
    const size_t size = system_diagnostics::encode(response, sizeof(response));
    return link::HandlerResult::completed(link::HANDLER_STATUS_OK, response, size);
}
#if defined(CONFIG_LINK_DIAGNOSTICS_REQUIRE_SECURE)
LINK_SECURE_ASYNC_HANDLER(CONFIG_LINK_DIAGNOSTICS_COMMAND_SET, 2U, system_snapshot, nullptr, 100U);
#else
LINK_ASYNC_HANDLER(CONFIG_LINK_DIAGNOSTICS_COMMAND_SET, 2U, system_snapshot, nullptr, 100U);
#endif
}

namespace link {
// Referenced from Router::init so this handler's archive member is retained.
void system_diagnostics_handler_anchor() {}
}
