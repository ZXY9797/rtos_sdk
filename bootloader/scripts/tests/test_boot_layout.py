from __future__ import annotations

import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest import mock


SCRIPT_DIR = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(SCRIPT_DIR))

import boot_layout  # noqa: E402


class BootLayoutTest(unittest.TestCase):
    def test_repository_layouts_validate_and_emit_cmake(self) -> None:
        self.assertEqual(("demo", "demo_ble", "gimbal"),
                         boot_layout.canonical_layout_names())
        for product in boot_layout.canonical_layout_names():
            layout = boot_layout.load_layout(product)
            with tempfile.TemporaryDirectory() as tmp:
                output = Path(tmp) / "layout.cmake"
                boot_layout.emit_cmake(layout, output)
                text = output.read_text(encoding="utf-8")
                self.assertIn('set(BOOT_FLASH_BASE "0x', text)
                self.assertIn('set(BOOT_SLOT0_SIZE "0x', text)

    def test_alias_resolves_to_canonical_layout(self) -> None:
        self.assertEqual("demo", boot_layout.load_layout("gd32f503")["name"])
        self.assertEqual("demo_ble",
                         boot_layout.load_layout("gr5525")["name"])
        self.assertEqual(
            "gimbal",
            boot_layout.load_layout("gd32f503-gimbal")["name"])

    def test_scratch_is_carved_from_storage_tail(self) -> None:
        expected_storage_offsets = {
            "demo": 0x00058000,
            "demo_ble": 0x000F8000,
            "gimbal": 0x00058000,
        }
        for product, expected_offset in expected_storage_offsets.items():
            layout = boot_layout.load_layout(product)
            partitions = layout["partitions"]
            self.assertEqual(expected_offset,
                             partitions["storage"]["offset"])
            self.assertEqual(
                partitions["storage"]["offset"]
                + partitions["storage"]["size"],
                partitions["scratch"]["offset"])
            self.assertEqual(layout["erase_block_size"],
                             partitions["scratch"]["size"])

    def test_misaligned_partition_is_rejected(self) -> None:
        raw = json.loads(
            (boot_layout.PRODUCT_ROOT / "demo" / "layout.json")
            .read_text(encoding="utf-8"))
        raw["partitions"]["slot0"]["size"] = "0x00028001"
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            product_dir = root / "broken"
            product_dir.mkdir()
            (product_dir / "layout.json").write_text(
                json.dumps(raw), encoding="utf-8")
            with mock.patch.object(boot_layout, "PRODUCT_ROOT", root):
                with self.assertRaisesRegex(ValueError, "not contiguous"):
                    boot_layout.load_layout("broken")

    def test_invalid_flash_geometry_is_rejected(self) -> None:
        raw = json.loads(
            (boot_layout.PRODUCT_ROOT / "demo" / "layout.json")
            .read_text(encoding="utf-8"))
        raw["write_block_size"] = "0x00000003"
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            product_dir = root / "broken"
            product_dir.mkdir()
            (product_dir / "layout.json").write_text(
                json.dumps(raw), encoding="utf-8")
            with mock.patch.object(boot_layout, "PRODUCT_ROOT", root):
                with self.assertRaisesRegex(ValueError, "geometry"):
                    boot_layout.load_layout("broken")

    def test_image_header_must_follow_flash_write_alignment(self) -> None:
        raw = json.loads(
            (boot_layout.PRODUCT_ROOT / "demo" / "layout.json")
            .read_text(encoding="utf-8"))
        raw["write_block_size"] = "0x00000100"
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            product_dir = root / "broken"
            product_dir.mkdir()
            (product_dir / "layout.json").write_text(
                json.dumps(raw), encoding="utf-8")
            with mock.patch.object(boot_layout, "PRODUCT_ROOT", root):
                with self.assertRaisesRegex(ValueError, "header size"):
                    boot_layout.load_layout("broken")


if __name__ == "__main__":
    unittest.main()
