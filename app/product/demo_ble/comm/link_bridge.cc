#include "comm/link_bridge.h"

#include "services/ble_app.h"

#include <log.h>

#ifdef CONFIG_LINK
#include <link/ble_link.h>
#include <link/router.h>
#endif

namespace app {
namespace {

#ifdef CONFIG_LINK
// Default ATT payload is 20 bytes; reserve one byte for fragmentation.
static link::BleLink g_ble_link(19);

bool ble_link_tx(const uint8_t *data, size_t len, void *) {
    return ble_send_uart(data, len);
}

void ble_link_rx(const uint8_t *data, size_t len) {
    g_ble_link.on_receive(data, len);
}
#endif

} // namespace

int init_comm() {
#ifdef CONFIG_LINK
    g_ble_link.set_id(1);
    g_ble_link.set_tx_func(ble_link_tx, nullptr);
    g_ble_link.set_connected(ble_is_connected());
    set_ble_rx_callback(ble_link_rx);

    auto &router = link::Router::instance();
    router.set_self_addr(link::make_addr(0x2, 0));

    static const link::RouteEntry routes[] = {
        link::make_route(link::route_by_host(0x10, 0xF0).to(1)),
        link::make_route(link::route_direct(0).to(1)),
    };
    if (!router.set_routes(routes, 2)) {
        LOGE("link", "invalid route table");
        deinit_comm();
        return -1;
    }

    LOGI("link", "BLE comm initialized: self=0x%02x",
         link::make_addr(0x2, 0));
#endif
    return 0;
}

void deinit_comm() {
#ifdef CONFIG_LINK
    set_ble_rx_callback(nullptr);
    g_ble_link.set_connected(false);
    g_ble_link.reset_rx();
    g_ble_link.set_tx_func(nullptr, nullptr);
    g_ble_link.set_id(0U);
    auto &router = link::Router::instance();
    (void)router.set_routes(nullptr, 0U);
    router.set_self_addr(0U);
#endif
}

void set_comm_connected(bool connected) {
#ifdef CONFIG_LINK
    g_ble_link.set_connected(false);
    g_ble_link.reset_rx();
    if (connected) {
        g_ble_link.set_connected(true);
    }
#else
    (void)connected;
#endif
}

} // namespace app
