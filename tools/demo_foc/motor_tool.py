#!/usr/bin/env python3
"""Link v1 motor service client. Serial writes are sent once, without retries."""
import argparse
import binascii
import csv
import math
import json
import secrets
import struct
import time
from collections import deque
from pathlib import Path


RECORDS = dict(current=0, bus=1, input_tmr=2, output_tmr=3, electrical=4,
               input_lut=5, output_lut=6, zero=7, offset=8)


def crc8(data):
    value = 0
    for byte in data:
        value ^= byte
        for _ in range(8):
            value = ((value << 1) ^ (0x07 if value & 0x80 else 0)) & 0xFF
    return value


def frame(command, payload=b"", sequence=0, sender=0x20, receiver=0x10, ack=False):
    if len(payload) > 496:
        raise ValueError("payload exceeds 496 bytes")
    head = struct.pack("<BH", 0xAA, (1 << 10) | (13 + len(payload)))
    data = head + bytes([crc8(head)]) + struct.pack(
        "<BBBHBB", 1 if ack else 4, sender, receiver, sequence, 0x30, command) + payload
    return data + struct.pack("<H", binascii.crc_hqx(data, 0xFFFF))


class Parser:
    def __init__(self):
        self.data = bytearray()

    def feed(self, data):
        self.data.extend(data)
        frames = []
        while len(self.data) >= 4:
            if self.data[0] != 0xAA:
                del self.data[0]
                continue
            word = int.from_bytes(self.data[1:3], "little")
            size = word & 0x3FF
            if word >> 10 != 1 or not 13 <= size <= 512 or crc8(self.data[:3]) != self.data[3]:
                del self.data[0]
                continue
            if len(self.data) < size:
                break
            packet = bytes(self.data[:size])
            if binascii.crc_hqx(packet[:-2], 0xFFFF) != int.from_bytes(packet[-2:], "little"):
                del self.data[0]
                continue
            del self.data[:size]
            command_type, sender, receiver, sequence, command_set, command = struct.unpack_from(
                "<BBBHBB", packet, 4)
            if command_type & 0x98:  # Reserved/encrypted frames are unsupported here.
                continue
            frames.append((command_type, sender, receiver, sequence, command_set, command, packet[11:-2]))
        return frames


class Client:
    def __init__(self, transport, host=0x20, device=0x10, timeout=3.0):
        self.transport, self.host, self.device, self.timeout = transport, host, device, timeout
        self.sequence = secrets.randbelow(65536)
        self.parser, self.pending = Parser(), deque()
        self.on_frame = lambda packet: None

    def receive(self, deadline):
        while time.monotonic() < deadline:
            if self.pending:
                return self.pending.popleft()
            self.pending.extend(self.parser.feed(self.transport.read(512)))
        raise TimeoutError("Link response timed out; command was not retried")

    def request(self, command, payload=b""):
        self.sequence = (self.sequence + 1) & 0xFFFF
        packet = frame(command, payload, self.sequence, self.host, self.device)
        if self.transport.write(packet) != len(packet):
            raise OSError("short serial write; command was not retried")
        deadline = time.monotonic() + self.timeout
        while True:
            reply = self.receive(deadline)
            kind, sender, receiver, sequence, command_set, command_id, data = reply
            if sender != self.device or receiver != self.host or command_set != 0x30:
                continue
            if not kind & 1:
                self.on_frame(reply)
                continue
            if sequence != self.sequence or command_id != command or not data:
                continue
            if data[0] == 1:  # Accepted/in progress is not completion.
                continue
            if data[0]:
                raise RuntimeError(f"command 0x{command:02x} rejected: 0x{data[0]:02x}")
            return data[1:]


def read_record(client, record_id):
    values, offset, expected = [], 0, None
    while True:
        data = client.request(0x11, struct.pack("<BBHH", 0, record_id, offset, 0))
        if len(data) < 10:
            raise ValueError("truncated calibration response")
        version, received_id, valid, reserved, index, count, total = struct.unpack_from("<BBBBHHH", data)
        if (version, received_id, reserved, index) != (1, record_id, 0, offset) or len(data) != 10+4*count:
            raise ValueError("invalid calibration page")
        if not count or count > 120 or index+count > total or (expected and expected != (valid, total)):
            raise ValueError("inconsistent calibration pages")
        expected = valid, total
        values.extend(struct.unpack_from(f"<{count}f", data, 10))
        offset += count
        if offset == total:
            return valid, values


def lut_header(table, action, session, index=0, count=0):
    return struct.pack("<BBBBIHH", 2, table, action, 0, session, index, count)


def lut_reply(data, table, action, session, index=0, count=0):
    if len(data) != 16+4*count:
        raise ValueError("invalid LUT response size")
    version, table_id, action_id, status, token, start, size, checksum = struct.unpack_from("<BBBBIHHI", data)
    if (version, table_id, action_id, status, token, start, size) != (2, table, action, 0, session, index, count):
        raise ValueError("invalid LUT transaction response")
    return checksum


def read_lut(client, table):
    session, values = secrets.randbits(32) or 1, []
    checksum = lut_reply(client.request(0x0B, lut_header(table, 5, session)), table, 5, session)
    try:
        for start in range(0, 360, 120):
            data = client.request(0x0B, lut_header(table, 6, session, start, 120))
            if lut_reply(data, table, 6, session, start, 120) != checksum:
                raise ValueError("LUT changed during read")
            values.extend(struct.unpack_from("<120f", data, 16))
        if binascii.crc32(struct.pack("<360f", *values)) != checksum:
            raise ValueError("LUT readback CRC mismatch")
        return values
    finally:
        client.request(0x0B, lut_header(table, 4, session))


def write_lut(client, table, values):
    if len(values) != 360 or not all(math.isfinite(value) for value in values):
        raise ValueError("LUT requires 360 finite radian offsets")
    encoded = struct.pack("<360f", *values)
    session = secrets.randbits(32) or 1
    data = lut_header(table, 1, session, count=360) + struct.pack("<I", binascii.crc32(encoded))
    lut_reply(client.request(0x0B, data), table, 1, session)
    for start in range(0, 360, 120):
        data = lut_header(table, 2, session, start, 120) + encoded[start*4:(start+120)*4]
        lut_reply(client.request(0x0B, data), table, 2, session, start)
    lut_reply(client.request(0x0B, lut_header(table, 3, session)), table, 3, session)
    # Finish is acknowledged after verified persistence; verify via a new read session too.
    if struct.pack("<360f", *read_lut(client, table)) != encoded:
        raise ValueError("persisted LUT differs from requested table")


CAPTURE_FIELDS = ["version", "flags", "reserved", "timestamp_hz", "pair_seq", "joint_seq",
                  "joint_us", "input_us", "encoder_seq", "encoder_processed_us", "encoder_raw_count",
                  "dropped", "input_angle_rad", "output_raw_rad", "output_calibrated_rad"]


def capture_row(data):
    if len(data) != 52:
        raise ValueError("invalid capture packet size")
    row = dict(zip(CAPTURE_FIELDS, struct.unpack("<BBH9I3f", data)))
    if row["version"] != 1 or row["reserved"] or row["flags"] & ~7 or row["timestamp_hz"] != 1000000:
        raise ValueError("invalid capture packet header")
    row["encoder_age_us"] = (row["joint_us"]-row["encoder_processed_us"]) & 0xFFFFFFFF
    return row


RAW_FIELDS = ("iu_raw_a iv_raw_a iw_raw_a iu_calibrated_a iv_calibrated_a iw_calibrated_a "
              "iu_filtered_a iv_filtered_a iw_filtered_a bus_raw_v bus_calibrated_v bus_filtered_v "
              "rotor_tmr1_v rotor_tmr2_v output_tmr1_v output_tmr2_v input_angle_rad "
              "electrical_angle_rad output_raw_rad board_temp_c motor_temp_c").split()
CONTROL_FIELDS = ("target_position_rad target_velocity_rad_s target_iq_a target_kp target_kd "
                  "planned_position_rad planned_velocity_rad_s position_error_rad speed_output_a "
                  "mit_position_a mit_velocity_a foc_target_iq_a joint_feedback_iq_a foc_id_a foc_iq_a "
                  "ud_v uq_v position_rad velocity_rad_s motor_velocity_rad_s target_age_s").split()


def observation_row(data):
    if len(data) < 20:
        raise ValueError("truncated observation")
    version, kind, flags, fast, joint, input_us, joint_us = struct.unpack_from("<BBH4I", data)
    if version not in (1, 2) or kind > 3 or flags & ~(0x307 if version == 1 else 0xF07) or len(data) != [104, 108, 84, 48][kind]:
        raise ValueError("invalid observation header")
    row = dict(version=version, kind=kind, flags=flags, fast_seq=fast, joint_seq=joint,
               input_us=input_us, joint_us=joint_us, mode=(flags >> 8) & 15)
    if kind < 2:
        offset = 20
        if kind == 1:
            row["host_timestamp_us"] = struct.unpack_from("<I", data, 20)[0]
            offset = 24
        row.update(zip(RAW_FIELDS if kind == 0 else CONTROL_FIELDS,
                       struct.unpack_from("<21f", data, offset)))
    elif kind == 2:
        row["timestamp_hz"] = struct.unpack_from("<I", data, 20)[0]
        if row["timestamp_hz"] != 1000000:
            raise ValueError("unsupported timing units")
        for loop, name in enumerate(["current", "joint", "service"]):
            for field, value in zip(["count", "last_us", "maximum_us", "overruns", "maximum_interval_us"],
                                    struct.unpack_from("<5I", data, 24+loop*20)):
                row[f"{name}_{field}"] = value
    else:
        row["present_mask"], row["valid_mask"], reserved = struct.unpack_from("<BBH", data, 20)
        if reserved:
            raise ValueError("invalid temperature observation")
        for i, value in enumerate(struct.unpack_from("<6f", data, 24)):
            row[f"ntc_{i if i < 3 else i-3}_{'voltage_v' if i < 3 else 'temperature_c'}"] = value
    return row


def mode_status(data):
    if len(data) != 16:
        raise ValueError("invalid mode status size")
    version, mode, flags, reserved, accepted, applied, faults = struct.unpack("<BBBBIII", data)
    if (version, mode) not in [(1, m) for m in range(4)] + [(2, m) for m in range(4, 15)] or reserved or flags & ~15:
        raise ValueError("invalid mode status header")
    return dict(mode=mode, enabled=bool(flags & 1), holding=bool(flags & 2), busy=bool(flags & 4),
                locally_disabled=bool(flags & 8), accepted=accepted, applied=applied, faults=faults)


def motor_status(data):
    if len(data) != 49 or data[0] != 1:
        raise ValueError("invalid motor status")
    row = dict(version=1, flags=data[1], calibration_state=data[2], backlash_ret=data[3],
               protection=struct.unpack_from("<I", data, 4)[0])
    row.update(zip(["position_rad", "velocity_rad_s", "iq_a", "bus_v", "backlash_rad"],
                   struct.unpack_from("<5f", data, 8)))
    row.update(zip(["rl_finished", "rl_ret", "sweep_finished", "sweep_ret"], data[28:32]))
    row["dropped_rl"], row["dropped_sweep"] = struct.unpack_from("<II", data, 32)
    row["calibration_kind"] = data[40]
    row["calibration_samples"], row["calibration_motions"] = struct.unpack_from("<II", data, 41)
    return row


def collect_csv(client, path, seconds, command, start, stop, stream, decoder):
    if not math.isfinite(seconds) or seconds <= 0:
        raise ValueError("seconds must be finite and positive")
    with Path(path).open("w", newline="", encoding="utf-8") as output:
        writer = None
        def consume(packet):
            nonlocal writer
            if not packet[0] & 1 and packet[5] == stream:
                row = decoder(packet[6])
                if writer is None:
                    writer = csv.DictWriter(output, list(row))
                    writer.writeheader()
                writer.writerow(row)
        previous = client.on_frame
        client.on_frame = consume
        started = False
        try:
            client.request(command, start)
            started = True
            deadline = time.monotonic()+seconds
            while time.monotonic() < deadline:
                try:
                    packet = client.receive(deadline)
                except TimeoutError:
                    break
                if packet[1:3] == (client.device, client.host) and packet[4] == 0x30:
                    consume(packet)
        finally:
            try:
                if started:
                    client.request(command, stop)
            finally:
                client.on_frame = previous


def capture_csv(client, path, seconds, divider):
    if not 4 <= divider <= 65535:
        raise ValueError("divider must be 4..65535")
    collect_csv(client, path, seconds, 0x16, struct.pack("<BH", 1, divider), b"\0\0\0",
                0x85, capture_row)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", required=True)
    parser.add_argument("--baud", type=int, default=2000000)
    sub = parser.add_subparsers(dest="command", required=True)
    for command in ["read", "erase"]:
        item = sub.add_parser(command)
        item.add_argument("record", choices=list(RECORDS)+(["all"] if command == "erase" else []))
    observe = sub.add_parser("observe")
    observe.add_argument("kind", choices=["raw", "control", "timing", "temperature"])
    capture = sub.add_parser("capture")
    capture.add_argument("file")
    capture.add_argument("--seconds", type=float, default=10)
    capture.add_argument("--divider", type=int, default=20)
    telemetry = sub.add_parser("telemetry")
    telemetry.add_argument("file")
    telemetry.add_argument("--kind", choices=["raw", "control", "timing", "temperature"], default="control")
    telemetry.add_argument("--seconds", type=float, default=10)
    telemetry.add_argument("--period", type=int, default=20)
    for command in ["lut-read", "lut-write"]:
        item = sub.add_parser(command)
        item.add_argument("file")
        item.add_argument("--table", choices=["output", "input"], default="output")
    for command, count in [("current-linear", 6), ("bus-linear", 2)]:
        item = sub.add_parser(command)
        item.add_argument("coefficients", type=float, nargs=count, metavar="VALUE")
    mode = sub.add_parser("mode")
    mode.add_argument("action", choices=["target", "hold", "restore", "disable", "select"])
    mode.add_argument("--mode", type=int, choices=range(4), default=2)
    mode.add_argument("--position", type=float, default=0)
    mode.add_argument("--velocity", type=float, default=0)
    mode.add_argument("--iq", type=float, default=0)
    mode.add_argument("--kp", type=int, choices=range(256), default=0)
    mode.add_argument("--kd", type=int, choices=range(256), default=0)
    mode.add_argument("--disabled", action="store_true")
    sub.add_parser("mode-status")
    sub.add_parser("status")
    sub.add_parser("clear-fault")
    stop = sub.add_parser("stop")
    stop.add_argument("--emergency", action="store_true")
    calibrate = sub.add_parser("calibrate")
    calibrate.add_argument("kind", choices=["all", "input", "electrical", "output"])
    plan = sub.add_parser("plan")
    plan.add_argument("kind", choices=["s-curve", "linear"])
    args = parser.parse_args()
    import serial  # Optional dependency; offline codec tests use only the standard library.
    with serial.Serial(args.port, args.baud, timeout=0.05, write_timeout=1) as port:
        client = Client(port)
        if args.command == "read":
            record = RECORDS[args.record]
            valid, values = (1, read_lut(client, 1 if record == 5 else 0)) if record in (5, 6) else read_record(client, record)
            print({"valid_mask": valid, "values": values})
        elif args.command == "erase":
            record = 255 if args.record == "all" else RECORDS[args.record]
            client.request(0x11, struct.pack("<BBHH", 1, record, 0, 0))
        elif args.command == "observe":
            print(json.dumps(observation_row(client.request(0x12, bytes([
                ["raw", "control", "timing", "temperature"].index(args.kind)]))), indent=2))
        elif args.command == "capture":
            capture_csv(client, args.file, args.seconds, args.divider)
        elif args.command == "telemetry":
            if not 5 <= args.period <= 65535:
                raise ValueError("telemetry period must be 5..65535 ms")
            kind = ["raw", "control", "timing", "temperature"].index(args.kind)
            collect_csv(client, args.file, args.seconds, 0x15,
                        struct.pack("<BBH", 1, kind, args.period), bytes([0, kind, 0, 0]),
                        0x84, observation_row)
        elif args.command.startswith("lut-"):
            table = ["output", "input"].index(args.table)
            if args.command == "lut-write":
                with Path(args.file).open(newline="", encoding="utf-8-sig") as source:
                    rows = list(csv.DictReader(source))
                if [int(row["index"]) for row in rows] != list(range(360)):
                    raise ValueError("CSV index must be 0..359 in order")
                write_lut(client, table, [float(row["offset_rad"]) for row in rows])
            else:
                values = read_lut(client, table)
                with Path(args.file).open("w", newline="", encoding="utf-8") as output:
                    writer = csv.writer(output)
                    writer.writerow(["index", "offset_rad"])
                    writer.writerows(enumerate(values))
        elif args.command in ["current-linear", "bus-linear"]:
            values = args.coefficients
            if not all(math.isfinite(v) for v in values) or any(abs(v) <= 1e-6 for v in values[1::2]):
                raise ValueError("coefficients require finite offsets and nonzero finite gains")
            current = args.command == "current-linear"
            client.request(0x0E if current else 0x0F, struct.pack(f"<{len(values)}f", *values))
            valid, received = read_record(client, 0 if current else 1)
            if valid != (7 if current else 1) or struct.pack(f"<{len(values)}f", *received) != struct.pack(f"<{len(values)}f", *values):
                raise ValueError("linear calibration readback differs")
        elif args.command in ["status", "clear-fault", "stop", "calibrate", "plan"]:
            commands = dict(status=0, **{"clear-fault": 6}, stop=5, calibrate=4, plan=0x10)
            payload = b""
            if args.command == "stop":
                payload = bytes([args.emergency])
            elif args.command == "calibrate":
                payload = bytes([dict(all=0, input=3, electrical=4, output=5)[args.kind]])
            elif args.command == "plan":
                payload = bytes([["s-curve", "linear"].index(args.kind)])
            print(json.dumps(motor_status(client.request(commands[args.command], payload)), indent=2))
        else:
            status = mode_status(client.request(0x14))
            if args.command == "mode-status":
                print(json.dumps(status, indent=2))
                return
            sequence = (status["accepted"]+1) & 0xFFFFFFFF
            action = ["target", "hold", "restore", "disable", "select"].index(args.action)
            enabled = action != 3 and not args.disabled
            payload = struct.pack("<BBBBIfffBBHI4x", 1, action, args.mode, enabled, sequence,
                                  args.position, args.velocity, args.iq, args.kp, args.kd, 0,
                                  int(time.monotonic()*1e6) & 0xFFFFFFFF)
            print(json.dumps(mode_status(client.request(0x13, payload)), indent=2))


if __name__ == "__main__":
    main()
