#!/usr/bin/env python3
"""Load and validate product Flash layouts from their canonical manifests."""

from __future__ import annotations

import json
import argparse
from pathlib import Path
from typing import Any


PRODUCT_ROOT = Path(__file__).resolve().parents[1] / "product"
PARTITION_ORDER = (
    "preloader", "loader", "slot0", "upgrade", "storage", "scratch",
    "boot_ctrl",
)


def _integer(value: Any, field: str) -> int:
    if isinstance(value, bool):
        raise ValueError(f"{field} must be an integer or base-prefixed string")
    if isinstance(value, int):
        result = value
    elif isinstance(value, str):
        result = int(value, 0)
    else:
        raise ValueError(f"{field} must be an integer or base-prefixed string")
    if result < 0 or result > 0xFFFFFFFF:
        raise ValueError(f"{field} is outside uint32 range")
    return result


def _load_canonical(product: str) -> dict[str, Any]:
    path = PRODUCT_ROOT / product / "layout.json"
    try:
        raw = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        raise ValueError(f"cannot load layout {path}: {exc}") from exc
    if raw.get("schema_version") != 1:
        raise ValueError(f"unsupported layout schema in {path}")

    aliases = raw.get("aliases", ())
    if (not isinstance(aliases, list)
            or any(not isinstance(alias, str) or not alias for alias in aliases)):
        raise ValueError(f"aliases must be a list of non-empty strings in {path}")
    layout: dict[str, Any] = {
        "name": product,
        "path": path,
        "aliases": tuple(aliases),
    }
    for field in (
        "product_id", "flash_base", "flash_size", "erase_block_size",
        "write_block_size", "ram_base", "ram_size",
        "image_header_size", "product_info_offset",
    ):
        layout[field] = _integer(raw.get(field), field)

    raw_partitions = raw.get("partitions")
    if not isinstance(raw_partitions, dict):
        raise ValueError(f"partitions must be an object in {path}")
    partitions: dict[str, dict[str, int]] = {}
    previous_end: int | None = None
    for name in PARTITION_ORDER:
        raw_part = raw_partitions.get(name)
        if not isinstance(raw_part, dict):
            raise ValueError(f"missing partition {name} in {path}")
        offset = _integer(raw_part.get("offset"), f"{name}.offset")
        size = _integer(raw_part.get("size"), f"{name}.size")
        if size == 0:
            raise ValueError(f"partition {name} is empty")
        if previous_end is not None and offset != previous_end:
            raise ValueError(f"partition {name} is not contiguous")
        if offset + size > 0x100000000:
            raise ValueError(f"partition {name} exceeds uint32 address space")
        partitions[name] = {"offset": offset, "size": size}
        previous_end = offset + size
    layout["partitions"] = partitions

    if layout["flash_size"] == 0 or previous_end > layout["flash_size"]:
        raise ValueError("partitions exceed product Flash capacity")
    erase_size = layout["erase_block_size"]
    write_size = layout["write_block_size"]
    if (erase_size == 0 or write_size == 0 or write_size > erase_size
            or erase_size % write_size != 0
            or layout["flash_size"] % erase_size != 0):
        raise ValueError("product Flash geometry is invalid")
    for name, partition in partitions.items():
        if (partition["offset"] % erase_size != 0
                or partition["size"] % erase_size != 0):
            raise ValueError(
                f"partition {name} is not erase-block aligned")
    if partitions["boot_ctrl"]["size"] < erase_size * 2:
        raise ValueError("boot_ctrl must contain at least two erase blocks")
    if partitions["scratch"]["size"] != erase_size:
        raise ValueError("scratch must contain exactly one erase block")
    if partitions["upgrade"]["size"] <= layout["image_header_size"]:
        raise ValueError("image header does not fit upgrade")
    if (layout["image_header_size"] == 0
            or layout["image_header_size"] % write_size != 0):
        raise ValueError("image header size is not write-block aligned")
    if layout["flash_base"] + layout["flash_size"] > 0x100000000:
        raise ValueError("product Flash address range exceeds uint32")
    if (layout["ram_size"] == 0
            or layout["ram_base"] + layout["ram_size"] > 0x100000000):
        raise ValueError("product RAM address range is invalid")

    slot = partitions["slot0"]
    if layout["image_header_size"] >= slot["size"]:
        raise ValueError("image header does not fit slot0")
    product_info_offset = layout["product_info_offset"]
    installable_size = min(slot["size"], partitions["upgrade"]["size"])
    if (product_info_offset < layout["image_header_size"]
            or product_info_offset + 64 > installable_size
            or product_info_offset % write_size != 0):
        raise ValueError("ProductInfo offset does not fit installable image")
    return layout


def canonical_layout_names() -> tuple[str, ...]:
    return tuple(sorted(
        path.parent.name for path in PRODUCT_ROOT.glob("*/layout.json")
    ))


def all_layouts() -> dict[str, dict[str, Any]]:
    layouts: dict[str, dict[str, Any]] = {}
    for name in canonical_layout_names():
        layout = _load_canonical(name)
        for key in (name, *layout["aliases"]):
            if key in layouts:
                raise ValueError(f"duplicate boot layout name: {key}")
            layouts[key] = layout
    return layouts


def load_layout(name: str) -> dict[str, Any]:
    try:
        return all_layouts()[name]
    except KeyError as exc:
        raise ValueError(f"unknown boot layout: {name}") from exc


def _cmake_value(value: int) -> str:
    return f"0x{value:X}"


def emit_cmake(layout: dict[str, Any], output: Path) -> None:
    """Emit validated primitive values for CMake without re-parsing JSON."""
    fields = {
        "BOOT_LAYOUT_SCHEMA_VERSION": 1,
        "BOOT_PRODUCT_ID": layout["product_id"],
        "BOOT_FLASH_BASE": layout["flash_base"],
        "BOOT_FLASH_SIZE": layout["flash_size"],
        "BOOT_ERASE_BLOCK_SIZE": layout["erase_block_size"],
        "BOOT_WRITE_BLOCK_SIZE": layout["write_block_size"],
        "BOOT_RAM_BASE": layout["ram_base"],
        "BOOT_RAM_SIZE": layout["ram_size"],
        "BOOT_IMAGE_HEADER_SIZE": layout["image_header_size"],
        "BOOT_PRODUCT_INFO_OFFSET": layout["product_info_offset"],
    }
    for name, partition in layout["partitions"].items():
        prefix = f"BOOT_{name.upper()}"
        fields[f"{prefix}_OFFSET"] = partition["offset"]
        fields[f"{prefix}_SIZE"] = partition["size"]

    lines = [
        "# Generated by boot_layout.py; do not edit.",
        *(f'set({name} "{_cmake_value(value)}")'
          for name, value in fields.items()),
        "",
    ]
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text("\n".join(lines), encoding="utf-8")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("product", help="canonical product name or alias")
    parser.add_argument("--cmake-output", type=Path, required=True)
    args = parser.parse_args()
    try:
        emit_cmake(load_layout(args.product), args.cmake_output)
    except ValueError as exc:
        parser.error(str(exc))


if __name__ == "__main__":
    main()
