#!/usr/bin/env python3
"""Merge preloader + loader + app into one factory firmware image."""

from __future__ import annotations

import argparse
from pathlib import Path
import sys


KB = 1024

LAYOUTS = {
    "demo": {
        "flash_base": 0x08000000,
        "partitions": {
            "preloader": {"offset": 0x00000000, "size": 16 * KB},
            "loader": {"offset": 0x00004000, "size": 24 * KB},
            "app": {"offset": 0x0000A000, "size": 160 * KB},
        },
    },
    "gd32f503": {
        "flash_base": 0x08000000,
        "partitions": {
            "preloader": {"offset": 0x00000000, "size": 16 * KB},
            "loader": {"offset": 0x00004000, "size": 24 * KB},
            "app": {"offset": 0x0000A000, "size": 160 * KB},
        },
    },
    "demo_ble": {
        "flash_base": 0x00200000,
        "partitions": {
            "preloader": {"offset": 0x00002000, "size": 16 * KB},
            "loader": {"offset": 0x00006000, "size": 24 * KB},
            "app": {"offset": 0x0000C000, "size": 160 * KB},
        },
    },
    "gr5525": {
        "flash_base": 0x00200000,
        "partitions": {
            "preloader": {"offset": 0x00002000, "size": 16 * KB},
            "loader": {"offset": 0x00006000, "size": 24 * KB},
            "app": {"offset": 0x0000C000, "size": 160 * KB},
        },
    },
}


def read_bin(path: Path, name: str) -> bytes:
    try:
        return path.read_bytes()
    except OSError as exc:
        raise SystemExit(f"error: cannot read {name} image: {path}: {exc}") from exc


def pad_to(data: bytes, size: int, name: str) -> bytes:
    if len(data) > size:
        raise SystemExit(
            f"error: {name} image size {len(data)} exceeds partition size {size}"
        )
    return data + b"\xff" * (size - len(data))


def layout_total_size(partitions: dict[str, dict[str, int]], include_app: bool) -> int:
    names = ("preloader", "loader", "app") if include_app else ("preloader", "loader")
    return max(partitions[name]["offset"] + partitions[name]["size"] for name in names)


def merge(args: argparse.Namespace) -> None:
    layout = LAYOUTS[args.layout]
    partitions = layout["partitions"]
    flash_base = layout["flash_base"]

    images = {
        "preloader": read_bin(Path(args.preloader), "preloader"),
        "loader": read_bin(Path(args.loader), "loader"),
    }
    if args.app:
        images["app"] = read_bin(Path(args.app), "app")

    for name, data in images.items():
        part = partitions[name]
        addr = flash_base + part["offset"]
        print(f"{name:9s}: {len(data)} bytes (max {part['size']}) @ 0x{addr:08X}")

    total_size = layout_total_size(partitions, "app" in images)
    firmware = bytearray(b"\xff" * total_size)

    for name, data in images.items():
        part = partitions[name]
        offset = part["offset"]
        size = part["size"]
        firmware[offset:offset + size] = pad_to(data, size, name)

    output = Path(args.output)
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_bytes(firmware)

    print(f"\noutput: {output} ({len(firmware)} bytes = {len(firmware) // KB} KB)")
    print(f"flash range: 0x{flash_base:08X} - 0x{flash_base + len(firmware):08X}")


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Merge preloader + loader + app into one firmware image"
    )
    parser.add_argument(
        "--layout",
        default="demo",
        choices=sorted(LAYOUTS.keys()),
        help="flash layout to use",
    )
    parser.add_argument("--preloader", required=True, help="preloader.bin path")
    parser.add_argument("--loader", required=True, help="loader.bin path")
    parser.add_argument("--app", help="app .bin path")
    parser.add_argument("--output", default="firmware.bin", help="output path")
    args = parser.parse_args()
    merge(args)


if __name__ == "__main__":
    main()
