#!/usr/bin/env python3
"""Read allowlisted MCU RAM through a hidden Ozone CM_ATTACH session.

This controller deliberately exposes no reset, halt, continue, download, breakpoint,
register-write, or memory-write operation. It is intended to run on the Windows PC
that owns the USB J-Link. All machine-readable output is emitted as one JSON object.
"""

from __future__ import annotations

import argparse
import csv
import hashlib
import json
import os
import re
import shutil
import socket
import subprocess
import sys
import tempfile
import time
import uuid
from dataclasses import dataclass
from datetime import datetime, timezone
from pathlib import Path
from typing import Any, Iterable


SCHEMA_VERSION = 1
LOCAL_HOST = "127.0.0.1"
DEFAULT_CONFLICTING_PROCESSES = {
    "ozone.exe",
    "jlink.exe",
    "jlinkgdbserver.exe",
    "jlinkgdbservercl.exe",
    "jlinkrttclient.exe",
    "jlinkrttlogger.exe",
    "jscope.exe",
}
DEFAULT_DISTURBANCE_PATTERNS = (
    r"\bsysresetreq\b",
    r"\breset(?:ting)?\s+(?:the\s+)?target\b",
    r"\btarget\s+reset(?:ted)?\b",
    r"\bperforming\b.{0,40}\breset\b",
    r"\bhalting\s+(?:the\s+)?(?:cpu|core|target)\b",
    r"\b(?:cpu|core|target)\s+(?:is\s+)?halted\b",
    r"\bdownload(?:ing)?\s+(?:program|file|debuggee|image)\b",
    r"\bflash\s+(?:download|programming|erase)\b",
    r"\bwork\s*ram\b",
)
FORBIDDEN_PROJECT_TOKENS = (
    "Debug.Halt",
    "Debug.Reset",
    "Debug.Continue",
    "Debug.Download",
    "Exec.Reset",
    "Exec.Download",
    "Target.Write",
    "Target.SetReg",
    "File.Load",
)


class LiveAttachError(RuntimeError):
    pass


class DisturbanceDetected(LiveAttachError):
    pass


def utc_now() -> str:
    return datetime.now(timezone.utc).isoformat(timespec="milliseconds").replace("+00:00", "Z")


def parse_int(value: Any, field: str) -> int:
    if isinstance(value, bool):
        raise LiveAttachError(f"{field} must be an integer, not boolean")
    if isinstance(value, int):
        return value
    if isinstance(value, str):
        try:
            return int(value, 0)
        except ValueError as exc:
            raise LiveAttachError(f"{field} is not an integer: {value!r}") from exc
    raise LiveAttachError(f"{field} must be an integer or 0x-prefixed string")


def require_text(config: dict[str, Any], key: str) -> str:
    value = config.get(key)
    if not isinstance(value, str) or not value.strip():
        raise LiveAttachError(f"missing non-empty string: {key}")
    return value.strip()


def resolve_path(value: str, base: Path) -> Path:
    path = Path(os.path.expandvars(value)).expanduser()
    if not path.is_absolute():
        path = base / path
    return path.resolve()


def c_string(value: str) -> str:
    normalized = value.replace("\\", "/")
    return normalized.replace("\\", "\\\\").replace('"', '\\"')


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def require_elf(path: Path) -> None:
    try:
        with path.open("rb") as stream:
            magic = stream.read(4)
    except OSError as exc:
        raise LiveAttachError(f"unable to read ELF: {path}: {exc}") from exc
    if magic != b"\x7fELF":
        raise LiveAttachError(f"program file is not ELF: {path}")


@dataclass(frozen=True)
class RamRange:
    name: str
    start: int
    end: int

    def contains(self, address: int, size: int) -> bool:
        return size > 0 and self.start <= address and address + size <= self.end


@dataclass(frozen=True)
class ReadItem:
    name: str
    address: int
    width: int
    count: int
    source: dict[str, Any]

    @property
    def byte_width(self) -> int:
        return self.width // 8

    @property
    def byte_size(self) -> int:
        return self.byte_width * self.count


def load_symbols(tool: Path, elf: Path) -> dict[str, int]:
    completed = subprocess.run(
        [str(tool), "-n", "--defined-only", str(elf)],
        capture_output=True,
        text=True,
        encoding="utf-8",
        errors="replace",
        timeout=30,
        check=False,
    )
    if completed.returncode != 0:
        detail = completed.stderr.strip() or completed.stdout.strip()
        raise LiveAttachError(f"symbol tool failed ({completed.returncode}): {detail}")

    symbols: dict[str, int] = {}
    for line in completed.stdout.splitlines():
        match = re.match(r"^\s*([0-9A-Fa-f]+)\s+\S\s+(.+?)\s*$", line)
        if match:
            symbols[match.group(2)] = int(match.group(1), 16)
    if not symbols:
        raise LiveAttachError("symbol tool returned no defined symbols")
    return symbols


def parse_ranges(config: dict[str, Any]) -> list[RamRange]:
    raw_ranges = config.get("allowed_ram_ranges")
    if not isinstance(raw_ranges, list) or not raw_ranges:
        raise LiveAttachError("allowed_ram_ranges must be a non-empty list")

    ranges: list[RamRange] = []
    for index, raw in enumerate(raw_ranges):
        if not isinstance(raw, dict):
            raise LiveAttachError(f"allowed_ram_ranges[{index}] must be an object")
        if raw.get("kind", "ram") != "ram":
            raise LiveAttachError(f"allowed_ram_ranges[{index}].kind must be 'ram'")
        start = parse_int(raw.get("start"), f"allowed_ram_ranges[{index}].start")
        end = parse_int(raw.get("end"), f"allowed_ram_ranges[{index}].end")
        if start < 0 or end <= start:
            raise LiveAttachError(f"allowed_ram_ranges[{index}] has invalid bounds")
        ranges.append(RamRange(str(raw.get("name", f"ram_{index}")), start, end))
    return ranges


def parse_reads(
    config: dict[str, Any], symbols: dict[str, int], ranges: list[RamRange]
) -> list[ReadItem]:
    raw_reads = config.get("reads")
    if not isinstance(raw_reads, list) or not raw_reads:
        raise LiveAttachError("reads must be a non-empty list")

    reads: list[ReadItem] = []
    names: set[str] = set()
    for index, raw in enumerate(raw_reads):
        if not isinstance(raw, dict):
            raise LiveAttachError(f"reads[{index}] must be an object")
        name = raw.get("name")
        if not isinstance(name, str) or not name.strip():
            raise LiveAttachError(f"reads[{index}].name must be non-empty")
        if name in names:
            raise LiveAttachError(f"duplicate read name: {name}")
        names.add(name)

        has_address = "address" in raw
        has_symbol = "symbol" in raw
        if has_address == has_symbol:
            raise LiveAttachError(f"reads[{index}] must specify exactly one of address or symbol")
        offset = parse_int(raw.get("offset", 0), f"reads[{index}].offset")
        if has_symbol:
            symbol = raw["symbol"]
            if not isinstance(symbol, str) or not symbol:
                raise LiveAttachError(f"reads[{index}].symbol must be non-empty")
            if symbol not in symbols:
                raise LiveAttachError(f"symbol not found in ELF: {symbol}")
            address = symbols[symbol] + offset
            source = {"symbol": symbol, "offset": offset}
        else:
            address = parse_int(raw["address"], f"reads[{index}].address") + offset
            source = {"address": f"0x{address:X}", "offset": offset}

        width = parse_int(raw.get("width", 32), f"reads[{index}].width")
        count = parse_int(raw.get("count", 1), f"reads[{index}].count")
        if width not in (8, 16, 32):
            raise LiveAttachError(f"reads[{index}].width must be 8, 16, or 32")
        if count < 1 or count > 4096:
            raise LiveAttachError(f"reads[{index}].count must be in 1..4096")
        if address < 0 or address % (width // 8) != 0:
            raise LiveAttachError(f"reads[{index}] address is invalid or unaligned")

        item = ReadItem(name, address, width, count, source)
        if not any(region.contains(item.address, item.byte_size) for region in ranges):
            raise LiveAttachError(
                f"read {name!r} at 0x{address:X} size {item.byte_size} is outside allowed RAM"
            )
        reads.append(item)
    return reads


def running_processes() -> set[str]:
    if os.name != "nt":
        return set()
    completed = subprocess.run(
        ["tasklist", "/FO", "CSV", "/NH"],
        capture_output=True,
        text=True,
        encoding="utf-8",
        errors="replace",
        timeout=10,
        check=False,
    )
    if completed.returncode != 0:
        raise LiveAttachError("unable to inspect Windows process list")
    names: set[str] = set()
    for row in csv.reader(completed.stdout.splitlines()):
        if row:
            names.add(row[0].lower())
    return names


def parse_probe_list(output: str) -> list[dict[str, str]]:
    pattern = re.compile(
        r"J-Link\[(?P<index>\d+)\]:\s*Connection:\s*(?P<connection>[^,]+),\s*"
        r"Serial number:\s*(?P<serial>[^,\r\n]+),\s*ProductName:\s*(?P<product>[^,\r\n]+)"
        r"(?:,\s*Nickname:\s*(?P<nickname>[^\r\n]+))?",
        re.IGNORECASE,
    )
    probes: list[dict[str, str]] = []
    for match in pattern.finditer(output):
        if match.group("connection").strip().upper() != "USB":
            continue
        probes.append(
            {
                "index": match.group("index").strip(),
                "connection": "USB",
                "serial": match.group("serial").strip(),
                "product": match.group("product").strip(),
                "nickname": (match.group("nickname") or "").strip(),
            }
        )
    return probes


def enumerate_jlinks(jlink_commander: Path) -> tuple[list[dict[str, str]], str]:
    """List USB probes without issuing a target Connect command."""
    with tempfile.TemporaryDirectory() as temp:
        command_file = Path(temp) / "list_probes.jlink"
        command_file.write_text("ShowEmuList USB\nExit\n", encoding="ascii", newline="\n")
        completed = subprocess.run(
            [
                str(jlink_commander),
                "-NoGui",
                "1",
                "-AutoConnect",
                "0",
                "-ExitOnError",
                "1",
                "-CommandFile",
                str(command_file),
            ],
            capture_output=True,
            text=True,
            encoding="utf-8",
            errors="replace",
            timeout=20,
            check=False,
        )
    output = completed.stdout + "\n" + completed.stderr
    probes = parse_probe_list(output)
    if completed.returncode != 0 and not probes:
        raise LiveAttachError(f"J-Link probe enumeration failed ({completed.returncode})")
    serials = [probe["serial"] for probe in probes]
    if len(serials) != len(set(serials)):
        raise LiveAttachError("J-Link enumeration returned duplicate serial numbers")
    return probes, output


def port_is_available(port: int) -> bool:
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as listener:
        try:
            listener.bind((LOCAL_HOST, port))
        except OSError:
            return False
    return True


def make_project(
    path: Path,
    *,
    device: str,
    probe_serial: str,
    target_interface: str,
    speed: str,
    elf: Path,
    jlink_log: Path,
    console_log: Path,
) -> str:
    project = f'''/*********************************************************************
* Generated by ozone_live_attach.py. Read-only CM_ATTACH project.
**********************************************************************/
void OnProjectLoad(void) {{
  Project.SetDevice("{c_string(device)}");
  Project.SetHostIF("USB", "{c_string(probe_serial)}");
  Project.SetTargetIF("{c_string(target_interface)}");
  Project.SetTIFSpeed("{c_string(speed)}");
  Project.SetJLinkLogFile("{c_string(str(jlink_log))}");
  Project.SetConsoleLogFile("{c_string(str(console_log))}");
  File.Open("{c_string(str(elf))}");
  Break.ClearAll();
  Break.ClearAllOnData();
  Debug.SetConnectMode(CM_ATTACH);
}}
'''
    for token in FORBIDDEN_PROJECT_TOKENS:
        if token in project:
            raise LiveAttachError(f"generated project contains forbidden token: {token}")
    path.write_text(project, encoding="utf-8", newline="\n")
    return project


class OzoneSocket:
    def __init__(self, port: int, timeout_s: float):
        self.port = port
        self.timeout_s = timeout_s
        self.sock: socket.socket | None = None
        self.transcript: list[str] = []

    def connect(self) -> None:
        deadline = time.monotonic() + self.timeout_s
        last_error: OSError | None = None
        while time.monotonic() < deadline:
            try:
                sock = socket.create_connection((LOCAL_HOST, self.port), timeout=1.0)
                sock.settimeout(0.5)
                self.sock = sock
                self._drain_welcome()
                return
            except OSError as exc:
                last_error = exc
                time.sleep(0.2)
        raise LiveAttachError(f"Ozone automation socket did not open: {last_error}")

    def _drain_welcome(self) -> None:
        if self.sock is None:
            return
        deadline = time.monotonic() + 2.0
        chunks: list[bytes] = []
        while time.monotonic() < deadline:
            try:
                chunk = self.sock.recv(4096)
            except socket.timeout:
                break
            if not chunk:
                break
            chunks.append(chunk)
        if chunks:
            text = b"".join(chunks).decode("utf-8", errors="replace")
            self.transcript.append(text)

    def command(self, command: str) -> str:
        if self.sock is None:
            raise LiveAttachError("Ozone socket is not connected")
        marker = f"__CODEX_DONE_{uuid.uuid4().hex}__"
        payload = f'{command.rstrip(";")};\nUtil.Log("{marker}");\n'.encode("utf-8")
        self.sock.sendall(payload)

        deadline = time.monotonic() + self.timeout_s
        chunks: list[bytes] = []
        marker_bytes = marker.encode("utf-8")
        while time.monotonic() < deadline:
            try:
                chunk = self.sock.recv(4096)
            except socket.timeout:
                continue
            if not chunk:
                raise LiveAttachError("Ozone automation socket closed unexpectedly")
            chunks.append(chunk)
            if marker_bytes in b"".join(chunks):
                text = b"".join(chunks).decode("utf-8", errors="replace")
                self.transcript.append(text)
                return text
        raise LiveAttachError(f"Ozone command timed out: {command}")

    def close(self) -> None:
        if self.sock is not None:
            try:
                self.sock.close()
            finally:
                self.sock = None

    def request_exit(self) -> None:
        """Ask Ozone to exit without waiting for a completion marker.

        File.Exit closes the process and socket before a following marker can run.
        """
        if self.sock is None:
            raise LiveAttachError("Ozone socket is not connected")
        self.sock.sendall(b"File.Exit();\n")


def parse_return_value(response: str, command: str) -> int:
    values = re.findall(r"\breturns\s+(-?0x[0-9A-Fa-f]+|-?\d+)\b", response, re.IGNORECASE)
    if not values:
        raise LiveAttachError(f"unable to parse Ozone return value for {command}")
    value = int(values[-1], 0)
    if value == -1:
        raise LiveAttachError(f"Ozone returned an error for {command}")
    return value


def read_text(path: Path) -> str:
    if not path.exists():
        return ""
    try:
        return path.read_text(encoding="utf-8", errors="replace")
    except OSError:
        return ""


def find_disturbance(text: str, extra_patterns: Iterable[str] = ()) -> list[str]:
    evidence: list[str] = []
    for pattern in (*DEFAULT_DISTURBANCE_PATTERNS, *extra_patterns):
        try:
            match = re.search(pattern, text, re.IGNORECASE | re.DOTALL)
        except re.error as exc:
            raise LiveAttachError(f"invalid disturbance regex {pattern!r}: {exc}") from exc
        if match:
            excerpt = re.sub(r"\s+", " ", match.group(0)).strip()
            evidence.append(f"/{pattern}/ matched {excerpt!r}")
    return evidence


def launch_hidden_ozone(ozone_exe: Path, project: Path, port: int, app_log: Path) -> subprocess.Popen[bytes]:
    args = [
        str(ozone_exe),
        "-project",
        str(project),
        "-port",
        str(port),
        "-logfile",
        str(app_log),
        "-loginterval",
        "0",
    ]
    startupinfo = None
    creationflags = 0
    if os.name == "nt":
        startupinfo = subprocess.STARTUPINFO()
        startupinfo.dwFlags |= subprocess.STARTF_USESHOWWINDOW
        startupinfo.wShowWindow = 0
        creationflags |= getattr(subprocess, "CREATE_NO_WINDOW", 0)
    return subprocess.Popen(
        args,
        stdin=subprocess.DEVNULL,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
        startupinfo=startupinfo,
        creationflags=creationflags,
    )


def validate_config(config: dict[str, Any], config_path: Path, require_runtime: bool) -> dict[str, Any]:
    if config.get("schema_version") != SCHEMA_VERSION:
        raise LiveAttachError(f"schema_version must be {SCHEMA_VERSION}")

    base = config_path.parent
    ozone_exe = resolve_path(require_text(config, "ozone_exe"), base)
    jlink_commander = resolve_path(require_text(config, "jlink_commander"), base)
    elf = resolve_path(require_text(config, "elf"), base)
    output_dir = resolve_path(require_text(config, "output_dir"), base)
    device = require_text(config, "device")
    probe_serial = require_text(config, "probe_serial")
    if not probe_serial.isdigit() or int(probe_serial) == 0:
        raise LiveAttachError("probe_serial must be the non-zero numeric J-Link serial number")
    target_interface = str(config.get("target_interface", "SWD")).upper()
    if target_interface not in ("SWD", "JTAG"):
        raise LiveAttachError("target_interface must be SWD or JTAG")
    speed = str(config.get("speed", "1 MHz")).strip()
    port = parse_int(config.get("automation_port", 19200), "automation_port")
    if port < 1024 or port > 65535:
        raise LiveAttachError("automation_port must be in 1024..65535")
    timeout_s = float(config.get("timeout_s", 20.0))
    if timeout_s <= 0 or timeout_s > 120:
        raise LiveAttachError("timeout_s must be in (0, 120]")

    if not elf.is_file():
        raise LiveAttachError(f"ELF not found: {elf}")
    require_elf(elf)
    if require_runtime and not ozone_exe.is_file():
        raise LiveAttachError(f"Ozone executable not found: {ozone_exe}")
    if require_runtime and not jlink_commander.is_file():
        raise LiveAttachError(f"J-Link Commander not found: {jlink_commander}")

    raw_reads = config.get("reads", [])
    uses_symbols = any(isinstance(item, dict) and "symbol" in item for item in raw_reads)
    symbol_tool: Path | None = None
    symbols: dict[str, int] = {}
    if uses_symbols:
        symbol_tool_text = require_text(config, "symbol_tool")
        symbol_tool = resolve_path(symbol_tool_text, base)
        if not symbol_tool.is_file():
            discovered = shutil.which(symbol_tool_text)
            if discovered:
                symbol_tool = Path(discovered).resolve()
            else:
                raise LiveAttachError(f"symbol_tool not found: {symbol_tool}")
        symbols = load_symbols(symbol_tool, elf)

    ranges = parse_ranges(config)
    reads = parse_reads(config, symbols, ranges)
    extra_patterns = config.get("additional_disturbance_patterns", [])
    if not isinstance(extra_patterns, list) or not all(isinstance(item, str) for item in extra_patterns):
        raise LiveAttachError("additional_disturbance_patterns must be a list of regex strings")
    for pattern in extra_patterns:
        re.compile(pattern)

    return {
        "ozone_exe": ozone_exe,
        "jlink_commander": jlink_commander,
        "elf": elf,
        "elf_sha256": sha256_file(elf),
        "output_dir": output_dir,
        "device": device,
        "probe_serial": probe_serial,
        "target_interface": target_interface,
        "speed": speed,
        "port": port,
        "timeout_s": timeout_s,
        "ranges": ranges,
        "reads": reads,
        "symbol_tool": symbol_tool,
        "extra_patterns": extra_patterns,
    }


def result_template(config_path: Path) -> dict[str, Any]:
    return {
        "schema_version": SCHEMA_VERSION,
        "mode": "ozone_cm_attach_read_only",
        "ok": False,
        "disturbed": False,
        "disturbance_evidence": [],
        "started_at": utc_now(),
        "ended_at": None,
        "config": str(config_path),
        "target_state": "unknown",
        "original_target_state_known": False,
        "reads": [],
        "artifacts": {},
        "cleanup": {"disconnect_requested": False, "exit_requested": False, "manual_cleanup_required": False},
        "errors": [],
    }


def run_capture(config_path: Path, *, dry_run: bool) -> tuple[int, dict[str, Any]]:
    result = result_template(config_path)
    ozone: subprocess.Popen[bytes] | None = None
    client: OzoneSocket | None = None
    attached = False
    output_dir: Path | None = None
    logs: list[Path] = []
    final_extra_patterns: Iterable[str] = ()

    try:
        config = json.loads(config_path.read_text(encoding="utf-8"))
        if not isinstance(config, dict):
            raise LiveAttachError("configuration root must be an object")
        plan = validate_config(config, config_path, require_runtime=not dry_run)
        final_extra_patterns = plan["extra_patterns"]
        output_dir = plan["output_dir"]
        output_dir.mkdir(parents=True, exist_ok=True)
        session_id = datetime.now().strftime("%Y%m%d_%H%M%S") + "_" + uuid.uuid4().hex[:8]
        session_dir = output_dir / session_id
        session_dir.mkdir(parents=False, exist_ok=False)
        project = session_dir / "live_attach.jdebug"
        jlink_log = session_dir / "jlink.log"
        console_log = session_dir / "console.log"
        app_log = session_dir / "ozone.log"
        logs = [jlink_log, console_log, app_log]
        make_project(
            project,
            device=plan["device"],
            probe_serial=plan["probe_serial"],
            target_interface=plan["target_interface"],
            speed=plan["speed"],
            elf=plan["elf"],
            jlink_log=jlink_log,
            console_log=console_log,
        )
        result["artifacts"] = {
            "session_dir": str(session_dir),
            "project": str(project),
            "jlink_log": str(jlink_log),
            "console_log": str(console_log),
            "ozone_log": str(app_log),
            "elf": str(plan["elf"]),
            "elf_sha256": plan["elf_sha256"],
        }
        result["plan"] = {
            "device": plan["device"],
            "probe_serial": plan["probe_serial"],
            "target_interface": plan["target_interface"],
            "speed": plan["speed"],
            "automation_host": LOCAL_HOST,
            "automation_port": plan["port"],
            "reads": [
                {
                    "name": item.name,
                    "address": f"0x{item.address:X}",
                    "width": item.width,
                    "count": item.count,
                    "source": item.source,
                }
                for item in plan["reads"]
            ],
        }

        if dry_run:
            result["ok"] = True
            result["dry_run"] = True
            return 0, result

        if os.name != "nt":
            raise LiveAttachError("live capture is Windows-only; use --dry-run for offline validation")
        conflicts = sorted(running_processes() & DEFAULT_CONFLICTING_PROCESSES)
        if conflicts:
            raise LiveAttachError(f"competing J-Link client processes detected: {', '.join(conflicts)}")
        if not port_is_available(plan["port"]):
            raise LiveAttachError(f"automation port is already in use on localhost: {plan['port']}")

        probes, probe_enumeration_log = enumerate_jlinks(plan["jlink_commander"])
        result["connected_jlinks"] = probes
        result["probe_enumeration_log"] = probe_enumeration_log
        matches = [probe for probe in probes if probe["serial"] == plan["probe_serial"]]
        if len(matches) != 1:
            available = ", ".join(probe["serial"] for probe in probes) or "none"
            raise LiveAttachError(
                f"selected J-Link S/N {plan['probe_serial']} was not found exactly once; available: {available}"
            )

        ozone = launch_hidden_ozone(plan["ozone_exe"], project, plan["port"], app_log)
        result["ozone_pid"] = ozone.pid
        client = OzoneSocket(plan["port"], plan["timeout_s"])
        client.connect()

        client.command("Break.ClearAll()")
        client.command("Break.ClearAllOnData()")
        client.command("Debug.SetConnectMode(CM_ATTACH)")
        start_response = client.command("Debug.Start()")
        attached = True

        evidence = find_disturbance(
            start_response + "\n" + "\n".join(read_text(path) for path in logs),
            plan["extra_patterns"],
        )
        if evidence:
            result["disturbed"] = True
            result["disturbance_evidence"] = evidence
            raise DisturbanceDetected("target disturbance detected during attach; RAM reads were skipped")

        state_response = client.command("Debug.IsHalted()")
        halted = parse_return_value(state_response, "Debug.IsHalted()")
        result["target_state"] = "halted_observed" if halted else "running_observed"

        read_results: list[dict[str, Any]] = []
        for item in plan["reads"]:
            values: list[int] = []
            read_action = {8: "Target.ReadU8", 16: "Target.ReadU16", 32: "Target.ReadU32"}[item.width]
            for index in range(item.count):
                address = item.address + index * item.byte_width
                command = f"{read_action}(0x{address:X})"
                response = client.command(command)
                values.append(parse_return_value(response, command))
                evidence = find_disturbance(
                    "\n".join(read_text(path) for path in logs), plan["extra_patterns"]
                )
                if evidence:
                    result["disturbed"] = True
                    result["disturbance_evidence"] = evidence
                    raise DisturbanceDetected("target disturbance detected during RAM read")
            digits = item.width // 4
            read_results.append(
                {
                    "name": item.name,
                    "address": f"0x{item.address:X}",
                    "width": item.width,
                    "count": item.count,
                    "source": item.source,
                    "values_unsigned": values,
                    "values_hex": [f"0x{value:0{digits}X}" for value in values],
                }
            )
        result["reads"] = read_results
        result["ok"] = True

    except (OSError, ValueError, json.JSONDecodeError, subprocess.SubprocessError, LiveAttachError) as exc:
        result["errors"].append(str(exc))
    finally:
        if client is not None:
            if attached:
                try:
                    client.command("Debug.Disconnect()")
                    result["cleanup"]["disconnect_requested"] = True
                    attached = False
                except Exception as exc:  # cleanup must be reported, not hidden
                    result["errors"].append(f"disconnect failed: {exc}")
                    result["cleanup"]["manual_cleanup_required"] = True
                    result["ok"] = False
            if not attached:
                try:
                    client.request_exit()
                    result["cleanup"]["exit_requested"] = True
                except Exception as exc:
                    result["errors"].append(f"Ozone exit failed: {exc}")
                    result["cleanup"]["manual_cleanup_required"] = True
                    result["ok"] = False
            result["automation_transcript"] = "".join(client.transcript)
            client.close()

        if ozone is not None:
            try:
                ozone.wait(timeout=5)
            except subprocess.TimeoutExpired:
                if attached:
                    result["cleanup"]["manual_cleanup_required"] = True
                    result["ok"] = False
                else:
                    ozone.terminate()
                    try:
                        ozone.wait(timeout=3)
                    except subprocess.TimeoutExpired:
                        result["cleanup"]["manual_cleanup_required"] = True

        if logs:
            final_evidence = find_disturbance(
                "\n".join(read_text(path) for path in logs),
                final_extra_patterns,
            )
            if final_evidence:
                result["disturbed"] = True
                result["disturbance_evidence"] = sorted(
                    set(result["disturbance_evidence"] + final_evidence)
                )
                result["ok"] = False
        result["ended_at"] = utc_now()

    return (0 if result["ok"] else 1), result


def self_test() -> tuple[int, dict[str, Any]]:
    checks: list[dict[str, Any]] = []

    def check(name: str, condition: bool) -> None:
        checks.append({"name": name, "ok": bool(condition)})
        if not condition:
            raise AssertionError(name)

    try:
        check("parse decimal return", parse_return_value("// returns 17", "x") == 17)
        check("parse hex return", parse_return_value("// returns 0xA5", "x") == 0xA5)
        check("detect reset", bool(find_disturbance("J-Link: Resetting target")))
        check("ignore harmless mode text", not find_disturbance("Debug.SetConnectMode(CM_ATTACH)"))
        probes = parse_probe_list(
            "J-Link[0]: Connection: USB, Serial number: 111111111, ProductName: J-Link PLUS, Nickname: left\n"
            "J-Link[1]: Connection: USB, Serial number: 222222222, ProductName: J-Link BASE, Nickname: <not set>\n"
        )
        check("parse two J-Link serials", [probe["serial"] for probe in probes] == ["111111111", "222222222"])
        ranges = [RamRange("sram", 0x20000000, 0x20001000)]
        reads = parse_reads(
            {"reads": [{"name": "probe", "address": "0x20000010", "width": 32, "count": 2}]},
            {},
            ranges,
        )
        check("allow RAM read", reads[0].byte_size == 8)
        symbol_reads = parse_reads(
            {"reads": [{"name": "symbol", "symbol": "g_probe", "offset": 4, "width": 32}]},
            {"g_probe": 0x20000020},
            ranges,
        )
        check("resolve offline symbol", symbol_reads[0].address == 0x20000024)
        try:
            parse_reads(
                {"reads": [{"name": "peripheral", "address": "0x40000000", "width": 32}]},
                {},
                ranges,
            )
            outside_rejected = False
        except LiveAttachError:
            outside_rejected = True
        check("reject non-whitelisted read", outside_rejected)

        with tempfile.TemporaryDirectory() as temp:
            temp_path = Path(temp)
            project_path = temp_path / "test.jdebug"
            text = make_project(
                project_path,
                device="TEST_DEVICE",
                probe_serial="123456",
                target_interface="SWD",
                speed="1 MHz",
                elf=temp_path / "firmware.elf",
                jlink_log=temp_path / "jlink.log",
                console_log=temp_path / "console.log",
            )
            check("project uses CM_ATTACH", "CM_ATTACH" in text)
            check("project has no forbidden action", not any(t in text for t in FORBIDDEN_PROJECT_TOKENS))
        return 0, {"schema_version": SCHEMA_VERSION, "self_test": True, "ok": True, "checks": checks}
    except Exception as exc:
        return 1, {
            "schema_version": SCHEMA_VERSION,
            "self_test": True,
            "ok": False,
            "checks": checks,
            "errors": [str(exc)],
        }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--config", type=Path, help="Path to live-attach JSON configuration")
    parser.add_argument("--dry-run", action="store_true", help="Validate and generate a plan without launching Ozone")
    parser.add_argument("--self-test", action="store_true", help="Run offline safety/parser tests")
    parser.add_argument("--list-probes", action="store_true", help="List USB J-Link probes without target connect")
    parser.add_argument("--jlink-commander", type=Path, help="JLink.exe path for --list-probes")
    args = parser.parse_args()

    if args.self_test:
        code, result = self_test()
    elif args.list_probes:
        result = {"schema_version": SCHEMA_VERSION, "operation": "list_probes", "ok": False, "probes": [], "errors": []}
        try:
            if os.name != "nt":
                raise LiveAttachError("J-Link enumeration is Windows-only")
            if args.jlink_commander is None:
                raise LiveAttachError("--jlink-commander is required with --list-probes")
            commander = args.jlink_commander.resolve()
            if not commander.is_file():
                raise LiveAttachError(f"J-Link Commander not found: {commander}")
            conflicts = sorted(running_processes() & DEFAULT_CONFLICTING_PROCESSES)
            if conflicts:
                raise LiveAttachError(f"competing J-Link client processes detected: {', '.join(conflicts)}")
            probes, raw_log = enumerate_jlinks(commander)
            result.update({"ok": True, "probes": probes, "raw_log": raw_log})
            code = 0
        except (OSError, subprocess.SubprocessError, LiveAttachError) as exc:
            result["errors"].append(str(exc))
            code = 1
    elif args.config:
        config_path = args.config.resolve()
        if not config_path.is_file():
            code, result = 1, result_template(config_path)
            result["errors"].append(f"configuration not found: {config_path}")
            result["ended_at"] = utc_now()
        else:
            code, result = run_capture(config_path, dry_run=args.dry_run)
    else:
        parser.error("one of --config, --self-test, or --list-probes is required")
        return 2

    json.dump(result, sys.stdout, ensure_ascii=False, indent=2)
    sys.stdout.write("\n")
    return code


if __name__ == "__main__":
    raise SystemExit(main())
