#!/usr/bin/env python3
"""Create and validate the bootloader's 128-byte application image header."""

from __future__ import annotations

import argparse
import hashlib
from pathlib import Path
import struct
import zlib

from boot_layout import all_layouts


IMAGE_MAGIC = 0x96F3B83D
IMAGE_HEADER_SIZE = 128
IMAGE_F_PENDING = 0xFFFE
IMAGE_F_CONFIRMED = 0xFFFC
PRODUCT_INFO_MAGIC = 0x50524F44
HEADER_FORMAT = "<IIHHIBBHII32s64sI"
P256_FIELD = 0xFFFFFFFF00000001000000000000000000000000FFFFFFFFFFFFFFFFFFFFFFFF
P256_A = P256_FIELD - 3
P256_B = 0x5AC635D8AA3A93E7B3EBBD55769886BC651D06B0CC53B0F63BCE3C3E27D2604B
P256_G = (
    0x6B17D1F2E12C4247F8BCE6E563A440F277037D812DEB33A0F4A13945D898C296,
    0x4FE342E2FE1A7F9B8EE7EB4A7C0F9E162BCE33576B315ECECBB6406837BF51F5,
)
P256_ORDER = 0xFFFFFFFF00000000FFFFFFFFFFFFFFFFBCE6FAADA7179E84F3B9CAC2FC632551


def _p256_add(
    left: tuple[int, int] | None,
    right: tuple[int, int] | None,
) -> tuple[int, int] | None:
    if left is None:
        return right
    if right is None:
        return left
    x1, y1 = left
    x2, y2 = right
    if x1 == x2 and (y1 + y2) % P256_FIELD == 0:
        return None
    if left == right:
        slope = ((3 * x1 * x1 + P256_A)
                 * pow(2 * y1, -1, P256_FIELD)) % P256_FIELD
    else:
        slope = ((y2 - y1)
                 * pow((x2 - x1) % P256_FIELD, -1, P256_FIELD)) % P256_FIELD
    x3 = (slope * slope - x1 - x2) % P256_FIELD
    return x3, (slope * (x1 - x3) - y1) % P256_FIELD


def _p256_mul(
    scalar: int,
    point: tuple[int, int],
) -> tuple[int, int] | None:
    result = None
    addend: tuple[int, int] | None = point
    while scalar:
        if scalar & 1:
            result = _p256_add(result, addend)
        addend = _p256_add(addend, addend)
        scalar >>= 1
    return result


def verify_p256_signature(
    public_key: bytes,
    digest: bytes,
    signature: bytes,
) -> bool:
    """Verify raw x||y and r||s values without an optional Python package."""
    if len(public_key) != 64 or len(digest) != 32 or len(signature) != 64:
        return False
    x = int.from_bytes(public_key[:32], "big")
    y = int.from_bytes(public_key[32:], "big")
    if (x >= P256_FIELD or y >= P256_FIELD
            or (y * y - (x * x * x + P256_A * x + P256_B))
            % P256_FIELD != 0):
        return False
    r = int.from_bytes(signature[:32], "big")
    s = int.from_bytes(signature[32:], "big")
    if not (1 <= r < P256_ORDER and 1 <= s < P256_ORDER):
        return False
    inverse = pow(s, -1, P256_ORDER)
    z = int.from_bytes(digest, "big")
    point = _p256_add(
        _p256_mul((z * inverse) % P256_ORDER, P256_G),
        _p256_mul((r * inverse) % P256_ORDER, (x, y)),
    )
    return point is not None and point[0] % P256_ORDER == r


def _pack_layouts() -> dict[str, dict[str, int]]:
    result: dict[str, dict[str, int]] = {}
    for name, source in all_layouts().items():
        slot = source["partitions"]["slot0"]
        upgrade = source["partitions"]["upgrade"]
        if source["image_header_size"] != IMAGE_HEADER_SIZE:
            raise ValueError(f"layout {name} uses an unsupported image header size")
        result[name] = {
            "slot_addr": source["flash_base"] + slot["offset"],
            "image_size": min(slot["size"], upgrade["size"]),
            "product_id": source["product_id"],
            "product_info_offset": source["product_info_offset"],
            "ram_base": source["ram_base"],
            "ram_size": source["ram_size"],
        }
    return result


LAYOUTS = _pack_layouts()


def parse_product_info(
    payload: bytes,
    product_id: int,
    product_info_offset: int,
) -> tuple[int, ...]:
    offset = product_info_offset - IMAGE_HEADER_SIZE
    if len(payload) < offset + 64:
        raise ValueError("payload does not contain ProductInfo")

    info = payload[offset:offset + 64]
    magic, actual_product_id = struct.unpack_from("<II", info)
    if magic != PRODUCT_INFO_MAGIC:
        raise ValueError("invalid ProductInfo magic")
    if actual_product_id != product_id:
        raise ValueError(
            f"ProductInfo ID 0x{actual_product_id:08X} does not match "
            f"layout ID 0x{product_id:08X}"
        )

    expected_crc = struct.unpack_from("<I", info, 60)[0]
    if zlib.crc32(info[:48]) != expected_crc:
        raise ValueError("invalid ProductInfo CRC-32")
    return struct.unpack_from("<BBHI", info, 12)


def validate_vectors(
    payload: bytes,
    load_addr: int,
    ram_base: int,
    ram_size: int,
) -> None:
    if len(payload) < 8:
        raise ValueError("payload is too small for a vector table")
    stack_pointer, reset_handler = struct.unpack_from("<II", payload)
    entry_addr = reset_handler & ~1
    ram_end = ram_base + ram_size
    if stack_pointer < ram_base or stack_pointer > ram_end:
        raise ValueError("initial stack pointer is outside supported SRAM")
    if stack_pointer & 0x7:
        raise ValueError("initial stack pointer is not 8-byte aligned")
    if not reset_handler & 1:
        raise ValueError("reset handler is not a Thumb address")
    if entry_addr < load_addr or entry_addr >= load_addr + len(payload):
        raise ValueError("reset handler is outside the application payload")


def canonical_header_crc(header: bytes) -> int:
    if len(header) != IMAGE_HEADER_SIZE:
        raise ValueError("invalid image header size")
    canonical = bytearray(header)
    struct.pack_into("<H", canonical, 10, 0)
    struct.pack_into("<I", canonical, 124, 0)
    return zlib.crc32(canonical)


def authentication_digest(header: bytes) -> bytes:
    if len(header) != IMAGE_HEADER_SIZE:
        raise ValueError("invalid image header size")
    canonical = bytearray(header)
    struct.pack_into("<H", canonical, 10, 0)
    canonical[60:124] = bytes(64)
    struct.pack_into("<I", canonical, 124, 0)
    return hashlib.sha256(canonical).digest()


def build_image(
    payload: bytes,
    layout_name: str,
    security_version: int,
    flags: int,
    signature: bytes,
) -> bytes:
    layout = LAYOUTS[layout_name]
    if len(payload) + IMAGE_HEADER_SIZE > layout["image_size"]:
        raise ValueError("application image exceeds the installable limit")
    if len(signature) != 64:
        raise ValueError("ECDSA-P256 signature must contain 64 raw bytes")

    load_addr = layout["slot_addr"] + IMAGE_HEADER_SIZE
    validate_vectors(
        payload, load_addr, layout["ram_base"], layout["ram_size"])
    version = parse_product_info(
        payload, layout["product_id"], layout["product_info_offset"])
    digest = hashlib.sha256(payload).digest()

    fields = (
        IMAGE_MAGIC,
        load_addr,
        IMAGE_HEADER_SIZE,
        flags,
        len(payload),
        *version,
        security_version,
        digest,
        signature,
        0,
    )
    header = struct.pack(HEADER_FORMAT, *fields)
    crc32 = canonical_header_crc(header)
    header = header[:124] + struct.pack("<I", crc32)
    return header + payload


def validate_image(image: bytes, layout_name: str) -> None:
    if len(image) < IMAGE_HEADER_SIZE:
        raise ValueError("image is smaller than its header")
    fields = struct.unpack_from(HEADER_FORMAT, image)
    magic, load_addr, header_size, flags, payload_size = fields[:5]
    security_version = fields[9]
    digest = fields[10]
    header_crc = fields[12]
    layout = LAYOUTS[layout_name]

    if magic != IMAGE_MAGIC or header_size != IMAGE_HEADER_SIZE:
        raise ValueError("invalid image header magic or size")
    if load_addr != layout["slot_addr"] + IMAGE_HEADER_SIZE:
        raise ValueError("image load address does not match the layout")
    if flags not in (IMAGE_F_CONFIRMED, IMAGE_F_PENDING):
        raise ValueError("invalid image state flags")
    if payload_size != len(image) - IMAGE_HEADER_SIZE:
        raise ValueError("image payload length does not match the header")
    if len(image) > layout["image_size"]:
        raise ValueError("image exceeds the installable limit")
    if canonical_header_crc(image[:IMAGE_HEADER_SIZE]) != header_crc:
        raise ValueError("invalid image header CRC-32")

    payload = image[IMAGE_HEADER_SIZE:]
    if hashlib.sha256(payload).digest() != digest:
        raise ValueError("invalid image payload SHA-256")
    validate_vectors(
        payload, load_addr, layout["ram_base"], layout["ram_size"])
    parse_product_info(
        payload, layout["product_id"], layout["product_info_offset"])
    if security_version < 0:
        raise ValueError("invalid security version")


def read_signature(path: Path | None) -> bytes:
    if path is None:
        return bytes(64)
    signature = path.read_bytes()
    if len(signature) != 64:
        raise ValueError("signature file must be exactly 64 bytes")
    return signature


def read_public_key(path: Path | None) -> bytes | None:
    if path is None:
        return None
    public_key = path.read_bytes()
    if len(public_key) != 64:
        raise ValueError("public key file must be exactly 64 raw bytes")
    return public_key


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--layout", required=True, choices=sorted(LAYOUTS))
    parser.add_argument("--input", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--security-version", type=int, default=0)
    parser.add_argument("--signature", type=Path)
    parser.add_argument(
        "--public-key",
        type=Path,
        help="verify the raw signature with this 64-byte P-256 x||y key",
    )
    parser.add_argument(
        "--auth-digest-output",
        type=Path,
        help="write the canonical 32-byte digest to be signed",
    )
    parser.add_argument(
        "--pending",
        action="store_true",
        help="mark an OTA/DFU image pending; factory slot0 images stay confirmed",
    )
    args = parser.parse_args()

    if args.security_version < 0 or args.security_version > 0xFFFFFFFF:
        parser.error("--security-version must be a uint32 value")
    try:
        signature = read_signature(args.signature)
        public_key = read_public_key(args.public_key)
        if public_key is not None and args.signature is None:
            raise ValueError("--public-key requires --signature")
        flags = IMAGE_F_PENDING if args.pending else IMAGE_F_CONFIRMED
        image = build_image(
            args.input.read_bytes(),
            args.layout,
            args.security_version,
            flags,
            signature,
        )
        validate_image(image, args.layout)
        if public_key is not None and not verify_p256_signature(
                public_key,
                authentication_digest(image[:IMAGE_HEADER_SIZE]),
                signature):
            raise ValueError("ECDSA-P256 signature verification failed")
    except (OSError, ValueError) as exc:
        parser.error(str(exc))

    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(image)
    if args.auth_digest_output is not None:
        args.auth_digest_output.parent.mkdir(parents=True, exist_ok=True)
        args.auth_digest_output.write_bytes(
            authentication_digest(image[:IMAGE_HEADER_SIZE]))
    print(
        f"image: {args.output} ({len(image)} bytes, "
        f"security version {args.security_version})"
    )


if __name__ == "__main__":
    main()
