"""Physical motor-control commissioning over Link v1; every write is sent once."""
import argparse
import json
import math
import struct
import time
from pathlib import Path
from motor_tool import Client, mode_status, motor_status

SECTIONS = {
    "limits": "phase_current_a continuous_current_a excess_i2t_a2_s maximum_velocity_rad_s current_slew_a_s command_timeout_s temperature_timeout_s minimum_position_rad maximum_position_rad hard_limit_margin_rad following_error_rad following_time_s motoring_power_w regenerative_power_w derate_start_c derate_stop_c position_limits mechanical_power".split(),
    "joint": "joint_nm_per_iq_a bias_nm servo_stiffness servo_damping stiffness_rate damping_rate torque_limit_nm torque_rate_nm_s initial_energy_j maximum_energy_j external_timeout_s use_energy_tank use_model_external desired_inertia".split(),
    "admittance": "inertia damping stiffness position_limit velocity_limit acceleration_limit jerk_limit force_deadband".split(),
    "compensation": "gravity_bias_nm gravity_amplitude_nm gravity_phase_rad viscous_nm_s coulomb_nm breakaway_nm stribeck_velocity sign_velocity backlash_rad backlash_rate_rad_s load_inertia".split(),
    "elastic": "stiffness_nm_rad damping_nm_s zero_rad backlash_rad observer_alpha resonance_damping".split(),
    "force": "kp ki maximum_nm rate_nm_s contact_n release_n contact_time_s lever_arm_m approach_velocity release_distance maximum_travel_rad approach_timeout_s closing_direction".split(),
    "electrical": "resistance ld lq flux decoupling".split(),
    "cartesian": ["length_m", "mass_kg", "com_m", "inertia_kg_m2"] +
        [f"{name}_{axis}" for name in ("stiffness", "damping", "force_kp", "force_ki", "wrench_limit") for axis in "xyzXYZ"] +
        [f"force_axis_{axis}" for axis in "xyzXYZ"] + ["dynamics_feedforward"],
    "cartesian_admittance": [f"{axis}_{name}" for axis in "xyzXYZ" for name in
        "inertia damping stiffness position_limit velocity_limit acceleration_limit jerk_limit force_deadband".split()],
    "cogging": [f"nm_{i}" for i in range(64)],
}


def section_packet(section, values):
    index = list(SECTIONS).index(section)
    fields = SECTIONS[section]
    if set(values) != set(fields) or not all(math.isfinite(float(values[k])) for k in fields):
        raise ValueError("section requires exactly its documented finite fields")
    return struct.pack(f"<BBH{len(fields)}f", 1, index, len(fields), *(values[k] for k in fields))


def section_reply(section, data):
    fields = SECTIONS[section]
    if len(data) != 4 + 4*len(fields) or struct.unpack_from("<BBH", data) != (1, list(SECTIONS).index(section), len(fields)):
        raise ValueError("configuration schema mismatch")
    values = dict(zip(fields, struct.unpack_from(f"<{len(fields)}f", data, 4)))
    if not all(math.isfinite(v) for v in values.values()):
        raise ValueError("nonfinite configuration")
    return values


def read_section(client, section):
    return section_reply(section, client.request(0x17, bytes([0, list(SECTIONS).index(section)])))


def write_section(client, section, updates):
    values = read_section(client, section)
    if set(updates) - set(values):
        raise ValueError("unknown section fields")
    values.update(updates)
    packet = section_packet(section, values)
    client.request(0x17, b"\x01" + packet)
    received = read_section(client, section)
    if section_packet(section, received) != packet:
        raise ValueError("configuration readback differs; write was not retried")
    return received


def physical_packet(mode, action, sequence, values, timestamp=0):
    if mode not in list(range(4, 11))+[14] or action not in (0, 2, 3) or len(values) != 6 or not all(math.isfinite(v) for v in values):
        raise ValueError("invalid physical target")
    if any(v < 0 for v in values[3:]):
        raise ValueError("stiffness, damping and contact force must be nonnegative")
    return struct.pack("<BBBBI6fI4x", 2, action, mode, action != 3, sequence, *values, timestamp)


def cartesian_packet(mode, action, sequence, pose, velocity, wrench, timestamp=0):
    values = list(pose) + list(velocity) + list(wrench)
    if mode not in (11, 12, 13) or action not in (0, 2, 3) or len(values) != 18 or not all(math.isfinite(v) for v in values):
        raise ValueError("invalid Cartesian target")
    return struct.pack("<BBBBI18fI4x", 2, action, mode, action != 3, sequence, *values, timestamp)


def external_packet(sequence, external=None, effort=None, force=None, wrench=None, timestamp=0):
    if wrench is not None and len(wrench) != 6:
        raise ValueError("world wrench requires six values")
    flags = int(external is not None) | (int(effort is not None) << 1) | (int(force is not None) << 2) | (int(wrench is not None) << 3)
    values = [0 if x is None else x for x in (external, effort, force)] + (list(wrench) if wrench is not None else [0]*6)
    if not all(math.isfinite(v) for v in values) or values[2] < 0:
        raise ValueError("invalid external measurement")
    return struct.pack("<BBHII9f", 1, flags, 0, sequence, timestamp, *values)


def interaction_status(data):
    if len(data) != 132 or data[0] != 1 or data[3] or data[69:72] != b"\0\0\0":
        raise ValueError("invalid interaction status")
    names = "torque_nm requested_iq_a reference_rad reference_velocity external_estimate_nm elastic_effort_nm energy_j stiffness damping applied_iq_a iq_limit_a command_age_s invalid_temperature_s excess_i2t_a2_s electrical_power_w".split()
    result = dict(grip=data[1], flags=data[2], execution_faults=struct.unpack_from("<I", data, 4)[0])
    result.update(zip(names, struct.unpack_from("<15f", data, 8)))
    result["cartesian_valid"] = bool(data[68])
    result["cartesian_error"] = struct.unpack_from("<6f", data, 72)
    result["cartesian_wrench"] = struct.unpack_from("<6f", data, 96)
    result['external_sequence'], result['external_age_s'] = struct.unpack_from('<If', data, 120)
    result['external_flags'] = data[128]
    if data[128] & ~15 or any(data[129:]):
        raise ValueError('invalid external feedback status')
    return result


def electrical_fit(data):
    if len(data) != 24 or data[0] != 1 or data[1] > 1 or data[2:4] != b"\0\0":
        raise ValueError("invalid electrical fit")
    result = dict(valid=bool(data[1]), samples=struct.unpack_from("<I", data, 4)[0])
    result.update(zip(["resistance_ohm", "inductance_h", "offset_v", "rms_v"], struct.unpack_from("<4f", data, 8)))
    return result


def identify(client, apply=False):
    """Drain RL bursts while polling completion; cancellation is explicit cleanup."""
    client.request(8, b"\x01")
    started = False
    try:
        client.request(9)
        started = True
        deadline = time.monotonic()+8
        while time.monotonic() < deadline:
            status = motor_status(client.request(0))
            if status["rl_finished"]:
                started = False
                if status["rl_ret"]:
                    raise ValueError(f"identification failed: {status['rl_ret']}")
                return electrical_fit(client.request(0x1A, bytes([int(apply)])))
            time.sleep(0.02)
        raise TimeoutError("RL identification did not complete")
    finally:
        if started:
            client.request(5, b"\x01")
        client.request(8, b"\x00")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", required=True)
    parser.add_argument("--baud", type=int, default=2000000)
    sub = parser.add_subparsers(dest="command", required=True)
    read = sub.add_parser("config-read")
    read.add_argument("section", choices=SECTIONS)
    write = sub.add_parser("config-write")
    write.add_argument("section", choices=SECTIONS)
    write.add_argument("json_file", help="JSON object containing the fields to change")
    sub.add_parser("status")
    for name in ("rl-fit", "identify"):
        item = sub.add_parser(name)
        item.add_argument("--apply", action="store_true")
    torque = sub.add_parser("torque-cal")
    torque.add_argument("action", choices=["begin", "sample", "finish"])
    torque.add_argument("--measured-nm", type=float)
    external = sub.add_parser("external")
    external.add_argument("--sequence", type=int)
    external.add_argument("--external-nm", type=float)
    external.add_argument("--effort-nm", type=float)
    external.add_argument("--force-n", type=float)
    external.add_argument("--wrench", type=float, nargs=6)
    for name in ("joint", "cartesian"):
        item = sub.add_parser(name)
        item.add_argument("--mode", type=int, choices=list(range(4, 11))+[14] if name == "joint" else range(11, 14), required=True)
        item.add_argument("--action", choices=["target", "restore", "disable"], default="target")
        if name == "joint":
            item.add_argument("--values", type=float, nargs=6, default=[0]*6,
                              metavar="VALUE", help="q rad, v rad/s, Nm, Nm/rad, Nm s/rad, N")
        else:
            item.add_argument("--pose", type=float, nargs=6, default=[0]*6)
            item.add_argument("--velocity", type=float, nargs=6, default=[0]*6)
            item.add_argument("--wrench", type=float, nargs=6, default=[0]*6)
    args = parser.parse_args()
    import serial
    with serial.Serial(args.port, args.baud, timeout=0.05, write_timeout=1) as port:
        client = Client(port)
        stamp = int(time.monotonic()*1e6) & 0xFFFFFFFF
        if args.command == "config-read":
            result = read_section(client, args.section)
        elif args.command == "config-write":
            result = write_section(client, args.section, json.loads(Path(args.json_file).read_text(encoding="utf-8-sig")))
        elif args.command == "status":
            result = interaction_status(client.request(0x19))
        elif args.command == "rl-fit":
            result = electrical_fit(client.request(0x1A, bytes([args.apply])))
        elif args.command == "identify":
            result = identify(client, args.apply)
        elif args.command == "external":
            if args.sequence is None:
                args.sequence = (interaction_status(client.request(0x19))['external_sequence']+1) & 0xFFFFFFFF
            client.request(0x18, external_packet(args.sequence, args.external_nm, args.effort_nm, args.force_n, args.wrench, stamp))
            result = {"submitted_sequence": args.sequence}
        elif args.command == "torque-cal":
            action = ["begin", "sample", "finish"].index(args.action)
            if action == 1 and (args.measured_nm is None or not math.isfinite(args.measured_nm)):
                raise ValueError("sample requires finite --measured-nm")
            data = client.request(0x1B, bytes([action]) + (struct.pack("<f", args.measured_nm) if action == 1 else b""))
            result = dict(zip(["joint_nm_per_iq_a", "bias_nm", "rms_nm"], struct.unpack("<3f", data)))
        else:
            sequence = (mode_status(client.request(0x14))["accepted"]+1) & 0xFFFFFFFF
            action = dict(target=0, restore=2, disable=3)[args.action]
            packet = physical_packet(args.mode, action, sequence, args.values, stamp) if args.command == "joint" else cartesian_packet(
                args.mode, action, sequence, args.pose, args.velocity, args.wrench, stamp)
            result = mode_status(client.request(0x13, packet))
        print(json.dumps(result, indent=2))


if __name__ == "__main__":
    main()
