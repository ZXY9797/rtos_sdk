#!/usr/bin/env python3
"""Fail a build when a binary image exceeds its physical partition."""

from __future__ import annotations

import argparse
from pathlib import Path


def parse_size(value: str) -> int:
    size = int(value, 0)
    if size <= 0:
        raise argparse.ArgumentTypeError("partition size must be positive")
    return size


def validate_image_size(image: Path, maximum: int, label: str) -> int:
    if maximum <= 0:
        raise ValueError("partition size must be positive")
    if not image.is_file():
        raise ValueError(f"{label} image not found: {image}")

    actual = image.stat().st_size
    if actual > maximum:
        raise ValueError(
            f"{label} image is {actual} bytes, exceeds {maximum}-byte "
            "physical partition"
        )
    return actual


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--image", type=Path, required=True)
    parser.add_argument("--max-size", type=parse_size, required=True)
    parser.add_argument("--label", default="firmware")
    args = parser.parse_args()

    try:
        actual = validate_image_size(args.image, args.max_size, args.label)
    except ValueError as exc:
        parser.error(str(exc))

    headroom = args.max_size - actual
    usage = actual * 100.0 / args.max_size
    print(
        f"{args.label} partition: {actual}/{args.max_size} bytes "
        f"({usage:.2f}%), headroom {headroom} bytes"
    )


if __name__ == "__main__":
    main()
