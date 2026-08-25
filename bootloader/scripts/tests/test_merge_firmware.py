from __future__ import annotations

import argparse
from pathlib import Path
import sys
import tempfile
import unittest
from unittest import mock


SCRIPT_DIR = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(SCRIPT_DIR))

import merge_firmware  # noqa: E402


class MergeFirmwareTest(unittest.TestCase):
    def _arguments(self, root: Path, app: bytes) -> argparse.Namespace:
        preloader = root / "preloader.bin"
        loader = root / "loader.bin"
        application = root / "app.img"
        preloader.write_bytes(b"preloader")
        loader.write_bytes(b"loader")
        application.write_bytes(app)
        return argparse.Namespace(
            layout="demo",
            preloader=str(preloader),
            loader=str(loader),
            app=str(application),
            output=str(root / "factory.bin"),
        )

    def test_app_limit_is_installable_but_output_covers_physical_slot(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            args = self._arguments(root, b"packaged-app")
            with mock.patch.object(merge_firmware, "validate_image"):
                merge_firmware.merge(args)

            layout = merge_firmware.LAYOUTS["demo"]
            app = layout["partitions"]["app"]
            image = Path(args.output).read_bytes()
            self.assertLess(app["max_image_size"], app["size"])
            self.assertEqual(app["offset"] + app["size"], len(image))
            self.assertEqual(b"packaged-app",
                             image[app["offset"]:app["offset"] + 12])
            self.assertEqual(0xFF, image[-1])

    def test_app_larger_than_upgrade_slot_is_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            app = merge_firmware.LAYOUTS["demo"]["partitions"]["app"]
            args = self._arguments(root, bytes(app["max_image_size"] + 1))
            with mock.patch.object(merge_firmware, "validate_image"):
                with self.assertRaisesRegex(SystemExit, "installable size"):
                    merge_firmware.merge(args)


if __name__ == "__main__":
    unittest.main()
