#!/usr/bin/env python3

import hashlib
import struct
import sys
import unittest
from pathlib import Path


SCRIPT_DIR = Path(__file__).resolve().parents[1]
if str(SCRIPT_DIR) not in sys.path:
    sys.path.insert(0, str(SCRIPT_DIR))

from pack_loader_payload import (  # noqa: E402
    HEADER,
    MAGIC,
    VERSION,
    build_blob,
    render_source,
)


class LoaderPayloadTest(unittest.TestCase):
    def test_manifest_contains_exact_size_and_digest(self):
        payload = bytes(range(64))
        blob = build_blob(payload)
        magic, header_size, version, payload_size, digest, reserved = (
            HEADER.unpack_from(blob))
        self.assertEqual(magic, MAGIC)
        self.assertEqual(header_size, HEADER.size)
        self.assertEqual(version, VERSION)
        self.assertEqual(payload_size, len(payload))
        self.assertEqual(digest, hashlib.sha256(payload).digest())
        self.assertEqual(reserved, 0)
        self.assertEqual(blob[HEADER.size:], payload)

    def test_empty_loader_is_rejected(self):
        with self.assertRaisesRegex(ValueError, 'empty'):
            build_blob(b'')

    def test_generated_source_exports_payload_symbols(self):
        source = render_source(build_blob(b'loader'))
        self.assertIn('section(".loader_payload")', source)
        self.assertIn('g_loader_payload_size', source)
        values = struct.pack('<I', MAGIC)
        for value in values:
            self.assertIn(f'0x{value:02X}U', source)


if __name__ == '__main__':
    unittest.main()
