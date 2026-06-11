#include <boot/protocol.h>

#include <device.h>
#include <drivers/status.h>
#include <drivers/uart.h>
#include <drivers_generated.h>
#include <init.h>
#include <link/codec.h>

#include <cstddef>
#include <cstdint>

namespace boot {
namespace {

hal::UartBase &console_uart() {
    return device_get(uart0);
}

bool uart_rx(uint8_t *buf, size_t &len) {
    if (!buf) {
        len = 0;
        return false;
    }

    auto &uart = console_uart();
    len = 0;
    while (len < CONFIG_LINK_MAX_FRAME_SIZE && uart.rx_available() > 0) {
        size_t read = 0;
        const hal::Status status = uart.recv(buf + len, 1, &read, 0);
        if (status != hal::Status::Ok || read == 0) break;
        len += read;
    }

    return len > 0;
}

bool uart_tx(const uint8_t *buf, size_t len) {
    if (!buf || len == 0) return false;

    size_t sent = 0;
    const hal::Status status = console_uart().send(buf, len, &sent, 500);
    return status == hal::Status::Ok && sent == len;
}

int transport_init() {
    protocol_register_transport(uart_rx, uart_tx);
    return 0;
}

} // namespace
} // namespace boot

SYS_INIT(boot::transport_init, INITCALL_LEVEL_PRE_KERNEL_3, 80);
