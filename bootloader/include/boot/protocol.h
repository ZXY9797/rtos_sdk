#pragma once

#include <boot/image.h>

#include <cstddef>
#include <cstdint>

namespace boot_proto {

static constexpr uint8_t CMD_SET = 0x00;

static constexpr uint8_t CMD_ENTER_LOADER = 0x07;
static constexpr uint8_t CMD_QUERY_STATUS = 0x08;
static constexpr uint8_t CMD_FW_TRANSFER  = 0x09;
static constexpr uint8_t CMD_FW_VERIFY    = 0x0A;
static constexpr uint8_t CMD_REBOOT       = 0x0B;

static constexpr uint8_t ACK_OK        = 0x00;
static constexpr uint8_t ACK_ERR_CRC   = 0x01;
static constexpr uint8_t ACK_ERR_STATE = 0x02;
static constexpr uint8_t ACK_ERR_ADDR  = 0x03;
static constexpr uint8_t ACK_ERR_HASH  = 0x04;
static constexpr uint8_t ACK_ERR_ERASE = 0x05;
static constexpr uint8_t ACK_ERR_MAGIC = 0x06;
static constexpr uint8_t ACK_ERR_PROD  = 0x07;

// Bootloader uses component/link frame format:
// SOF + ver_len + head_crc + cmd_type + sender + receiver + seq
// + cmd_set + cmd_id + payload + crc16.

struct __attribute__((packed)) TransferReq {
    uint32_t offset;
    uint32_t length;
    uint8_t data[];
};

struct __attribute__((packed)) VerifyReq {
    uint8_t sha256[32];
};

struct __attribute__((packed)) VerifyAck {
    uint8_t status;
    uint8_t computed[32];
};

struct __attribute__((packed)) StatusAck {
    uint8_t in_loader;
    uint8_t boot_mode;      // 0=AB, 1=Direct
    uint8_t slot0_valid;
    uint8_t upgrade_valid;
    boot::ImageVersion slot0_ver;
    boot::ImageVersion upgrade_ver;
};

uint16_t crc16_ccitt(const uint8_t *data, size_t len);

} // namespace boot_proto

namespace boot {

using ProtocolRxCallback = bool (*)(uint8_t *buf, size_t &len);
using ProtocolTxCallback = bool (*)(const uint8_t *buf, size_t len);

void protocol_register_transport(ProtocolRxCallback rx,
                                 ProtocolTxCallback tx);
void protocol_process();

} // namespace boot
