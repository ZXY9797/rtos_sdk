import binascii
import struct
import unittest
import tempfile
from pathlib import Path
from itertools import count
from unittest import mock

import motor_tool as tool


class Transport:
    def __init__(self, reply):
        self.reply, self.data, self.writes = reply, b"", []

    def write(self, data):
        self.writes.append(data)
        self.data += self.reply(tool.Parser().feed(data)[0])
        return len(data)

    def read(self, size):
        data, self.data = self.data[:size], self.data[size:]
        return data


class MotorToolTests(unittest.TestCase):
    def test_crc_and_fragmented_corrupt_stream(self):
        self.assertEqual(tool.crc8(b"123456789"), 0xF4)
        self.assertEqual(binascii.crc_hqx(b"123456789", 0xFFFF), 0x29B1)
        packet = tool.frame(0x12, b"\x02", 0x1234)
        self.assertEqual(packet[4:11], bytes.fromhex("04201034123012"))
        bad = bytearray(packet)
        bad[-1] ^= 1
        parser = tool.Parser()
        replies = []
        for byte in b"noise"+bad+packet+packet:
            replies.extend(parser.feed(bytes([byte])))
        self.assertEqual(len(replies), 2)
        self.assertEqual(replies[0][3], 0x1234)
        self.assertEqual(replies[0][-1], b"\x02")

    def test_final_ack_correlation_and_async_frame(self):
        def reply(request):
            seq = request[3]
            return (tool.frame(0x11, b"\x01", seq, 0x10, 0x20, True) +
                    tool.frame(0x85, b"sample", 0, 0x10, 0x20) +
                    tool.frame(0x11, b"\0old", (seq-1) & 65535, 0x10, 0x20, True) +
                    tool.frame(0x11, b"\0done", seq, 0x10, 0x20, True))
        transport = Transport(reply)
        client = tool.Client(transport)
        observed = []
        client.on_frame = observed.append
        self.assertEqual(client.request(0x11), b"done")
        self.assertEqual(len(transport.writes), 1)
        self.assertEqual(observed[0][5], 0x85)

    def test_rejection_and_timeout_never_retry_writes(self):
        transport = Transport(lambda r: tool.frame(r[5], b"\x02", r[3], 0x10, 0x20, True))
        with self.assertRaisesRegex(RuntimeError, "0x02"):
            tool.Client(transport).request(0x11, b"erase")
        self.assertEqual(len(transport.writes), 1)
        silent = Transport(lambda _: b"")
        with self.assertRaises(TimeoutError):
            tool.Client(silent, timeout=0.001).request(0x13, b"move")
        self.assertEqual(len(silent.writes), 1)

    def test_read_calibration_pages(self):
        class Reader:
            def request(self, command, payload):
                self_test.assertEqual(command, 0x11)
                operation, record, offset, count = struct.unpack("<BBHH", payload)
                self_test.assertEqual((operation, record, count), (0, 6, 0))
                return struct.pack("<BBBBHHH120f", 1, 6, 1, 0, offset, 120, 360,
                                   *[float(i) for i in range(offset, offset+120)])
        self_test = self
        valid, values = tool.read_record(Reader(), 6)
        self.assertEqual(valid, 1)
        self.assertEqual(values, list(range(360)))

    def test_capture_wrap_and_gaps(self):
        data = struct.pack("<BBH6I3f", 2, 1, 0, 1000000, 99, 400, 5,
                           0xFFFFFFF0, 7, 0.25, 0.5, 0.75)
        row = tool.capture_row(data)
        self.assertEqual((row["joint_us"], row["input_us"]), (5, 0xFFFFFFF0))
        self.assertEqual(row["dropped"], 7)
        with self.assertRaises(ValueError):
            tool.capture_row(data[:-1])
        with self.assertRaises(ValueError):
            tool.capture_row(bytes([1])+data[1:])

    def test_lut_write_and_new_session_readback(self):
        self_test = self
        class Firmware:
            def __init__(self):
                self.staging = bytearray(1440)
                self.saved, self.actions = None, []

            def request(self, command, payload):
                self_test.assertEqual(command, 0x0B)
                version, table, action, reserved, token, index, count = struct.unpack_from("<BBBBIHH", payload)
                self_test.assertEqual((version, table, reserved), (2, 0, 0))
                self.actions.append(action)
                crc = 0
                if action == 1:
                    self_test.assertEqual(count, 360)
                    self.crc = struct.unpack_from("<I", payload, 12)[0]
                    self.write_token = token
                elif action == 2:
                    self_test.assertEqual(token, self.write_token)
                    self_test.assertEqual(count, 120)
                    self.staging[index*4:(index+count)*4] = payload[12:]
                elif action == 3:
                    self_test.assertEqual(binascii.crc32(self.staging), self.crc)
                    self.saved = bytes(self.staging)
                elif action == 5:
                    self_test.assertNotEqual(token, self.write_token)
                    crc = binascii.crc32(self.saved)
                elif action == 6:
                    crc = binascii.crc32(self.saved)
                    return struct.pack("<BBBBIHHI", 2, table, action, 0, token, index, count, crc) + self.saved[index*4:(index+count)*4]
                return struct.pack("<BBBBIHHI", 2, table, action, 0, token, index, 0, crc)
        firmware = Firmware()
        values = [i/10000 for i in range(360)]
        tool.write_lut(firmware, 0, values)
        self.assertEqual(firmware.saved, struct.pack("<360f", *values))
        self.assertEqual(firmware.actions, [1, 2, 2, 2, 3, 5, 6, 6, 6, 4])
        with self.assertRaises(ValueError):
            tool.write_lut(firmware, 0, [float("nan")]*360)

    def test_observation_and_mode_decoding(self):
        head = struct.pack("<BBH4I", 1, 0, 0x201, 10, 20, 30, 40)
        raw = tool.observation_row(head + struct.pack("<21f", *range(21)))
        self.assertEqual(raw["mode"], 2)
        self.assertEqual(raw["output_tmr2_v"], 15)
        self.assertEqual(raw["motor_temp_c"], 20)
        head = struct.pack("<BBH4I", 1, 1, 0x302, 10, 20, 30, 40)
        control = tool.observation_row(head+struct.pack("<I21f", 55, *range(21)))
        self.assertEqual(control["host_timestamp_us"], 55)
        self.assertEqual(control["mit_position_a"], 9)
        self.assertEqual(control["joint_feedback_iq_a"], 12)
        data = struct.pack("<BBBBIII", 1, 2, 6, 0, 8, 7, 4)
        self.assertTrue(tool.mode_status(data)["holding"])
        self.assertTrue(tool.mode_status(data)["busy"])
        with self.assertRaises(ValueError):
            tool.mode_status(data[:-1])
        with self.assertRaises(ValueError):
            tool.observation_row(head)
        status = bytes([1, 3, 2, 0])+struct.pack("<I5f4BIIBII", 4, 1, 2, 3, 24, 0.1,
                                                               1, 0, 0, 0, 5, 6, 3, 7, 8)
        self.assertEqual(tool.motor_status(status)["calibration_samples"], 7)

    def test_csv_subscription_cleanup_and_rejected_owner(self):
        sample = struct.pack("<BBH6I3f", 2, 1, 0, 1000000, 1, 2, 3, 4, 0, 0.1, 0.2, 0.3)
        def reply(request):
            ack = tool.frame(0x16, b"\0", request[3], 0x10, 0x20, True)
            return ack + (tool.frame(0x85, sample, 0, 0x10, 0x20) if request[6][0] else b"")
        transport = Transport(reply)
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder)/"capture.csv"
            with mock.patch.object(tool.time, "monotonic", side_effect=count(0.0, 0.001)):
                tool.capture_csv(tool.Client(transport), path, 0.01, 20)
            self.assertIn("output_raw_rad", path.read_text())
            self.assertEqual(len(path.read_text().splitlines()), 2)
            self.assertEqual(len(transport.writes), 2)
            self.assertEqual(tool.Parser().feed(transport.writes[-1])[0][6], b"\0\0\0")
            rejected = Transport(lambda r: tool.frame(0x16, b"\x02", r[3], 0x10, 0x20, True))
            with self.assertRaises(RuntimeError):
                tool.capture_csv(tool.Client(rejected), path, 0.005, 20)
            self.assertEqual(len(rejected.writes), 1)


if __name__ == "__main__":
    unittest.main()
