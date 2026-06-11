#pragma once

#include <cstdint>
#include <cstddef>
#include "image.h"

namespace boot_proto {

// cmd_set = 0x00
static constexpr uint8_t CMD_SET = 0x00;

// 命令 ID
static constexpr uint8_t CMD_ENTER_LOADER = 0x07;
static constexpr uint8_t CMD_QUERY_STATUS = 0x08;
static constexpr uint8_t CMD_FW_TRANSFER  = 0x09;
static constexpr uint8_t CMD_FW_VERIFY    = 0x0A;
static constexpr uint8_t CMD_REBOOT       = 0x0B;

// 应答码
static constexpr uint8_t ACK_OK        = 0x00;
static constexpr uint8_t ACK_ERR_CRC   = 0x01;
static constexpr uint8_t ACK_ERR_STATE = 0x02;
static constexpr uint8_t ACK_ERR_ADDR  = 0x03;
static constexpr uint8_t ACK_ERR_HASH  = 0x04;
static constexpr uint8_t ACK_ERR_ERASE = 0x05;
static constexpr uint8_t ACK_ERR_MAGIC = 0x06;
static constexpr uint8_t ACK_ERR_PROD  = 0x07;

// 帧格式: cmd_set(1) + cmd_id(1) + len(2) + payload(N) + crc16(2)
static constexpr uint8_t FRAME_HEADER_SIZE = 4;  // cmd_set + cmd_id + len
static constexpr uint8_t FRAME_FOOTER_SIZE = 2;  // crc16

struct __attribute__((packed)) TransferReq {
    uint32_t offset;
    uint32_t length;
    uint8_t  data[];
};

struct __attribute__((packed)) VerifyReq {
    uint8_t  sha256[32];
};

struct __attribute__((packed)) VerifyAck {
    uint8_t  status;
    uint8_t  computed[32];
};

struct __attribute__((packed)) StatusAck {
    uint8_t  in_loader;
    uint8_t  boot_mode;      // 0=AB, 1=Direct
    uint8_t  slot0_valid;
    uint8_t  upgrade_valid;
    boot::ImageVersion slot0_ver;
    boot::ImageVersion upgrade_ver;
};

// CRC-16 CCITT
uint16_t crc16_ccitt(const uint8_t *data, size_t len);

} // namespace boot_proto
