#!/usr/bin/env python3
"""Retain the transitive CMake helper API used by first-party build files."""
from __future__ import annotations
import argparse
import hashlib
import json
from pathlib import Path
import re

DEFINITION = re.compile(r"^\s*(function|macro)\s*\(\s*([A-Za-z_][A-Za-z_0-9]*)", re.I)
END = re.compile(r"^\s*end(function|macro)\s*\(", re.I)
CALL = re.compile(r"^[ \t]*([A-Za-z_][A-Za-z_0-9]*)[ \t\r\n]*\(", re.M)


def reduce(source: str, consumers: str):
    chunks, outside, blocks = [], [], {}
    lines = source.splitlines(keepends=True)
    i = 0
    while i < len(lines):
        match = DEFINITION.match(lines[i])
        if not match:
            outside.append(lines[i]); chunks.append((None, lines[i])); i += 1; continue
        name, start, depth = match[2].lower(), i, 1
        i += 1
        while i < len(lines) and depth:
            if DEFINITION.match(lines[i]): depth += 1
            if END.match(lines[i]): depth -= 1
            i += 1
        if depth: raise ValueError(f"Unterminated helper {name}")
        block = "".join(lines[start:i])
        blocks[name] = block
        chunks.append((name, block))
    used = {name.lower() for name in CALL.findall(consumers + "".join(outside))} & blocks.keys()
    while True:
        expanded = used | ({name.lower() for key in used for name in CALL.findall(blocks[key])} & blocks.keys())
        if expanded == used: break
        used = expanded
    output = ["# SPDX-License-Identifier: Apache-2.0\n",
              "# Reduced from the previously vendored Zephyr helpers; see upstream-helpers.json.\n"]
    for name, chunk in chunks:
        if name in used:
            output.extend(["\n", chunk])
        elif name is None:
            # Preserve executable top-level scaffolding and license notices.
            stripped = chunk.strip()
            if stripped in ('include(user_cache)', 'include(CheckCCompilerFlag)',
                            'include(CheckCXXCompilerFlag)', 'set(TYPES "FLASH" "DEBUG" "SIM" "ROBOT")'):
                continue
            if stripped and (not stripped.startswith("#") or "Copyright" in stripped): output.append(chunk)
    return "".join(output), sorted(used)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--root", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--provenance", type=Path, required=True)
    args = parser.parse_args()
    consumers = []
    for directory in ("app", "embedded", "bootloader", "component", "tools/cmake", "share"):
        for path in (args.root / directory).rglob("*"):
            relative = path.relative_to(args.root).as_posix()
            if any(x in relative for x in ("/osal/rtos/", "/cmsis/CMSIS/", "/ble/goodix/libraries/")): continue
            if path == args.output or not path.is_file(): continue
            if path.suffix == ".cmake" or path.name == "CMakeLists.txt": consumers.append(path.read_text(encoding="utf-8"))
    consumers.append((args.root / "CMakeLists.txt").read_text(encoding="utf-8"))
    original = args.source.read_text(encoding="utf-8")
    output, retained = reduce(original, "\n".join(consumers))
    args.output.write_text(output, encoding="utf-8")
    args.provenance.write_text(json.dumps({"upstream": "https://github.com/zephyrproject-rtos/zephyr",
        "original_revision": "not recorded by original import", "license": "Apache-2.0",
        "original_sha256": hashlib.sha256(original.encode()).hexdigest(),
        "original_lines": len(original.splitlines()), "retained_lines": len(output.splitlines()),
        "retained_helpers": retained}, indent=2) + "\n", encoding="utf-8")
    print(f"CMake helpers: {len(original.splitlines())} -> {len(output.splitlines())} lines; {len(retained)} helpers")


if __name__ == "__main__": main()
