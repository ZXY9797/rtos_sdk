#pragma once

#include "link.h"
#include <drivers/uart.h>

#ifndef CONFIG_LINK_UART_TX_TIMEOUT_MS
#define CONFIG_LINK_UART_TX_TIMEOUT_MS 100
#endif

namespace link {

class UartLink : public Link {
public:
    explicit UartLink(hal::UartBase &uart) : uart_(uart) {
        registered_ = register_link(this);
    }

    int send(const uint8_t *data, size_t len) override {
        size_t sent = 0;
        (void)uart_.send(data, len, &sent,
                         CONFIG_LINK_UART_TX_TIMEOUT_MS);
        return static_cast<int>(sent);
    }

    int recv(uint8_t *buf, size_t max_len) override {
        size_t read = 0;
        (void)uart_.recv(buf, max_len, &read, 0);
        return static_cast<int>(read);
    }

    bool is_ready() const override {
        return registered_ && uart_.is_initialized();
    }

private:
    hal::UartBase &uart_;
    bool registered_ {false};
};

} // namespace link
