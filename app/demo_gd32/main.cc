#include <device.h>
#include <drivers_generated.h>
#include <cstring>

#define device_get(alias) hal::device_get<DT_ORD(DT_ALIAS(alias))>()

int main(void) {
    auto &uart = device_get(uart0);
    auto &spi  = device_get(spi0);

    const char *msg = "Hello, GD32F503! (initcall)\r\n";
    size_t sent;
    (void)uart.send(reinterpret_cast<const uint8_t *>(msg), strlen(msg), &sent, 1000);

    uint8_t spi_tx[] = {0x9F, 0x00, 0x00, 0x00};
    uint8_t spi_rx[4] = {};
    (void)spi.sync_send(spi_tx, spi_rx, sizeof(spi_tx), 1000);

    while (1) {
        uint8_t ch;
        size_t read;
        if (uart.recv(&ch, 1, &read, 100) == hal::Status::Ok && read > 0) {
            (void)uart.send(&ch, 1, &sent, 100);
        }
    }

    return 0;
}
