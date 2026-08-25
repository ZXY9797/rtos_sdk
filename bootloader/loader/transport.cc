#include <boot/protocol.h>
#include <boot/runtime.h>

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

constexpr uint32_t kTxTimeoutMs = 500U;

hal::UartBase &console_uart() {
    return device_get(uart0);
}

bool uart_rx(uint8_t *buf, size_t &len) {
    const size_t capacity = len;
    if (buf == nullptr || capacity == 0U) {
        len = 0;
        return false;
    }

    auto &uart = console_uart();
    len = 0;
    while (len < capacity && uart.rx_available() > 0U) {
        size_t read = 0;
        const hal::Status status = uart.recv(buf + len, 1, &read, 0);
        if (status != hal::Status::Ok || read == 0U) {
            break;
        }
        len += read;
    }

    return len > 0U;
}

bool uart_tx(const uint8_t *buf, size_t len) {
    if (buf == nullptr || len == 0U) {
        return false;
    }

    size_t sent = 0;
    const hal::Status status = console_uart().send(
        buf, len, &sent, kTxTimeoutMs);
    return status == hal::Status::Ok && sent == len;
}

int transport_init() {
    if (!device_is_ready(uart0)) {
        return -1;
    }
    protocol_register_transport(uart_rx, uart_tx);
    return 0;
}

int transport_deinit() {
    protocol_register_transport(nullptr, nullptr);
    return console_uart().deinit() == hal::Status::Ok ? 0 : -1;
}

} // namespace
} // namespace boot

bool boot::transport_shutdown() {
    return transport_deinit() == 0;
}

SYS_INIT_ROLLBACK(boot::transport_init, boot::transport_deinit,
                  INITCALL_LEVEL_POST_KERNEL, 80);
