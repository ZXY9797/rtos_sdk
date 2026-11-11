#!/usr/bin/env python3
"""Persistent, policy-gated Ozone debug agent for a locally attached USB J-Link.

The agent owns one hidden Ozone CM_ATTACH session and accepts typed JSON requests on
stdin. It deliberately has no reset, flash erase, download, Target.LoadMemory, raw
console, or arbitrary expression operation. Run it in the interactive Windows session
that owns the USB probe; transport stdin/stdout through a controlled PC broker or SSH.
"""

from __future__ import annotations

import argparse
import hashlib
import hmac
import json
import os
import re
import socket
import subprocess
import sys
import tempfile
import threading
import time
import uuid
from dataclasses import dataclass
from datetime import datetime
from pathlib import Path
from typing import Any, Iterable

from ozone_live_attach import (
    DEFAULT_CONFLICTING_PROCESSES,
    LOCAL_HOST,
    LiveAttachError,
    OzoneSocket,
    c_string,
    enumerate_jlinks,
    launch_hidden_ozone,
    parse_int,
    parse_return_value,
    port_is_available,
    read_text,
    require_elf,
    require_text,
    resolve_path,
    running_processes,
    sha256_file,
    utc_now,
)


SCHEMA_VERSION = 1
VALID_MODES = ("observe", "live_modify", "stopped_debug")
CORE_REGISTERS = (
    "R0", "R1", "R2", "R3", "R4", "R5", "R6", "R7",
    "R8", "R9", "R10", "R11", "R12", "SP", "LR", "PC", "xPSR",
    "MSP", "PSP", "PRIMASK", "BASEPRI", "FAULTMASK", "CONTROL",
)
FATAL_PATTERNS = (
    r"\bsysresetreq\b",
    r"\breset(?:ting)?\s+(?:the\s+)?target\b",
    r"\btarget\s+reset(?:ted)?\b",
    r"\bdownload(?:ing)?\s+(?:program|file|debuggee|image)\b",
    r"\bflash\s+(?:download|programming|erase)\b",
    r"\bwork\s*ram\b",
)
REGISTER_RE = re.compile(r"^[A-Za-z_][A-Za-z0-9_.\[\]-]{0,127}$")
SOURCE_LOCATION_RE = re.compile(r"^[A-Za-z0-9_.$/\\:+ -]{1,260}$")
ARTIFACT_NAME_RE = re.compile(r"^[A-Za-z0-9_.-]{1,80}$")


class AgentError(RuntimeError):
    pass


@dataclass(frozen=True)
class MemoryRange:
    name: str
    start: int
    end: int

    def contains(self, address: int, size: int) -> bool:
        return size > 0 and self.start <= address and address + size <= self.end


def parse_ranges(config: dict[str, Any], key: str, *, required: bool) -> list[MemoryRange]:
    raw = config.get(key, [])
    if not isinstance(raw, list) or (required and not raw):
        suffix = "a non-empty list" if required else "a list"
        raise AgentError(f"{key} must be {suffix}")
    ranges: list[MemoryRange] = []
    for index, item in enumerate(raw):
        if not isinstance(item, dict):
            raise AgentError(f"{key}[{index}] must be an object")
        start = parse_int(item.get("start"), f"{key}[{index}].start")
        end = parse_int(item.get("end"), f"{key}[{index}].end")
        if start < 0 or end <= start:
            raise AgentError(f"{key}[{index}] has invalid bounds")
        ranges.append(MemoryRange(str(item.get("name", f"{key}_{index}")), start, end))
    return ranges


def in_ranges(ranges: Iterable[MemoryRange], address: int, size: int, label: str) -> None:
    if not any(region.contains(address, size) for region in ranges):
        raise AgentError(f"{label} 0x{address:X}+{size} is outside its allowlist")


def parse_modes(config: dict[str, Any]) -> tuple[str, ...]:
    raw = config.get("enabled_modes", ["observe"])
    if not isinstance(raw, list) or not raw or not all(isinstance(x, str) for x in raw):
        raise AgentError("enabled_modes must be a non-empty list of strings")
    modes = tuple(dict.fromkeys(raw))
    unknown = sorted(set(modes) - set(VALID_MODES))
    if unknown:
        raise AgentError(f"unknown enabled_modes: {', '.join(unknown)}")
    if "observe" not in modes:
        raise AgentError("enabled_modes must include observe")
    return modes


def validate_config(config: dict[str, Any], config_path: Path, *, runtime: bool) -> dict[str, Any]:
    if config.get("schema_version") != SCHEMA_VERSION:
        raise AgentError(f"schema_version must be {SCHEMA_VERSION}")
    base = config_path.parent
    ozone_exe = resolve_path(require_text(config, "ozone_exe"), base)
    jlink_commander = resolve_path(require_text(config, "jlink_commander"), base)
    elf = resolve_path(require_text(config, "elf"), base)
    output_dir = resolve_path(require_text(config, "output_dir"), base)
    device = require_text(config, "device")
    probe_serial = require_text(config, "probe_serial")
    if not probe_serial.isdigit() or int(probe_serial) == 0:
        raise AgentError("probe_serial must be a non-zero numeric J-Link serial")
    target_interface = str(config.get("target_interface", "SWD")).upper()
    if target_interface not in ("SWD", "JTAG"):
        raise AgentError("target_interface must be SWD or JTAG")
    speed = str(config.get("speed", "1 MHz")).strip()
    port = parse_int(config.get("automation_port", 19200), "automation_port")
    if not 1024 <= port <= 65535:
        raise AgentError("automation_port must be in 1024..65535")
    timeout_s = float(config.get("timeout_s", 20))
    if not 0 < timeout_s <= 120:
        raise AgentError("timeout_s must be in (0, 120]")
    max_block_bytes = parse_int(config.get("max_block_bytes", 1024 * 1024), "max_block_bytes")
    if not 1 <= max_block_bytes <= 16 * 1024 * 1024:
        raise AgentError("max_block_bytes must be in 1..16777216")
    if not elf.is_file():
        raise AgentError(f"ELF not found: {elf}")
    require_elf(elf)
    if runtime and not ozone_exe.is_file():
        raise AgentError(f"Ozone executable not found: {ozone_exe}")
    if runtime and not jlink_commander.is_file():
        raise AgentError(f"J-Link Commander not found: {jlink_commander}")

    modes = parse_modes(config)
    read_ranges = parse_ranges(config, "allowed_read_ranges", required=True)
    write_ranges = parse_ranges(config, "allowed_write_ranges", required="live_modify" in modes)
    code_ranges = parse_ranges(config, "allowed_code_ranges", required="stopped_debug" in modes)
    allowed_registers = config.get("allowed_registers", list(CORE_REGISTERS))
    if not isinstance(allowed_registers, list) or not all(
        isinstance(item, str) and REGISTER_RE.fullmatch(item) for item in allowed_registers
    ):
        raise AgentError("allowed_registers contains an invalid register name")
    allowed_source_files = config.get("allowed_source_files", [])
    allowed_source_functions = config.get("allowed_source_functions", [])
    if not isinstance(allowed_source_files, list) or not all(
        isinstance(item, str) and SOURCE_LOCATION_RE.fullmatch(item) for item in allowed_source_files
    ):
        raise AgentError("allowed_source_files contains an invalid path")
    if not isinstance(allowed_source_functions, list) or not all(
        isinstance(item, str) and re.fullmatch(r"[A-Za-z_.$][A-Za-z0-9_.$]*", item)
        for item in allowed_source_functions
    ):
        raise AgentError("allowed_source_functions contains an invalid function")
    fault_registers = config.get("fault_registers", [])
    if not isinstance(fault_registers, list):
        raise AgentError("fault_registers must be a list")
    normalized_faults: list[dict[str, Any]] = []
    for index, item in enumerate(fault_registers):
        if not isinstance(item, dict) or not isinstance(item.get("name"), str):
            raise AgentError(f"fault_registers[{index}] must contain name and address")
        address = parse_int(item.get("address"), f"fault_registers[{index}].address")
        width = parse_int(item.get("width", 32), f"fault_registers[{index}].width")
        if width not in (8, 16, 32) or address % (width // 8):
            raise AgentError(f"fault_registers[{index}] has invalid width/alignment")
        normalized_faults.append({"name": item["name"], "address": address, "width": width})

    objdump_tool: Path | None = None
    if config.get("objdump_tool"):
        objdump_tool = resolve_path(str(config["objdump_tool"]), base)
        if runtime and not objdump_tool.is_file():
            raise AgentError(f"objdump_tool not found: {objdump_tool}")
    control_token = config.get("control_token")
    if control_token is not None and (not isinstance(control_token, str) or len(control_token) < 16):
        raise AgentError("control_token must be at least 16 characters when configured")

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
        "max_block_bytes": max_block_bytes,
        "modes": modes,
        "read_ranges": read_ranges,
        "write_ranges": write_ranges,
        "code_ranges": code_ranges,
        "allowed_registers": set(allowed_registers),
        "allowed_source_files": tuple(item.replace("\\", "/").lower() for item in allowed_source_files),
        "allowed_source_functions": set(allowed_source_functions),
        "allow_register_write": bool(config.get("allow_register_write", False)),
        "allow_software_breakpoints": bool(config.get("allow_software_breakpoints", False)),
        "fault_registers": normalized_faults,
        "objdump_tool": objdump_tool,
        "control_token": control_token,
    }


def make_project(path: Path, plan: dict[str, Any], jlink_log: Path, console_log: Path) -> str:
    text = f'''/*********************************************************************
* Generated by ozone_debug_agent.py. CM_ATTACH; no reset/download.
**********************************************************************/
void OnProjectLoad(void) {{
  Project.SetDevice("{c_string(plan['device'])}");
  Project.SetHostIF("USB", "{c_string(plan['probe_serial'])}");
  Project.SetTargetIF("{c_string(plan['target_interface'])}");
  Project.SetTIFSpeed("{c_string(plan['speed'])}");
  Project.SetJLinkLogFile("{c_string(str(jlink_log))}");
  Project.SetConsoleLogFile("{c_string(str(console_log))}");
  File.Open("{c_string(str(plan['elf']))}");
  Break.ClearAll();
  Break.ClearAllOnData();
  Debug.SetConnectMode(CM_ATTACH);
}}
'''
    forbidden = ("Debug.Reset", "Debug.Download", "Target.LoadMemory", "Target.EraseChip", "File.Load")
    if any(token in text for token in forbidden):
        raise AgentError("generated project contains a forbidden operation")
    path.write_text(text, encoding="utf-8", newline="\n")
    return text


def fatal_evidence(text: str) -> list[str]:
    evidence: list[str] = []
    for pattern in FATAL_PATTERNS:
        match = re.search(pattern, text, re.IGNORECASE | re.DOTALL)
        if match:
            evidence.append(f"/{pattern}/ matched {match.group(0)!r}")
    return evidence


def request_ack(serial: str, operation: str) -> str:
    return f"DISTURB:{serial}:{operation}"


class DebugSession:
    def __init__(self, plan: dict[str, Any], session_dir: Path, client: OzoneSocket):
        self.plan = plan
        self.session_dir = session_dir
        self.client = client
        self.closed = False
        self.audit_path = session_dir / "audit.jsonl"
        self.jlink_log = session_dir / "jlink.log"
        self.console_log = session_dir / "console.log"
        self.ozone_log = session_dir / "ozone.log"
        self.log_offsets: dict[Path, int] = {}

    def _command_value(self, command: str) -> int:
        return parse_return_value(self.client.command(command), command)

    def _state(self) -> str:
        return "halted" if self._command_value("Debug.IsHalted()") else "running"

    def _mode(self, required: str) -> None:
        if required not in self.plan["modes"]:
            raise AgentError(f"operation requires disabled mode: {required}")

    def _ack(self, request: dict[str, Any], operation: str) -> None:
        expected = request_ack(self.plan["probe_serial"], operation)
        if request.get("acknowledge") != expected:
            raise AgentError(f"explicit acknowledgement required: {expected}")
        reason = request.get("reason")
        if not isinstance(reason, str) or not reason.strip():
            raise AgentError("a non-empty reason is required for a disturbing operation")

    def _halted(self) -> None:
        if self._state() != "halted":
            raise AgentError("operation requires an already halted target")

    def _address_size(self, request: dict[str, Any]) -> tuple[int, int]:
        address = parse_int(request.get("address"), "address")
        size = parse_int(request.get("size"), "size")
        if address < 0 or not 1 <= size <= self.plan["max_block_bytes"]:
            raise AgentError("invalid address or block size")
        return address, size

    def _artifact(self, prefix: str, suffix: str) -> Path:
        return self.session_dir / f"{prefix}_{uuid.uuid4().hex[:10]}{suffix}"

    def _new_log_text(self) -> str:
        chunks: list[str] = []
        for path in (self.jlink_log, self.console_log, self.ozone_log):
            try:
                with path.open("rb") as stream:
                    stream.seek(self.log_offsets.get(path, 0))
                    data = stream.read()
                    self.log_offsets[path] = stream.tell()
                if data:
                    chunks.append(data.decode("utf-8", errors="replace"))
            except OSError:
                continue
        return "\n".join(chunks)

    def _save_memory(self, address: int, size: int, *, code: bool = False) -> dict[str, Any]:
        ranges = self.plan["code_ranges"] if code else self.plan["read_ranges"]
        in_ranges(ranges, address, size, "code read" if code else "memory read")
        path = self._artifact("code" if code else "memory", ".bin")
        command = f'Target.SaveMemory("{c_string(str(path))}", 0x{address:X}, {size})'
        self._command_value(command)
        if not path.is_file() or path.stat().st_size != size:
            raise AgentError("Ozone did not create the expected memory artifact")
        data = path.read_bytes()
        result: dict[str, Any] = {
            "address": f"0x{address:X}",
            "size": size,
            "sha256": hashlib.sha256(data).hexdigest(),
            "artifact": str(path),
        }
        if size <= 4096:
            result["data_hex"] = data.hex()
        return result

    def _read_scalar(self, address: int, width: int) -> int:
        command = f"Target.ReadU{width}(0x{address:X})"
        return self._command_value(command)

    def _registers(self, names: list[str]) -> dict[str, str]:
        self._halted()
        values: dict[str, str] = {}
        for name in names:
            if name not in self.plan["allowed_registers"]:
                raise AgentError(f"register is not allowlisted: {name}")
            value = self._command_value(f'Target.GetReg("{c_string(name)}")')
            values[name] = f"0x{value:X}"
        return values

    def _objdump(self, start: int, stop: int) -> dict[str, Any]:
        tool = self.plan["objdump_tool"]
        if tool is None:
            return {"available": False, "reason": "objdump_tool is not configured"}
        completed = subprocess.run(
            [str(tool), "-d", "-S", f"--start-address=0x{start:X}", f"--stop-address=0x{stop:X}", str(self.plan["elf"])],
            capture_output=True,
            text=True,
            encoding="utf-8",
            errors="replace",
            timeout=30,
            check=False,
        )
        if completed.returncode != 0:
            raise AgentError(f"objdump failed ({completed.returncode}): {completed.stderr.strip()}")
        return {"available": True, "start": f"0x{start:X}", "stop": f"0x{stop:X}", "text": completed.stdout}

    def capabilities(self) -> dict[str, Any]:
        operations = ["session.status", "session.capabilities", "memory.read", "session.close"]
        if self.plan["code_ranges"]:
            operations.extend(("disassemble.elf", "disassemble.target"))
        if "live_modify" in self.plan["modes"]:
            operations.append("memory.write_checked")
        if "stopped_debug" in self.plan["modes"]:
            operations.extend((
                "breakpoint.add_instruction", "breakpoint.add_data",
                "breakpoint.clear_instruction", "breakpoint.clear_all", "execution.halt",
                "execution.continue", "execution.step_into", "execution.step_over",
                "execution.step_out", "registers.read", "context.capture", "fault.capture",
                "snapshot.save",
            ))
            if self.plan["allowed_source_files"] or self.plan["allowed_source_functions"]:
                operations.extend(("breakpoint.add_source", "breakpoint.clear_source"))
            if self.plan["allow_register_write"]:
                operations.append("registers.write_checked")
        return {
            "enabled_modes": list(self.plan["modes"]),
            "operations": operations,
            "forbidden": ["raw console", "reset", "download", "flash erase", "Target.LoadMemory"],
            "ack_format": f"DISTURB:{self.plan['probe_serial']}:<operation>",
        }

    def execute(self, request: dict[str, Any]) -> dict[str, Any]:
        if self.closed:
            raise AgentError("session is closed")
        operation = request.get("operation")
        if not isinstance(operation, str):
            raise AgentError("request.operation must be a string")

        if operation == "session.status":
            return {"target_state": self._state(), "probe_serial": self.plan["probe_serial"], "elf_sha256": self.plan["elf_sha256"]}
        if operation == "session.capabilities":
            return self.capabilities()
        if operation == "memory.read":
            address, size = self._address_size(request)
            return self._save_memory(address, size)
        if operation == "disassemble.elf":
            start = parse_int(request.get("start"), "start")
            stop = parse_int(request.get("stop"), "stop")
            if stop <= start:
                raise AgentError("stop must be greater than start")
            in_ranges(self.plan["code_ranges"], start, stop - start, "ELF disassembly")
            return self._objdump(start, stop)
        if operation == "disassemble.target":
            address, size = self._address_size(request)
            target = self._save_memory(address, size, code=True)
            return {"target": target, "elf": self._objdump(address, address + size)}
        if operation == "memory.write_checked":
            self._mode("live_modify")
            self._ack(request, operation)
            address = parse_int(request.get("address"), "address")
            width = parse_int(request.get("width"), "width")
            value = parse_int(request.get("value"), "value")
            expected = parse_int(request.get("expected_old"), "expected_old")
            if width not in (8, 16, 32) or address % (width // 8):
                raise AgentError("invalid write width/alignment")
            limit = (1 << width) - 1
            if not 0 <= value <= limit or not 0 <= expected <= limit:
                raise AgentError("write values do not fit width")
            in_ranges(self.plan["write_ranges"], address, width // 8, "memory write")
            old = self._read_scalar(address, width)
            if old != expected:
                raise AgentError(f"compare failed: expected 0x{expected:X}, observed 0x{old:X}")
            self._command_value(f"Target.WriteU{width}(0x{address:X}, 0x{value:X})")
            verify = self._read_scalar(address, width)
            if verify != value:
                raise AgentError(f"write verify failed: observed 0x{verify:X}")
            return {"address": f"0x{address:X}", "width": width, "old": f"0x{old:X}", "new": f"0x{verify:X}"}
        if operation == "registers.read":
            self._mode("stopped_debug")
            names = request.get("names", list(CORE_REGISTERS))
            if not isinstance(names, list) or not all(isinstance(x, str) for x in names):
                raise AgentError("names must be a list of register names")
            return {"registers": self._registers(names)}
        if operation == "registers.write_checked":
            self._mode("stopped_debug")
            self._halted()
            self._ack(request, operation)
            if not self.plan["allow_register_write"]:
                raise AgentError("register writes are disabled by configuration")
            name = request.get("name")
            if not isinstance(name, str) or name not in self.plan["allowed_registers"]:
                raise AgentError("register is not allowlisted")
            expected = parse_int(request.get("expected_old"), "expected_old")
            value = parse_int(request.get("value"), "value")
            old = self._command_value(f'Target.GetReg("{c_string(name)}")')
            if old != expected:
                raise AgentError(f"compare failed: expected 0x{expected:X}, observed 0x{old:X}")
            self._command_value(f'Target.SetReg("{c_string(name)}", 0x{value:X})')
            verify = self._command_value(f'Target.GetReg("{c_string(name)}")')
            if verify != value:
                raise AgentError(f"register verify failed: observed 0x{verify:X}")
            return {"name": name, "old": f"0x{old:X}", "new": f"0x{verify:X}"}
        if operation.startswith("execution."):
            self._mode("stopped_debug")
            self._ack(request, operation)
            action = {
                "execution.halt": "Debug.Halt()",
                "execution.continue": "Debug.Continue()",
                "execution.step_into": "Debug.StepInto()",
                "execution.step_over": "Debug.StepOver()",
                "execution.step_out": "Debug.StepOut()",
            }.get(operation)
            if action is None:
                raise AgentError(f"unsupported operation: {operation}")
            before = self._state()
            if operation != "execution.halt" and before != "halted":
                raise AgentError("continue/step requires an already halted target")
            self._command_value(action)
            return {"before": before, "after": self._state()}
        if operation.startswith("breakpoint."):
            self._mode("stopped_debug")
            self._ack(request, operation)
            if operation in ("breakpoint.add_instruction", "breakpoint.clear_instruction"):
                address = parse_int(request.get("address"), "address")
                in_ranges(self.plan["code_ranges"], address, 2, "instruction breakpoint")
                if operation.endswith("add_instruction"):
                    implementation = request.get("implementation", "hardware")
                    if implementation == "hardware":
                        command = f"Break.SetEx(0x{address:X}, BP_TYPE_HARD)"
                    elif implementation == "software":
                        if not self.plan["allow_software_breakpoints"]:
                            raise AgentError("software/flash breakpoints are disabled")
                        command = f"Break.SetEx(0x{address:X}, BP_TYPE_SOFT)"
                    elif implementation == "auto":
                        command = f"Break.Set(0x{address:X})"
                    else:
                        raise AgentError("implementation must be hardware, software, or auto")
                else:
                    command = f"Break.Clear(0x{address:X})"
                self._command_value(command)
                return {"command": command, "address": f"0x{address:X}"}
            if operation in ("breakpoint.add_source", "breakpoint.clear_source"):
                location = request.get("location")
                if not isinstance(location, str) or not SOURCE_LOCATION_RE.fullmatch(location):
                    raise AgentError("invalid source location")
                if ":" in location:
                    source_file = location.rsplit(":", 1)[0].replace("\\", "/").lower()
                    if not any(source_file.endswith(item) for item in self.plan["allowed_source_files"]):
                        raise AgentError("source file is not allowlisted")
                elif location not in self.plan["allowed_source_functions"]:
                    raise AgentError("source function is not allowlisted")
                if operation.endswith("add_source"):
                    implementation = request.get("implementation", "hardware")
                    if implementation == "hardware":
                        command = f'Break.SetOnSrcEx("{c_string(location)}", BP_TYPE_HARD)'
                    elif implementation == "software":
                        if not self.plan["allow_software_breakpoints"]:
                            raise AgentError("software/flash breakpoints are disabled")
                        command = f'Break.SetOnSrcEx("{c_string(location)}", BP_TYPE_SOFT)'
                    elif implementation == "auto":
                        command = f'Break.SetOnSrc("{c_string(location)}")'
                    else:
                        raise AgentError("implementation must be hardware, software, or auto")
                else:
                    command = f'Break.ClearOnSrc("{c_string(location)}")'
                self._command_value(command)
                return {"command": command, "location": location}
            if operation == "breakpoint.add_data":
                address = parse_int(request.get("address"), "address")
                size = parse_int(request.get("size", 4), "size")
                if size not in (1, 2, 4) or address % size:
                    raise AgentError("data breakpoint size must be aligned 1, 2, or 4")
                in_ranges(self.plan["read_ranges"], address, size, "data breakpoint")
                access = {"read": "AT_READ_ONLY", "write": "AT_WRITE_ONLY", "read_write": "AT_READ_WRITE"}.get(request.get("access", "write"))
                if access is None:
                    raise AgentError("access must be read, write, or read_write")
                width = {1: "AW_BYTE", 2: "AW_HALF_WORD", 4: "AW_WORD"}[size]
                address_mask = parse_int(request.get("address_mask", 0), "address_mask")
                match_value = parse_int(request.get("match_value", 0), "match_value")
                value_mask = parse_int(request.get("value_mask", 0xFFFFFFFF), "value_mask")
                if address_mask != 0:
                    raise AgentError("address_mask must be zero; masked watch ranges are not supported")
                if not 0 <= match_value <= 0xFFFFFFFF or not 0 <= value_mask <= 0xFFFFFFFF:
                    raise AgentError("match_value and value_mask must fit 32 bits")
                command = f"Break.SetOnData(0x{address:X}, 0x{address_mask:X}, {access}, {width}, 0x{match_value:X}, 0x{value_mask:X})"
                self._command_value(command)
                return {"command": command, "address": f"0x{address:X}", "size": size}
            if operation == "breakpoint.clear_all":
                self._command_value("Break.ClearAll()")
                self._command_value("Break.ClearAllOnData()")
                return {"cleared": "all code and data breakpoints"}
            raise AgentError(f"unsupported operation: {operation}")
        if operation in ("context.capture", "fault.capture"):
            self._mode("stopped_debug")
            self._halted()
            registers = self._registers(list(CORE_REGISTERS))
            result: dict[str, Any] = {"registers": registers}
            if operation == "context.capture":
                pc = int(registers["PC"], 0)
                sp = int(registers["SP"], 0)
                code_before = parse_int(request.get("code_before", 32), "code_before")
                code_after = parse_int(request.get("code_after", 64), "code_after")
                stack_bytes = parse_int(request.get("stack_bytes", 256), "stack_bytes")
                code_start = max(0, pc - code_before)
                result["code"] = self._save_memory(code_start, code_before + code_after, code=True)
                result["disassembly"] = self._objdump(code_start, code_start + code_before + code_after)
                result["stack"] = self._save_memory(sp, stack_bytes)
            else:
                values: dict[str, str] = {}
                for item in self.plan["fault_registers"]:
                    values[item["name"]] = f"0x{self._read_scalar(item['address'], item['width']):X}"
                result["fault_registers_raw"] = values
                result["decoded"] = False
            return result
        if operation == "snapshot.save":
            self._mode("stopped_debug")
            self._halted()
            name = request.get("name", f"snapshot_{datetime.now().strftime('%H%M%S')}.jsnap")
            if not isinstance(name, str) or not ARTIFACT_NAME_RE.fullmatch(name):
                raise AgentError("invalid snapshot name")
            if not name.lower().endswith(".jsnap"):
                name += ".jsnap"
            path = self.session_dir / name
            self._command_value(f'Debug.SaveSnapshot("{c_string(str(path))}")')
            return {"artifact": str(path), "exists": path.exists()}
        if operation == "session.close":
            state = self._state()
            self.closed = True
            return {"target_state_before_disconnect": state, "automatic_resume": False}
        raise AgentError(f"unsupported operation: {operation}")

    def handle(self, request: dict[str, Any]) -> dict[str, Any]:
        request_id = request.get("id", uuid.uuid4().hex)
        started = time.perf_counter()
        response: dict[str, Any] = {"schema_version": SCHEMA_VERSION, "id": request_id, "ok": False, "operation": request.get("operation")}
        try:
            response["result"] = self.execute(request)
            response["ok"] = True
        except (OSError, ValueError, subprocess.SubprocessError, LiveAttachError, AgentError) as exc:
            response["error"] = str(exc)
        new_logs = self._new_log_text()
        evidence = fatal_evidence(new_logs)
        software_breakpoint = (
            request.get("operation") in ("breakpoint.add_instruction", "breakpoint.add_source")
            and request.get("implementation") == "software"
        )
        if evidence:
            response["disturbance_evidence"] = evidence
            unexpected = evidence
            if software_breakpoint:
                unexpected = [item for item in evidence if not item.startswith(r"/\bflash")]
            if unexpected:
                response["ok"] = False
                response["error"] = "unexpected reset/download/flash/work-RAM evidence detected"
        response["duration_ms"] = round((time.perf_counter() - started) * 1000, 3)
        response["timestamp"] = utc_now()
        audit_record = {
            key: response[key] for key in
            ("schema_version", "id", "ok", "operation", "error", "duration_ms", "timestamp", "disturbance_evidence")
            if key in response
        }
        result = response.get("result")
        if isinstance(result, dict):
            audit_record["result_fields"] = sorted(result)
            for key in ("address", "size", "width", "old", "new", "artifact", "target_state", "before", "after"):
                if key in result:
                    audit_record.setdefault("result_summary", {})[key] = result[key]
        audit_record["request"] = {
            key: value for key, value in request.items()
            if key in ("id", "operation", "reason", "address", "size", "width", "location", "implementation", "access")
        }
        with self.audit_path.open("a", encoding="utf-8") as stream:
            stream.write(json.dumps(audit_record, ensure_ascii=False) + "\n")
        return response


def start_session(plan: dict[str, Any]) -> tuple[DebugSession, subprocess.Popen[bytes], dict[str, Any]]:
    if os.name != "nt":
        raise AgentError("Ozone runtime is Windows-only; use --dry-run or --self-test elsewhere")
    conflicts = sorted(running_processes() & DEFAULT_CONFLICTING_PROCESSES)
    if conflicts:
        raise AgentError(f"competing J-Link client processes detected: {', '.join(conflicts)}")
    if not port_is_available(plan["port"]):
        raise AgentError(f"automation port is already in use: {LOCAL_HOST}:{plan['port']}")
    probes, probe_log = enumerate_jlinks(plan["jlink_commander"])
    matches = [probe for probe in probes if probe["serial"] == plan["probe_serial"]]
    if len(matches) != 1:
        available = ", ".join(probe["serial"] for probe in probes) or "none"
        raise AgentError(f"selected J-Link {plan['probe_serial']} not found exactly once; available: {available}")

    plan["output_dir"].mkdir(parents=True, exist_ok=True)
    session_id = datetime.now().strftime("%Y%m%d_%H%M%S") + "_" + uuid.uuid4().hex[:8]
    session_dir = plan["output_dir"] / session_id
    session_dir.mkdir()
    project = session_dir / "debug_agent.jdebug"
    jlink_log = session_dir / "jlink.log"
    console_log = session_dir / "console.log"
    ozone_log = session_dir / "ozone.log"
    make_project(project, plan, jlink_log, console_log)
    ozone = launch_hidden_ozone(plan["ozone_exe"], project, plan["port"], ozone_log)
    client = OzoneSocket(plan["port"], plan["timeout_s"])
    try:
        client.connect()
        client.command("Debug.SetConnectMode(CM_ATTACH)")
        attach_response = client.command("Debug.Start()")
        evidence = fatal_evidence(attach_response + read_text(jlink_log) + read_text(console_log) + read_text(ozone_log))
        if evidence:
            raise AgentError(f"forbidden target disturbance during attach: {'; '.join(evidence)}")
        session = DebugSession(plan, session_dir, client)
        ready = {
            "schema_version": SCHEMA_VERSION,
            "event": "session.ready",
            "ok": True,
            "probe": matches[0],
            "target_state": session._state(),
            "modes": list(plan["modes"]),
            "elf": str(plan["elf"]),
            "elf_sha256": plan["elf_sha256"],
            "session_dir": str(session_dir),
            "ozone_pid": ozone.pid,
            "probe_enumeration_log": probe_log,
        }
        return session, ozone, ready
    except Exception:
        client.close()
        ozone.terminate()
        raise


def stop_session(session: DebugSession | None, ozone: subprocess.Popen[bytes] | None) -> dict[str, Any]:
    result = {"disconnect_requested": False, "exit_requested": False, "manual_cleanup_required": False}
    if session is not None:
        try:
            session.client.command("Debug.Disconnect()")
            result["disconnect_requested"] = True
        except Exception as exc:
            result["disconnect_error"] = str(exc)
            result["manual_cleanup_required"] = True
        try:
            session.client.request_exit()
            result["exit_requested"] = True
        except Exception as exc:
            result["exit_error"] = str(exc)
            result["manual_cleanup_required"] = True
        session.client.close()
    if ozone is not None:
        try:
            ozone.wait(timeout=5)
        except subprocess.TimeoutExpired:
            ozone.terminate()
            try:
                ozone.wait(timeout=3)
            except subprocess.TimeoutExpired:
                result["manual_cleanup_required"] = True
    return result


def serve_tcp(session: DebugSession, port: int, token: str, *, announce: bool = True) -> None:
    if not 1024 <= port <= 65535 or port == session.plan["port"]:
        raise AgentError("control port must be in 1024..65535 and differ from the Ozone port")
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as listener:
        listener.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        listener.bind((LOCAL_HOST, port))
        listener.listen(4)
        listener.settimeout(1.0)
        if announce:
            emit({"schema_version": SCHEMA_VERSION, "event": "control.ready", "ok": True, "host": LOCAL_HOST, "port": port})
        while not session.closed:
            try:
                connection, _ = listener.accept()
            except socket.timeout:
                continue
            with connection:
                connection.settimeout(10)
                chunks: list[bytes] = []
                total = 0
                while True:
                    chunk = connection.recv(65536)
                    if not chunk:
                        break
                    chunks.append(chunk)
                    total += len(chunk)
                    if total > 1024 * 1024:
                        break
                    if b"\n" in chunk:
                        break
                try:
                    if total > 1024 * 1024:
                        raise AgentError("control request exceeds 1 MiB")
                    line = b"".join(chunks).split(b"\n", 1)[0]
                    request = json.loads(line.decode("utf-8"))
                    if not isinstance(request, dict):
                        raise AgentError("request must be a JSON object")
                    supplied = request.pop("_control_token", None)
                    if not isinstance(supplied, str) or not hmac.compare_digest(supplied, token):
                        raise AgentError("invalid control token")
                    response = session.handle(request)
                except (UnicodeError, json.JSONDecodeError, AgentError) as exc:
                    response = {"schema_version": SCHEMA_VERSION, "ok": False, "error": str(exc), "timestamp": utc_now()}
                connection.sendall((json.dumps(response, ensure_ascii=False) + "\n").encode("utf-8"))


def send_tcp_request(port: int, token: str, request: dict[str, Any]) -> dict[str, Any]:
    if not 1024 <= port <= 65535:
        raise AgentError("control port must be in 1024..65535")
    outbound = dict(request)
    outbound["_control_token"] = token
    with socket.create_connection((LOCAL_HOST, port), timeout=10) as connection:
        connection.settimeout(120)
        connection.sendall((json.dumps(outbound, ensure_ascii=False) + "\n").encode("utf-8"))
        chunks: list[bytes] = []
        total = 0
        while True:
            chunk = connection.recv(65536)
            if not chunk:
                break
            chunks.append(chunk)
            total += len(chunk)
            if total > 32 * 1024 * 1024:
                raise AgentError("control response exceeds 32 MiB")
            if b"\n" in chunk:
                break
    if not chunks:
        raise AgentError("control agent closed without a response")
    response = json.loads(b"".join(chunks).split(b"\n", 1)[0].decode("utf-8"))
    if not isinstance(response, dict):
        raise AgentError("control response is not a JSON object")
    return response


def plan_json(plan: dict[str, Any]) -> dict[str, Any]:
    def ranges(items: list[MemoryRange]) -> list[dict[str, str]]:
        return [{"name": item.name, "start": f"0x{item.start:X}", "end": f"0x{item.end:X}"} for item in items]
    return {
        "schema_version": SCHEMA_VERSION,
        "dry_run": True,
        "ok": True,
        "probe_serial": plan["probe_serial"],
        "device": plan["device"],
        "elf": str(plan["elf"]),
        "elf_sha256": plan["elf_sha256"],
        "automation": f"{LOCAL_HOST}:{plan['port']}",
        "enabled_modes": list(plan["modes"]),
        "allowed_read_ranges": ranges(plan["read_ranges"]),
        "allowed_write_ranges": ranges(plan["write_ranges"]),
        "allowed_code_ranges": ranges(plan["code_ranges"]),
        "allow_register_write": plan["allow_register_write"],
        "allow_software_breakpoints": plan["allow_software_breakpoints"],
        "control_auth_configured": bool(plan["control_token"]),
        "allowed_source_files": list(plan["allowed_source_files"]),
        "allowed_source_functions": sorted(plan["allowed_source_functions"]),
        "ack_format": f"DISTURB:{plan['probe_serial']}:<operation>",
    }


def self_test() -> tuple[int, dict[str, Any]]:
    checks: list[dict[str, Any]] = []
    def check(name: str, value: bool) -> None:
        checks.append({"name": name, "ok": bool(value)})
        if not value:
            raise AssertionError(name)
    try:
        ranges = [MemoryRange("ram", 0x20000000, 0x20001000)]
        in_ranges(ranges, 0x20000010, 16, "test")
        check("allow contained range", True)
        try:
            in_ranges(ranges, 0x40000000, 4, "test")
            rejected = False
        except AgentError:
            rejected = True
        check("reject outside range", rejected)
        check("bind acknowledgement to operation", request_ack("123", "execution.halt") == "DISTURB:123:execution.halt")
        check("reject source command injection", SOURCE_LOCATION_RE.fullmatch('main.c:10"); Debug.Reset();') is None)
        check("detect reset log", bool(fatal_evidence("J-Link: Resetting target")))
        check("ignore requested halt log", not fatal_evidence("Debug.Halt() // returns 0"))
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            elf = root / "firmware.elf"
            elf.write_bytes(b"\x7fELF" + b"\0" * 32)
            config_path = root / "config.json"
            config = {
                "schema_version": 1,
                "ozone_exe": "Ozone.exe",
                "jlink_commander": "JLink.exe",
                "device": "TEST",
                "probe_serial": "123456",
                "elf": str(elf),
                "output_dir": str(root / "out"),
                "enabled_modes": ["observe", "live_modify", "stopped_debug"],
                "allowed_read_ranges": [{"start": "0x20000000", "end": "0x20001000"}],
                "allowed_write_ranges": [{"start": "0x20000000", "end": "0x20000100"}],
                "allowed_code_ranges": [{"start": "0x08000000", "end": "0x08010000"}],
                "allowed_source_files": ["bms_control.c"],
                "control_token": "self-test-control-token",
            }
            config_path.write_text(json.dumps(config), encoding="utf-8")
            plan = validate_config(config, config_path, runtime=False)
            project = root / "test.jdebug"
            text = make_project(project, plan, root / "jlink.log", root / "console.log")
            check("validate all modes", plan["modes"] == ("observe", "live_modify", "stopped_debug"))
            dry_plan = plan_json(plan)
            check("dry-run reports auth without leaking token", dry_plan["control_auth_configured"] and "self-test-control-token" not in json.dumps(dry_plan))
            check("project attaches without reset/download", "CM_ATTACH" in text and "Debug.Reset" not in text and "Debug.Download" not in text)

            class FakeClient:
                def __init__(self) -> None:
                    self.commands: list[str] = []
                    self.memory = {0x20000010: 2}

                def command(self, command: str) -> str:
                    self.commands.append(command)
                    if command == "Debug.IsHalted()":
                        return "// returns 1"
                    save = re.fullmatch(r'Target\.SaveMemory\("(.+)", 0x([0-9A-F]+), (\d+)\)', command)
                    if save:
                        Path(save.group(1)).write_bytes(bytes(range(int(save.group(3)))))
                        return "// returns 0"
                    read = re.fullmatch(r"Target\.ReadU(8|16|32)\(0x([0-9A-F]+)\)", command)
                    if read:
                        return f"// returns 0x{self.memory.get(int(read.group(2), 16), 0):X}"
                    write = re.fullmatch(r"Target\.WriteU(8|16|32)\(0x([0-9A-F]+), 0x([0-9A-F]+)\)", command)
                    if write:
                        self.memory[int(write.group(2), 16)] = int(write.group(3), 16)
                        return "// returns 0"
                    if command.startswith("Break."):
                        return "// returns 0"
                    raise AssertionError(f"unexpected fake command: {command}")

            fake = FakeClient()
            session = DebugSession(plan, root, fake)  # type: ignore[arg-type]
            read_response = session.handle({"id": "r", "operation": "memory.read", "address": "0x20000000", "size": 16})
            check("native block read request", read_response["ok"] and len(read_response["result"]["data_hex"]) == 32)
            write_response = session.handle({
                "id": "w", "operation": "memory.write_checked", "address": "0x20000010",
                "width": 32, "expected_old": 2, "value": 3, "reason": "test",
                "acknowledge": "DISTURB:123456:memory.write_checked",
            })
            check("checked write and verify", write_response["ok"] and fake.memory[0x20000010] == 3)
            source_response = session.handle({
                "id": "b", "operation": "breakpoint.add_source", "location": "src/bms_control.c:317",
                "implementation": "hardware", "reason": "test",
                "acknowledge": "DISTURB:123456:breakpoint.add_source",
            })
            check("allowlisted source breakpoint", source_response["ok"] and "BP_TYPE_HARD" in source_response["result"]["command"])
            masked_response = session.handle({
                "id": "d", "operation": "breakpoint.add_data", "address": "0x20000020",
                "size": 4, "address_mask": 3, "reason": "test",
                "acknowledge": "DISTURB:123456:breakpoint.add_data",
            })
            check("reject masked watch range", not masked_response["ok"] and "address_mask" in masked_response["error"])
            raw_response = session.handle({"id": "x", "operation": "console.raw", "command": "Debug.Reset()"})
            check("reject raw Ozone console", not raw_response["ok"])
            check("write compact audit records", (root / "audit.jsonl").is_file() and '"data_hex":' not in (root / "audit.jsonl").read_text(encoding="utf-8"))
            with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as port_probe:
                port_probe.bind((LOCAL_HOST, 0))
                control_port = port_probe.getsockname()[1]
            token = "self-test-control-token"
            errors: list[str] = []
            def run_control() -> None:
                try:
                    serve_tcp(session, control_port, token, announce=False)
                except Exception as exc:
                    errors.append(str(exc))
            thread = threading.Thread(target=run_control, daemon=True)
            thread.start()
            deadline = time.monotonic() + 2
            while True:
                try:
                    status = send_tcp_request(control_port, token, {"id": "tcp", "operation": "session.status"})
                    break
                except OSError:
                    if time.monotonic() >= deadline:
                        raise
                    time.sleep(0.02)
            check("localhost authenticated control request", status["ok"] and status["result"]["target_state"] == "halted")
            denied = send_tcp_request(control_port, "incorrect-control-token", {"operation": "session.status"})
            check("reject invalid control token", not denied["ok"])
            closed = send_tcp_request(control_port, token, {"operation": "session.close"})
            thread.join(timeout=2)
            check("close persistent control session", closed["ok"] and not thread.is_alive() and not errors)
        return 0, {"schema_version": SCHEMA_VERSION, "self_test": True, "ok": True, "checks": checks}
    except Exception as exc:
        return 1, {"schema_version": SCHEMA_VERSION, "self_test": True, "ok": False, "checks": checks, "errors": [str(exc)]}


def emit(value: dict[str, Any]) -> None:
    sys.stdout.write(json.dumps(value, ensure_ascii=False) + "\n")
    sys.stdout.flush()


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--config", type=Path)
    parser.add_argument("--dry-run", action="store_true")
    parser.add_argument("--serve-stdio", action="store_true")
    parser.add_argument("--serve-tcp", type=int, metavar="PORT", help="Serve typed requests on localhost only")
    parser.add_argument("--request", type=Path, help="Execute one JSON request in a temporary session")
    parser.add_argument("--send-stdin", action="store_true", help="Send one stdin JSON request to a running localhost agent")
    parser.add_argument("--control-port", type=int, default=19300, help="Local agent port used with --send-stdin")
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    if args.self_test:
        code, result = self_test()
        emit(result)
        return code
    if args.config is None:
        parser.error("--config is required")
    config_path = args.config.resolve()
    try:
        config = json.loads(config_path.read_text(encoding="utf-8"))
        if not isinstance(config, dict):
            raise AgentError("configuration root must be an object")
        if args.send_stdin:
            token = config.get("control_token")
            if not isinstance(token, str) or len(token) < 16:
                raise AgentError("a control_token of at least 16 characters is required")
            request = json.loads(sys.stdin.read())
            if not isinstance(request, dict):
                raise AgentError("stdin request must be a JSON object")
            response = send_tcp_request(args.control_port, token, request)
            emit(response)
            return 0 if response.get("ok") else 1
        plan = validate_config(config, config_path, runtime=not args.dry_run)
        if args.dry_run:
            emit(plan_json(plan))
            return 0
    except (OSError, ValueError, json.JSONDecodeError, LiveAttachError, AgentError) as exc:
        emit({"schema_version": SCHEMA_VERSION, "ok": False, "error": str(exc)})
        return 1
    if not args.serve_stdio and args.serve_tcp is None and args.request is None:
        parser.error("use --serve-stdio, --serve-tcp, --request, --send-stdin, or --dry-run")
    if sum((bool(args.serve_stdio), args.serve_tcp is not None, args.request is not None)) != 1:
        parser.error("choose exactly one runtime mode: --serve-stdio, --serve-tcp, or --request")

    session: DebugSession | None = None
    ozone: subprocess.Popen[bytes] | None = None
    exit_code = 0
    try:
        session, ozone, ready = start_session(plan)
        emit(ready)
        if args.serve_tcp is not None:
            token = plan["control_token"]
            if not isinstance(token, str):
                raise AgentError("--serve-tcp requires control_token in the configuration")
            serve_tcp(session, args.serve_tcp, token)
        elif args.request is not None:
            request = json.loads(args.request.resolve().read_text(encoding="utf-8"))
            if not isinstance(request, dict):
                raise AgentError("request root must be an object")
            response = session.handle(request)
            emit(response)
            exit_code = 0 if response["ok"] else 1
        else:
            for line in sys.stdin:
                if not line.strip():
                    continue
                try:
                    request = json.loads(line)
                    if not isinstance(request, dict):
                        raise AgentError("request must be a JSON object")
                    response = session.handle(request)
                except (json.JSONDecodeError, AgentError) as exc:
                    response = {"schema_version": SCHEMA_VERSION, "ok": False, "error": str(exc), "timestamp": utc_now()}
                emit(response)
                if session.closed:
                    break
    except (OSError, ValueError, json.JSONDecodeError, subprocess.SubprocessError, LiveAttachError, AgentError) as exc:
        emit({"schema_version": SCHEMA_VERSION, "event": "session.error", "ok": False, "error": str(exc)})
        exit_code = 1
    finally:
        emit({"schema_version": SCHEMA_VERSION, "event": "session.cleanup", "ok": True, "cleanup": stop_session(session, ozone)})
    return exit_code


if __name__ == "__main__":
    raise SystemExit(main())
