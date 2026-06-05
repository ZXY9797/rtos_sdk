#include <device.h>
#include <drivers_generated.h>
#include <drivers/uart.h>
#include <drivers/spi.h>
#include <cstring>

int main(void) {
    /* ===== UART Demo (DMA TX) — 从 DTS 自动解析基地址和中断号 ===== */
    uint8_t rx_buffer[256];
    hal::UartConfig uart_config = {
        .baudrate = 115200U,
        .data_bits = hal::DataBits::Bits8,
        .stop_bits = hal::StopBits::One,
        .parity = hal::Parity::None,
        .rx_buffer = rx_buffer,
        .rx_buffer_size = sizeof(rx_buffer),
    };

    hal::Device<DT_ORD(DT_ALIAS(uart0))> uart;
    (void)uart.init(uart_config);

    const char *msg = "Hello, GD32F503! (DMA TX)\r\n";
    size_t sent;
    (void)uart.send(reinterpret_cast<const uint8_t *>(msg), strlen(msg), &sent, 1000);

    /* ===== SPI Demo (DMA TX/RX) — 从 DTS 自动解析基地址 ===== */
    hal::SpiConfig spi_config = {
        .mode = hal::SpiMode::Mode0,
        .clock_hz = 1000000U,
        .data_bits = 8,
    };

    hal::Device<DT_ORD(DT_ALIAS(spi0))> spi;
    (void)spi.init(spi_config);

    uint8_t spi_tx[] = {0x9F, 0x00, 0x00, 0x00};  /* Read JEDEC ID command */
    uint8_t spi_rx[4] = {};
    (void)spi.sync_send(spi_tx, spi_rx, sizeof(spi_tx), 1000);

    /* Main loop: echo UART data */
    while (1) {
        uint8_t ch;
        size_t read;
        if (uart.recv(&ch, 1, &read, 100) == hal::Status::Ok && read > 0) {
            (void)uart.send(&ch, 1, &sent, 100);
        }
    }

    return 0;
}
