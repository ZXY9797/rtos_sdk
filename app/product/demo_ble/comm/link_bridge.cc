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
static link::BleLink g_ble_link(20);

void ble_link_tx(const uint8_t *data, size_t len, void *) {
    ble_send_uart(data, len);
}

void ble_link_rx(const uint8_t *data, size_t len) {
    g_ble_link.on_receive(data, len);
}
#endif

} // namespace

void init_comm() {
#ifdef CONFIG_LINK
    g_ble_link.set_id(1);
    g_ble_link.set_tx_func(ble_link_tx, nullptr);
    g_ble_link.set_connected(ble_is_connected());
    set_ble_rx_callback(ble_link_rx);

    auto &router = link::Router::instance();
    router.set_self_addr(link::make_addr(0x20, 0));

    static const link::RouteEntry routes[] = {
        link::make_route(link::route_by_host(0x10, 0xF0).to(1)),
        link::make_route(link::route_direct(0).to(1)),
    };
    router.set_routes(routes, 2);

    LOGI("link", "BLE comm initialized: self=0x%02x",
         link::make_addr(0x20, 0));
#endif
}

} // namespace app
