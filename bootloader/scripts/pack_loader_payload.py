#!/usr/bin/env python3
"""Generate a checked loader-upgrade manifest and C++ payload source."""

import argparse
import hashlib
import struct
from pathlib import Path


MAGIC = 0x5052444C
VERSION = 1
HEADER = struct.Struct('<IHHI32sI')


def build_blob(payload: bytes) -> bytes:
    if not payload:
        raise ValueError('loader payload is empty')
    if len(payload) > 0xFFFFFFFF:
        raise ValueError('loader payload exceeds uint32 range')
    digest = hashlib.sha256(payload).digest()
    return HEADER.pack(
        MAGIC, HEADER.size, VERSION, len(payload), digest, 0) + payload


def render_source(blob: bytes) -> str:
    rows = []
    for offset in range(0, len(blob), 12):
        values = ', '.join(
            f'0x{value:02X}U' for value in blob[offset:offset + 12])
        rows.append(f'    {values},')
    return '\n'.join([
        '#include <cstdint>',
        '',
        'extern "C" {',
        '__attribute__((section(".loader_payload"), used, aligned(4)))',
        'extern const uint8_t g_loader_payload[] = {',
        *rows,
        '};',
        'extern const uint32_t g_loader_payload_size =',
        '    static_cast<uint32_t>(sizeof(g_loader_payload));',
        '}',
        '',
    ])


def write_if_changed(path: Path, text: str) -> None:
    encoded = text.encode('utf-8')
    if path.exists() and path.read_bytes() == encoded:
        return
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(encoded)


def parse_args():
    parser = argparse.ArgumentParser(allow_abbrev=False)
    parser.add_argument('--input', required=True, type=Path)
    parser.add_argument('--output', required=True, type=Path)
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    payload = args.input.read_bytes()
    blob = build_blob(payload)
    write_if_changed(args.output, render_source(blob))
    print(f'loader payload: {args.input} ({len(payload)} bytes)')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
