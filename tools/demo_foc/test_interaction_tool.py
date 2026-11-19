import struct
import unittest
import interaction_tool as tool
import motor_tool
import check_elf


class InteractionToolTest(unittest.TestCase):
    def test_unreachable_control_image_is_rejected(self):
        symbols = "\n".join(check_elf.REQUIRED)
        self.assertEqual(check_elf.missing_callbacks(symbols), [])
        self.assertEqual(check_elf.missing_callbacks("app::start_control()"), list(check_elf.REQUIRED[:3]))

    def test_schema(self):
        self.assertEqual([len(v) for v in tool.SECTIONS.values()], [18, 14, 8, 11, 6, 13, 5, 41, 48, 64, 12])
        for section, names in tool.SECTIONS.items():
            values = dict.fromkeys(names, 0.0)
            packet = tool.section_packet(section, values)
            self.assertEqual(tool.section_reply(section, packet), values)
            with self.assertRaises(ValueError):
                tool.section_reply(section, packet[:-1])

    def test_physical_wire_and_status(self):
        packet = tool.physical_packet(5, 0, 123, [1, 2, 3, 4, 5, 6], 456)
        self.assertEqual(len(packet), 40)
        self.assertEqual(packet[:8], bytes([2, 0, 5, 1, 123, 0, 0, 0]))
        self.assertEqual(struct.unpack_from("<I", packet, 32)[0], 456)
        self.assertEqual(packet[36:], bytes(4))
        self.assertEqual(len(tool.cartesian_packet(11, 2, 4, [0]*6, [0]*6, [0]*6)), 88)
        self.assertEqual(motor_tool.mode_status(struct.pack("<BBBBIII", 2, 13, 1, 0, 5, 4, 0))["mode"], 13)
        with self.assertRaises(ValueError):
            tool.physical_packet(5, 0, 1, [0, 0, 0, -1, 0, 0])

    def test_joint_fd_wire(self):
        wire = tool.joint_pdo(15, -1, 239, 0xFFFFFFFF, 7, 4, -.5, 2, -.25,
                              stiffness=1.5, damping=.5, external_nm=0, external_sequence=8,
                              support_a=-.125)
        self.assertEqual(len(wire), 48)
        self.assertEqual(wire[:4], bytes([15, 0, 255, 239]))
        self.assertEqual(struct.unpack_from("<3i", wire, 12), (-500000, 2000000, -250))
        self.assertEqual(struct.unpack_from("<HHBBh", wire, 40), (4, 0, 3, 0, -125))
        ack = tool.decode_joint_ack(struct.pack("<7I4B2i2I", *range(11), -250, 400, 350, 1 << 31))
        self.assertEqual(ack["output_mnm"], -250)
        self.assertEqual(ack["first_fault"], 1 << 31)

    def test_measurement_validity(self):
        packet = tool.external_packet(7, external=-1, force=2, wrench=[1]*6)
        self.assertEqual(len(packet), 48)
        self.assertEqual(packet[1], 13)
        self.assertEqual(struct.unpack_from("<3f", packet, 12), (-1, 0, 2))
        self.assertEqual(tool.external_packet(8)[1], 0)
        with self.assertRaises(ValueError):
            tool.external_packet(9, force=-1)

    def test_failed_readback_does_not_repeat_write(self):
        class Fake:
            def __init__(self):
                self.writes = 0
            def request(self, command, data):
                if data[0]:
                    self.writes += 1
                    return b""
                return tool.section_packet("joint", dict.fromkeys(tool.SECTIONS["joint"], 0.0))
        client = Fake()
        with self.assertRaises(ValueError):
            tool.write_section(client, "joint", {"joint_nm_per_iq_a": 1})
        self.assertEqual(client.writes, 1)

    def test_temperature_and_runtime_packets(self):
        packet = struct.pack("<BBH4IBBH6f", 1, 3, 1, 0, 0, 0, 0, 7, 5, 0, *range(6))
        row = motor_tool.observation_row(packet)
        self.assertEqual(row["valid_mask"], 5)
        self.assertEqual(row["ntc_2_temperature_c"], 5)
        runtime = bytearray(132)
        runtime[0] = 1
        self.assertFalse(tool.interaction_status(runtime)["cartesian_valid"])
        runtime[69] = 1
        with self.assertRaises(ValueError):
            tool.interaction_status(runtime)


if __name__ == "__main__":
    unittest.main()
