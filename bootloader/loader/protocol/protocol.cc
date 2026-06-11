#include <boot/flash_map.h>
#include <boot/flash_ops.h>
#include <boot/image.h>
#include <boot/protocol.h>

#include <link/codec.h>
#include <link/frame.h>

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace boot {

bool upgrade_erase();
uint8_t upgrade_write(uint32_t offset, const uint8_t *data, uint32_t len);
uint8_t upgrade_verify(const uint8_t expected_hash[32], uint8_t computed[32]);
bool check_image(uint32_t addr, uint32_t expected_product_id);
uint32_t get_product_id();

namespace {

static ProtocolRxCallback g_rx_fn = nullptr;
static ProtocolTxCallback g_tx_fn = nullptr;
static link::FrameCodec g_codec;

constexpr uint8_t kDefaultBootAddr = 0x20;

uint32_t load_u32_le(const uint8_t *p) {
    return static_cast<uint32_t>(p[0]) |
           (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) |
           (static_cast<uint32_t>(p[3]) << 24);
}

uint8_t ack_sender(const link::Frame &req) {
    return (req.receiver_id == link::ADDR_BROADCAST ||
            req.receiver_id == link::ADDR_RESERVED)
        ? kDefaultBootAddr
        : req.receiver_id;
}

void system_reset() {
    auto *aircr = reinterpret_cast<volatile uint32_t *>(0xE000ED0CU);
    *aircr = 0x05FA0004U;
    while (1) {}
}

void send_ack(const link::Frame &req, uint8_t status,
              const uint8_t *data = nullptr, size_t data_len = 0) {
    if (!g_tx_fn) return;

    uint8_t ack_data[CONFIG_LINK_MAX_FRAME_SIZE -
                     link::HEADER_SIZE - link::CRC_SIZE];
    if (sizeof(ack_data) == 0) return;

    size_t ack_len = 1 + data_len;
    if (ack_len > sizeof(ack_data) || ack_len > UINT8_MAX) {
        status = boot_proto::ACK_ERR_STATE;
        data = nullptr;
        data_len = 0;
        ack_len = 1;
    }

    ack_data[0] = status;
    if (data && data_len > 0) {
        std::memcpy(ack_data + 1, data, data_len);
    }

    link::PackArgs args{};
    args.sender = ack_sender(req);
    args.receiver = req.sender_id;
    args.cmd_set = req.cmd_set;
    args.cmd_id = req.cmd_id;
    args.ack_mode = link::AckMode::No;
    args.enc_mode = link::EncMode::None;
    args.priority = req.priority();
    args.is_ack = true;
    args.seq = req.seq;

    uint8_t frame_buf[CONFIG_LINK_MAX_FRAME_SIZE];
    const size_t frame_len = link::FrameCodec::pack(
        frame_buf, sizeof(frame_buf), args, ack_data,
        static_cast<uint8_t>(ack_len));
    if (frame_len > 0) {
        (void)g_tx_fn(frame_buf, frame_len);
    }
}

ImageVersion fill_image_status(uint32_t addr, uint32_t product_id,
                               uint8_t &valid) {
    ImageVersion version{};
    valid = check_image(addr, product_id) ? 1 : 0;
    if (valid) {
        ImageHeader hdr{};
        if (flash_read(addr, &hdr, sizeof(hdr))) {
            version = hdr.version;
        } else {
            valid = 0;
            std::memset(&version, 0, sizeof(version));
        }
    }
    return version;
}

void handle_enter_loader(const link::Frame &req) {
    const uint8_t status = upgrade_erase()
        ? boot_proto::ACK_OK
        : boot_proto::ACK_ERR_ERASE;
    send_ack(req, status);
}

void handle_query_status(const link::Frame &req) {
    boot_proto::StatusAck ack{};
    ack.in_loader = 1;
#if defined(CONFIG_BOOT_MODE_AB)
    ack.boot_mode = 0;
#else
    ack.boot_mode = 1;
#endif

    const uint32_t product_id = get_product_id();
    const ImageVersion slot0_ver = fill_image_status(
        flash_area_addr(FLASH_AREA_SLOT0), product_id, ack.slot0_valid);
    std::memcpy(reinterpret_cast<uint8_t *>(&ack) +
                    offsetof(boot_proto::StatusAck, slot0_ver),
                &slot0_ver, sizeof(slot0_ver));
#if defined(CONFIG_BOOT_MODE_AB)
    const ImageVersion upgrade_ver = fill_image_status(
        flash_area_addr(FLASH_AREA_UPGRADE), product_id, ack.upgrade_valid);
    std::memcpy(reinterpret_cast<uint8_t *>(&ack) +
                    offsetof(boot_proto::StatusAck, upgrade_ver),
                &upgrade_ver, sizeof(upgrade_ver));
#endif

    send_ack(req, boot_proto::ACK_OK,
             reinterpret_cast<const uint8_t *>(&ack), sizeof(ack));
}

void handle_fw_transfer(const link::Frame &req) {
    if (req.data_len < sizeof(uint32_t) * 2 || !req.data) {
        send_ack(req, boot_proto::ACK_ERR_ADDR);
        return;
    }

    constexpr uint32_t header_len = sizeof(uint32_t) * 2;
    const uint32_t offset = load_u32_le(req.data);
    const uint32_t payload_len = load_u32_le(req.data + sizeof(uint32_t));
    const uint32_t actual_data_len = req.data_len - header_len;
    if (payload_len != actual_data_len) {
        send_ack(req, boot_proto::ACK_ERR_ADDR);
        return;
    }

    const uint8_t status = upgrade_write(
        offset, req.data + header_len, payload_len);
    send_ack(req, status);
}

void handle_fw_verify(const link::Frame &req) {
    if (req.data_len < sizeof(boot_proto::VerifyReq) || !req.data) {
        send_ack(req, boot_proto::ACK_ERR_HASH);
        return;
    }

    uint8_t computed[32]{};
    const uint8_t status = upgrade_verify(req.data, computed);
    send_ack(req, status, computed, sizeof(computed));
}

void handle_reboot(const link::Frame &req) {
    send_ack(req, boot_proto::ACK_OK);
    system_reset();
}

void dispatch_frame(const link::Frame &frame) {
    if (frame.cmd_set != boot_proto::CMD_SET) return;
    if (frame.is_ack()) return;

    switch (frame.cmd_id) {
    case boot_proto::CMD_ENTER_LOADER:
        handle_enter_loader(frame);
        break;
    case boot_proto::CMD_QUERY_STATUS:
        handle_query_status(frame);
        break;
    case boot_proto::CMD_FW_TRANSFER:
        handle_fw_transfer(frame);
        break;
    case boot_proto::CMD_FW_VERIFY:
        handle_fw_verify(frame);
        break;
    case boot_proto::CMD_REBOOT:
        handle_reboot(frame);
        break;
    default:
        send_ack(frame, boot_proto::ACK_ERR_STATE);
        break;
    }
}

} // namespace

void protocol_register_transport(ProtocolRxCallback rx,
                                 ProtocolTxCallback tx) {
    g_rx_fn = rx;
    g_tx_fn = tx;
    g_codec.reset();
}

void protocol_process() {
    if (!g_rx_fn) return;

    uint8_t buf[CONFIG_LINK_MAX_FRAME_SIZE];
    size_t len = 0;
    if (!g_rx_fn(buf, len) || len == 0) return;

    for (size_t i = 0; i < len; ++i) {
        const int ret = g_codec.feed(buf[i]);
        if (ret > 0) {
            dispatch_frame(g_codec.frame());
            g_codec.reset();
        }
    }
}

} // namespace boot
