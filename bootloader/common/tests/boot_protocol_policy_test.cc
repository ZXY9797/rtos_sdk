#include <boot/protocol_policy.h>

#include <cstdio>
#include <cstdlib>

#define CHECK(condition)                                                     \
    do {                                                                     \
        if (!(condition)) {                                                  \
            std::fprintf(stderr, "CHECK failed at %s:%d: %s\n",           \
                         __FILE__, __LINE__, #condition);                    \
            std::abort();                                                    \
        }                                                                    \
    } while (false)

namespace {

link::Frame request(uint8_t sender, uint8_t receiver, uint8_t command)
{
    link::Frame frame {};
    frame.sender_id = sender;
    frame.receiver_id = receiver;
    frame.cmd_set = boot_proto::CMD_SET;
    frame.cmd_id = command;
    return frame;
}

void test_unicast_policy()
{
    link::Frame frame = request(0x11U, boot_proto::LOADER_ADDRESS,
                                boot_proto::CMD_FW_TRANSFER);
    CHECK(boot_proto::request_envelope_allowed(frame));

    frame.sender_id = link::ADDR_BROADCAST;
    CHECK(!boot_proto::request_envelope_allowed(frame));
    frame.sender_id = link::ADDR_RESERVED;
    CHECK(!boot_proto::request_envelope_allowed(frame));
    frame.sender_id = 0x11U;

    frame.receiver_id = 0x21U;
    CHECK(!boot_proto::request_envelope_allowed(frame));
    frame.receiver_id = link::ADDR_RESERVED;
    CHECK(!boot_proto::request_envelope_allowed(frame));

    frame.receiver_id = boot_proto::LOADER_ADDRESS;
    frame.cmd_type = link::pack_cmd_type(
        false, link::AckMode::No, link::EncMode::Aes128,
        link::Priority::Low, false);
    CHECK(!boot_proto::request_envelope_allowed(frame));
    frame.cmd_type = link::pack_cmd_type(
        true, link::AckMode::No, link::EncMode::None,
        link::Priority::Low, false);
    CHECK(!boot_proto::request_envelope_allowed(frame));
}

void test_broadcast_is_discovery_only()
{
    link::Frame frame = request(0x11U, link::ADDR_BROADCAST,
                                boot_proto::CMD_QUERY_STATUS);
    CHECK(boot_proto::request_envelope_allowed(frame));

    frame.cmd_id = boot_proto::CMD_ENTER_LOADER;
    CHECK(!boot_proto::request_envelope_allowed(frame));
    frame.cmd_id = boot_proto::CMD_FW_TRANSFER;
    CHECK(!boot_proto::request_envelope_allowed(frame));
    frame.cmd_id = boot_proto::CMD_FW_VERIFY;
    CHECK(!boot_proto::request_envelope_allowed(frame));
    frame.cmd_id = boot_proto::CMD_REBOOT;
    CHECK(!boot_proto::request_envelope_allowed(frame));
}

} // namespace

int main()
{
    test_unicast_policy();
    test_broadcast_is_discovery_only();
    return 0;
}
