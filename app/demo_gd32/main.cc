#include <drivers/uart.h>
#include <cstring>

/* Include GD32 headers for USART0_BASE and USART0_IRQn */
extern "C" {
#include "gd32f50x.h"
}

int main(void) {
    /* 配置 UART */
    uint8_t rx_buffer[256];
    hal::UartConfig uart_config = {
        .baudrate = 115200U,
        .data_bits = hal::DataBits::Bits8,
        .stop_bits = hal::StopBits::One,
        .parity = hal::Parity::None,
        .rx_buffer = rx_buffer,
        .rx_buffer_size = sizeof(rx_buffer),
    };

    /* 初始化 UART */
    hal::UartBase uart(USART0_BASE, USART0_IRQn);
    (void)uart.init(uart_config);

    /* 发送欢迎消息 */
    const char *msg = "Hello, GD32F503!\r\n";
    size_t sent;
    (void)uart.send(reinterpret_cast<const uint8_t *>(msg), strlen(msg), &sent, 1000);

    /* 主循环 */
    while (1) {
        /* 接收并回显数据 */
        uint8_t ch;
        size_t read;
        if (uart.recv(&ch, 1, &read, 100) == hal::Status::Ok && read > 0) {
            (void)uart.send(&ch, 1, &sent, 100);
        }
    }

    return 0;
}
