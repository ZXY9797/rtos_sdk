from __future__ import annotations

from pathlib import Path
import sys
import tempfile
import unittest


SCRIPT_DIR = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(SCRIPT_DIR))

from check_image_size import validate_image_size  # noqa: E402


class CheckImageSizeTest(unittest.TestCase):
    def test_image_at_partition_limit_is_accepted(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            image = Path(tmp) / "loader.bin"
            image.write_bytes(bytes(16))
            self.assertEqual(16, validate_image_size(image, 16, "loader"))

    def test_oversize_image_is_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            image = Path(tmp) / "loader.bin"
            image.write_bytes(bytes(17))
            with self.assertRaisesRegex(ValueError, "exceeds"):
                validate_image_size(image, 16, "loader")

    def test_missing_image_is_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            image = Path(tmp) / "missing.bin"
            with self.assertRaisesRegex(ValueError, "not found"):
                validate_image_size(image, 16, "loader")


if __name__ == "__main__":
    unittest.main()
