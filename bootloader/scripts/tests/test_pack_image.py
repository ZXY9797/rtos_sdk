from __future__ import annotations

import struct
import sys
from pathlib import Path
import unittest
import zlib


SCRIPT_DIR = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(SCRIPT_DIR))

import pack_image  # noqa: E402


def make_payload(layout_name: str) -> bytes:
    layout = pack_image.LAYOUTS[layout_name]
    load_addr = layout["slot_addr"] + pack_image.IMAGE_HEADER_SIZE
    payload = bytearray(b"\xA5" * 2048)
    struct.pack_into("<II", payload, 0, 0x20010000, (load_addr + 0x21) | 1)

    info_offset = (
        layout["product_info_offset"] - pack_image.IMAGE_HEADER_SIZE
    )
    info = bytearray(64)
    struct.pack_into(
        "<IIIBBHI", info, 0,
        pack_image.PRODUCT_INFO_MAGIC,
        layout["product_id"],
        3,
        1, 2, 7, 42,
    )
    struct.pack_into("<I", info, 60, zlib.crc32(info[:48]))
    payload[info_offset:info_offset + len(info)] = info
    return bytes(payload)


class PackImageTest(unittest.TestCase):
    def setUp(self) -> None:
        self.payload = make_payload("demo")
        self.image = pack_image.build_image(
            self.payload,
            "demo",
            security_version=9,
            flags=pack_image.IMAGE_F_PENDING,
            signature=bytes(range(64)),
        )

    def test_build_and_validate(self) -> None:
        pack_image.validate_image(self.image, "demo")
        self.assertEqual(len(self.image), len(self.payload) + 128)

    def test_payload_tamper_is_rejected(self) -> None:
        tampered = bytearray(self.image)
        tampered[-1] ^= 1
        with self.assertRaisesRegex(ValueError, "SHA-256"):
            pack_image.validate_image(bytes(tampered), "demo")

    def test_header_tamper_is_rejected(self) -> None:
        tampered = bytearray(self.image)
        tampered[20] ^= 1
        with self.assertRaisesRegex(ValueError, "CRC-32"):
            pack_image.validate_image(bytes(tampered), "demo")

    def test_pending_to_confirmed_is_canonical(self) -> None:
        self.assertEqual(
            pack_image.IMAGE_F_PENDING & pack_image.IMAGE_F_CONFIRMED,
            pack_image.IMAGE_F_CONFIRMED,
        )
        confirmed = bytearray(self.image)
        struct.pack_into("<H", confirmed, 10, pack_image.IMAGE_F_CONFIRMED)
        pack_image.validate_image(bytes(confirmed), "demo")

    def test_authentication_digest_canonicalizes_mutable_fields(self) -> None:
        original = self.image[:pack_image.IMAGE_HEADER_SIZE]
        mutable = bytearray(original)
        struct.pack_into("<H", mutable, 10, pack_image.IMAGE_F_CONFIRMED)
        mutable[60:124] = bytes(reversed(range(64)))
        struct.pack_into("<I", mutable, 124, 0x12345678)
        self.assertEqual(
            pack_image.authentication_digest(original),
            pack_image.authentication_digest(bytes(mutable)),
        )

        authenticated = bytearray(original)
        authenticated[20] ^= 1
        self.assertNotEqual(
            pack_image.authentication_digest(original),
            pack_image.authentication_digest(bytes(authenticated)),
        )

    def test_invalid_state_flags_are_rejected(self) -> None:
        invalid = bytearray(self.image)
        struct.pack_into("<H", invalid, 10, 0)
        with self.assertRaisesRegex(ValueError, "state flags"):
            pack_image.validate_image(bytes(invalid), "demo")

    def test_wrong_product_is_rejected(self) -> None:
        wrong_product = bytearray(self.payload)
        info_offset = (
            pack_image.LAYOUTS["demo"]["product_info_offset"]
            - pack_image.IMAGE_HEADER_SIZE
        )
        struct.pack_into("<I", wrong_product, info_offset + 4, 0x0002)
        crc = zlib.crc32(wrong_product[info_offset:info_offset + 48])
        struct.pack_into("<I", wrong_product, info_offset + 60, crc)
        with self.assertRaisesRegex(ValueError, "ProductInfo ID"):
            pack_image.build_image(
                bytes(wrong_product),
                "demo",
                security_version=1,
                flags=pack_image.IMAGE_F_CONFIRMED,
                signature=bytes(64),
            )

    def test_stack_pointer_outside_product_ram_is_rejected(self) -> None:
        bad_vectors = bytearray(self.payload)
        struct.pack_into("<I", bad_vectors, 0, 0x20030000)
        with self.assertRaisesRegex(ValueError, "supported SRAM"):
            pack_image.build_image(
                bytes(bad_vectors),
                "demo",
                security_version=1,
                flags=pack_image.IMAGE_F_CONFIRMED,
                signature=bytes(64),
            )

    def test_image_must_fit_both_execution_and_upgrade_slots(self) -> None:
        limit = pack_image.LAYOUTS["demo"]["image_size"]
        oversized = bytearray(self.payload)
        oversized.extend(bytes(limit - len(oversized)))
        with self.assertRaisesRegex(ValueError, "installable limit"):
            pack_image.build_image(
                bytes(oversized),
                "demo",
                security_version=1,
                flags=pack_image.IMAGE_F_PENDING,
                signature=bytes(64),
            )

    def test_p256_signature_verification(self) -> None:
        private_key = 0x123456789ABCDEF
        nonce = 0x23456789ABCDEF1
        digest = pack_image.authentication_digest(
            self.image[:pack_image.IMAGE_HEADER_SIZE])
        public_point = pack_image._p256_mul(
            private_key, pack_image.P256_G)
        self.assertIsNotNone(public_point)
        assert public_point is not None
        ephemeral = pack_image._p256_mul(nonce, pack_image.P256_G)
        self.assertIsNotNone(ephemeral)
        assert ephemeral is not None
        r = ephemeral[0] % pack_image.P256_ORDER
        z = int.from_bytes(digest, "big")
        s = (pow(nonce, -1, pack_image.P256_ORDER)
             * (z + r * private_key)) % pack_image.P256_ORDER
        public_key = b"".join(
            coordinate.to_bytes(32, "big") for coordinate in public_point)
        signature = r.to_bytes(32, "big") + s.to_bytes(32, "big")

        self.assertTrue(pack_image.verify_p256_signature(
            public_key, digest, signature))
        tampered = bytearray(digest)
        tampered[0] ^= 1
        self.assertFalse(pack_image.verify_p256_signature(
            public_key, bytes(tampered), signature))


if __name__ == "__main__":
    unittest.main()
