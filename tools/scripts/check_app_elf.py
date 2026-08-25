#!/usr/bin/env python3
"""Fail-closed structural checks for an application ELF."""

import argparse
import re
import shutil
import subprocess
import sys
from pathlib import Path


REQUIRED_EXACT = (
    "z_cstart",
    "arm_fault_handler",
    "__preinit_array_start",
    "__preinit_array_end",
    "__init_array_start",
    "__init_array_end",
    "__init_start",
    "__init_EARLY_start",
    "__init_PRE_KERNEL_1_start",
    "__init_PRE_KERNEL_2_start",
    "__init_PRE_KERNEL_3_start",
    "__init_POST_KERNEL_start",
    "__init_APPLICATION_start",
    "__init_end",
)
REQUIRED_PREFIX = ("_ZN3hal5fault5panic",)


def run(tool: str, *arguments: str) -> str:
    result = subprocess.run(
        [tool, *arguments], check=False, text=True,
        stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    if result.returncode != 0:
        raise RuntimeError(result.stderr.strip() or f"{tool} failed")
    return result.stdout


def resolve_tool(explicit: str | None, default: str) -> str:
    candidate = explicit or shutil.which(default)
    if not candidate:
        option = default.split("-")[-1]
        raise RuntimeError(f"tool not found: {default}; pass --{option}")
    return candidate


def check_elf(path: Path, nm: str, objdump: str) -> list[str]:
    errors: list[str] = []
    symbols: dict[str, int] = {}
    for line in run(nm, "-n", str(path)).splitlines():
        fields = line.split()
        if len(fields) >= 3:
            try:
                symbols[fields[-1]] = int(fields[0], 16)
            except ValueError:
                continue

    for required in REQUIRED_EXACT:
        if required not in symbols:
            errors.append(f"missing required symbol: {required}")
    for prefix in REQUIRED_PREFIX:
        if not any(symbol.startswith(prefix) for symbol in symbols):
            errors.append(f"missing required symbol prefix: {prefix}")

    start = symbols.get("__init_array_start")
    end = symbols.get("__init_array_end")
    if start is not None and end is not None and end <= start:
        errors.append("C++ constructor array is empty")

    init_markers = (
        "__init_start",
        "__init_EARLY_start",
        "__init_PRE_KERNEL_1_start",
        "__init_PRE_KERNEL_2_start",
        "__init_PRE_KERNEL_3_start",
        "__init_POST_KERNEL_start",
        "__init_APPLICATION_start",
        "__init_end",
    )
    if all(marker in symbols for marker in init_markers):
        addresses = [symbols[marker] for marker in init_markers]
        if addresses[-1] <= addresses[0]:
            errors.append("initcall table is empty")
        if any(current > following
               for current, following in zip(addresses, addresses[1:])):
            errors.append("initcall level markers are out of order")

    section_table = run(objdump, "-h", str(path))
    match = re.search(
        r"^\s*\d+\s+\.?noinit\s+([0-9a-fA-F]+)\s+"
        r"([0-9a-fA-F]+)\s",
                      section_table, re.MULTILINE)
    if match is None or int(match.group(1), 16) == 0:
        errors.append("noinit fault persistence section is missing or empty")
    elif not any(
            symbol.endswith("s_faultRecordE")
            and int(match.group(2), 16) <= address
            < int(match.group(2), 16) + int(match.group(1), 16)
            for symbol, address in symbols.items()):
        errors.append("persistent fault record is not inside noinit")

    disassembly = run(objdump, "-d", str(path))
    reset = re.search(
        r"^[0-9a-fA-F]+ <Reset_Handler>:\n(?P<body>(?:^\s.*\n)*)",
        disassembly, re.MULTILINE)
    if reset is None or "<z_cstart>" not in reset.group("body"):
        errors.append("Reset_Handler does not branch to z_cstart")

    undefined = run(nm, "-u", str(path)).strip()
    if undefined:
        errors.append(f"undefined symbols remain:\n{undefined}")
    return errors


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("elf", type=Path)
    parser.add_argument("--nm")
    parser.add_argument("--objdump")
    args = parser.parse_args()
    if not args.elf.is_file():
        parser.error(f"ELF does not exist: {args.elf}")
    try:
        nm = resolve_tool(args.nm, "arm-none-eabi-nm")
        objdump = resolve_tool(args.objdump, "arm-none-eabi-objdump")
        errors = check_elf(args.elf, nm, objdump)
    except RuntimeError as error:
        print(f"ERROR: {error}", file=sys.stderr)
        return 1
    if errors:
        for error in errors:
            print(f"ERROR: {error}", file=sys.stderr)
        return 1
    print(f"Application ELF policy checks passed: {args.elf}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
