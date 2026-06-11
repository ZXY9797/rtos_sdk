#include <boot/protocol.h>
#include <boot/image.h>
#include <boot/flash_map.h>
#include <cstring>

namespace boot {

// 由 upgrade_state.cc 实现
bool upgrade_erase();
uint8_t upgrade_write(uint32_t offset, const uint8_t *data, uint32_t len);
uint8_t upgrade_verify(const uint8_t expected_hash[32]);

namespace {

// 传输层回调 (由 product 层 transport.cc 注册)
using RxCallback = bool (*)(uint8_t *buf, size_t &len);
using TxCallback = bool (*)(const uint8_t *buf, size_t len);

static RxCallback g_rx_fn = nullptr;
static TxCallback g_tx_fn = nullptr;

void send_ack(uint8_t cmd_id, uint8_t status) {
    uint8_t buf[7];
    buf[0] = boot_proto::CMD_SET;
    buf[1] = cmd_id;
    buf[2] = 1; // len low
    buf[3] = 0; // len high
    buf[4] = status;
    uint16_t crc = boot_proto::crc16_ccitt(buf, 5);
    buf[5] = uint8_t(crc >> 8);
    buf[6] = uint8_t(crc);
    if (g_tx_fn) g_tx_fn(buf, 7);
}

void handle_enter_loader() {
    send_ack(boot_proto::CMD_ENTER_LOADER, boot_proto::ACK_OK);
}

void handle_query_status() {
    boot_proto::StatusAck ack{};
    ack.in_loader = 1;
#if defined(CONFIG_BOOT_MODE_AB)
    ack.boot_mode = 0;
#else
    ack.boot_mode = 1;
#endif
    // TODO: 检查 slot0/upgrade 区有效性

    uint8_t buf[4 + sizeof(boot_proto::StatusAck) + 2];
    buf[0] = boot_proto::CMD_SET;
    buf[1] = boot_proto::CMD_QUERY_STATUS;
    uint16_t plen = sizeof(boot_proto::StatusAck);
    buf[2] = uint8_t(plen);
    buf[3] = uint8_t(plen >> 8);
    memcpy(&buf[4], &ack, plen);
    uint16_t crc = boot_proto::crc16_ccitt(buf, 4 + plen);
    buf[4 + plen] = uint8_t(crc >> 8);
    buf[4 + plen + 1] = uint8_t(crc);
    if (g_tx_fn) g_tx_fn(buf, 4 + plen + 2);
}

void handle_fw_transfer(const uint8_t *payload, uint16_t len) {
    if (len < 8) {
        send_ack(boot_proto::CMD_FW_TRANSFER, boot_proto::ACK_ERR_ADDR);
        return;
    }
    auto *req = reinterpret_cast<const boot_proto::TransferReq *>(payload);
    uint8_t status = upgrade_write(req->offset, req->data, req->length);
    send_ack(boot_proto::CMD_FW_TRANSFER, status);
}

void handle_fw_verify(const uint8_t *payload, uint16_t len) {
    if (len < 32) {
        send_ack(boot_proto::CMD_FW_VERIFY, boot_proto::ACK_ERR_HASH);
        return;
    }
    uint8_t status = upgrade_verify(payload);
    send_ack(boot_proto::CMD_FW_VERIFY, status);
}

void handle_reboot() {
    send_ack(boot_proto::CMD_REBOOT, boot_proto::ACK_OK);
    // TODO: NVIC_SystemReset()
}

void dispatch_frame(const uint8_t *frame, size_t len) {
    if (len < 6) return;

    uint8_t cmd_set = frame[0];
    uint8_t cmd_id  = frame[1];
    uint16_t plen   = uint16_t(frame[2]) | (uint16_t(frame[3]) << 8);

    if (cmd_set != boot_proto::CMD_SET) return;
    if (len < size_t(4 + plen + 2)) return;

    // CRC 校验
    uint16_t rx_crc = uint16_t(frame[4 + plen]) << 8 | frame[4 + plen + 1];
    if (boot_proto::crc16_ccitt(frame, 4 + plen) != rx_crc) {
        send_ack(cmd_id, boot_proto::ACK_ERR_CRC);
        return;
    }

    const uint8_t *payload = &frame[4];

    switch (cmd_id) {
    case boot_proto::CMD_ENTER_LOADER: handle_enter_loader(); break;
    case boot_proto::CMD_QUERY_STATUS: handle_query_status(); break;
    case boot_proto::CMD_FW_TRANSFER:  handle_fw_transfer(payload, plen); break;
    case boot_proto::CMD_FW_VERIFY:    handle_fw_verify(payload, plen); break;
    case boot_proto::CMD_REBOOT:       handle_reboot(); break;
    default: send_ack(cmd_id, boot_proto::ACK_ERR_STATE); break;
    }
}

} // namespace

void protocol_register_transport(RxCallback rx, TxCallback tx) {
    g_rx_fn = rx;
    g_tx_fn = tx;
}

void protocol_process() {
    if (!g_rx_fn) return;

    uint8_t buf[1024];
    size_t len = 0;
    if (g_rx_fn(buf, len) && len > 0) {
        dispatch_frame(buf, len);
    }
}

} // namespace boot
