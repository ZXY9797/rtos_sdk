#!/usr/bin/env python3
"""Merge preloader + loader + app into one factory firmware image."""

from __future__ import annotations

import argparse
from pathlib import Path
import sys

from boot_layout import all_layouts
from pack_image import validate_image


KB = 1024

def _merge_layouts() -> dict[str, dict[str, object]]:
    result: dict[str, dict[str, object]] = {}
    for name, source in all_layouts().items():
        source_partitions = source["partitions"]
        slot = source_partitions["slot0"]
        upgrade = source_partitions["upgrade"]
        result[name] = {
            "canonical_name": source["name"],
            "flash_base": source["flash_base"],
            "partitions": {
                "preloader": source_partitions["preloader"],
                "loader": source_partitions["loader"],
                "app": {
                    "offset": slot["offset"],
                    "size": slot["size"],
                    "max_image_size": min(slot["size"], upgrade["size"]),
                },
            },
        }
    return result


LAYOUTS = _merge_layouts()


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
        try:
            validate_image(images["app"], str(layout["canonical_name"]))
        except ValueError as exc:
            raise SystemExit(f"error: invalid app image: {exc}") from exc

    for name, data in images.items():
        part = partitions[name]
        maximum = part.get("max_image_size", part["size"])
        if len(data) > maximum:
            raise SystemExit(
                f"error: {name} image size {len(data)} exceeds "
                f"installable size {maximum}"
            )
        addr = flash_base + part["offset"]
        print(f"{name:9s}: {len(data)} bytes (max {maximum}) @ 0x{addr:08X}")

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
    parser.add_argument("--app", help="packaged application image path")
    parser.add_argument("--output", default="firmware.bin", help="output path")
    args = parser.parse_args()
    merge(args)


if __name__ == "__main__":
    main()
