"""Capture the pass record from an OSAL contract firmware's UART."""
import argparse
from datetime import datetime, timezone
import hashlib
import json
import math
from pathlib import Path
import re
import sys
import tempfile
import time


def add_arguments(parser: argparse.ArgumentParser):
    parser.add_argument("-B", "--build", type=Path, required=True, help="linked OSAL contract build")
    parser.add_argument("--port", required=True, help="serial device (e.g. COM3 or /dev/ttyUSB0)")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--timeout", type=float, default=30, help="capture deadline in seconds")
    parser.add_argument("--output", type=Path, help="parent directory for per-run evidence")


def expected_run(build):
    if not (build / "build-info.json").is_file() and (build / "sdk/build-info.json").is_file():
        build = build / "sdk"
    matrix = json.loads((Path(__file__).resolve().parents[1] / "test_matrix.json").read_text(encoding="utf-8"))
    contract = next(c for c in matrix["target_acceptance"] if c["name"] == "osal-contract")
    info = json.loads((build / "build-info.json").read_text(encoding="utf-8"))
    if (info["product"] not in contract["products"] or info["firmware"] != "app"
            or info["application_path"] != "SDK_ROOT/samples/osal_contract"):
        raise ValueError("select the linked samples/osal_contract application")
    manifest = json.loads((build / (info["product"] + ".manifest.json")).read_text(encoding="utf-8"))
    config = (build / ".config").read_bytes()
    if (manifest["build"] != info or hashlib.sha256(config).hexdigest() != info["inputs"]["config"]
            or not re.fullmatch("[0-9a-f]{64}", info["build_id"])):
        raise ValueError("configuration differs from the linked image; rebuild the target")
    backends = [name for name in contract["backends"]
                if "CONFIG_RTOS_" + name.upper().replace("-", "_") + "=y" in config.decode().splitlines()]
    if len(backends) != 1:
        raise ValueError("expected exactly one OSAL contract backend")
    return info["build_id"], backends[0], contract["success_marker"]


def capture(port, raw, marker, build_id, timeout):
    expected = f"{marker} {build_id}".encode("ascii")
    deadline = time.monotonic() + timeout
    partial = bytearray()
    discard = False
    while time.monotonic() < deadline:
        chunk = port.read(min(4096, max(1, port.in_waiting)))
        raw.write(chunk)
        for byte in chunk:
            if byte == 10:
                line = bytes(partial).removesuffix(b"\r")
                if not discard and line == expected:
                    return line.decode("ascii")
                if not discard and line.startswith(marker.encode("ascii") + b" "):
                    raise ValueError("OSAL pass record belongs to a different build")
                partial.clear()
                discard = False
            elif len(partial) < 512:
                partial.append(byte)
            else:
                discard = True
    raise TimeoutError("no complete OSAL pass record for this build before the deadline")


def run(args):
    if not math.isfinite(args.timeout) or args.timeout <= 0 or args.baud <= 0:
        print("hil: baud and timeout must be positive finite values", file=sys.stderr)
        return 2
    parent = args.output or args.build / "hil"
    parent.mkdir(parents=True, exist_ok=True)
    directory = Path(tempfile.mkdtemp(prefix="osal-", dir=parent))
    result = {"test": "osal-contract", "status": "failed", "port": args.port, "baud": args.baud,
              "started_utc": datetime.now(timezone.utc).isoformat(), "timeout_s": args.timeout}
    try:
        build_id, backend, marker = expected_run(args.build)
        result.update(build_id=build_id, backend=backend)
        try:
            import serial
        except ImportError as error:
            raise ValueError("serial capture requires pyserial (python -m pip install pyserial)") from error
        with (directory / "uart.bin").open("wb") as raw, serial.Serial(
            args.port, args.baud, timeout=min(0.1, args.timeout)
        ) as port:
            port.reset_input_buffer()
            print(f"Listening on {args.port}. Start the selected OSAL firmware now.", flush=True)
            result["received"] = capture(port, raw, marker, build_id, args.timeout)
        result["status"] = "passed"
    except (OSError, ValueError, KeyError) as error:
        result["error"] = str(error)
        print(f"hil: {error}", file=sys.stderr)
    result["finished_utc"] = datetime.now(timezone.utc).isoformat()
    report = directory / "result.json"
    report.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    print(f"{result['status']}: {report}")
    return int(result["status"] != "passed")
