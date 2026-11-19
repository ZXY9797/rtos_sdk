from __future__ import annotations

from pathlib import Path
import hashlib
import json
import sys
import unittest
import tempfile
from unittest import mock


SCRIPT_DIR = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(SCRIPT_DIR))

import build_3in1  # noqa: E402


class BuildThreeInOneTest(unittest.TestCase):
    def test_factory_images_must_share_board_and_canonical_layout(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            layout = build_3in1.load_layout('demo_foc')
            checksum = hashlib.sha256(layout['path'].read_bytes()).hexdigest()
            for image in ('preloader', 'loader', 'app'):
                (root / image).mkdir()
                (root / image / 'build-info.json').write_text(json.dumps({
                    'product': 'demo_foc', 'firmware': image, 'board': 'gd32_demo', 'profile': 'release',
                    'inputs': {'layout': checksum, 'board_sha256': 'c' * 64}}), encoding='utf-8')
            build_3in1.verify_image_set(root, 'demo_foc')
            path = root / 'app/build-info.json'
            info = json.loads(path.read_text(encoding='utf-8'))
            build_3in1.verify_image_set(root, 'demo_foc', required_profile='release')
            info['profile'] = 'debug'
            path.write_text(json.dumps(info), encoding='utf-8')
            with self.assertRaisesRegex(SystemExit, 'release profile for production'):
                build_3in1.verify_image_set(root, 'demo_foc', required_profile='release')
            info['board'] = 'other-board'
            path.write_text(json.dumps(info), encoding='utf-8')
            with self.assertRaisesRegex(SystemExit, 'board differs'):
                build_3in1.verify_image_set(root, 'demo_foc')
            info['board'] = 'gd32_demo'
            info['inputs']['board_sha256'] = 'd' * 64
            path.write_text(json.dumps(info), encoding='utf-8')
            with self.assertRaisesRegex(SystemExit, 'board differs'):
                build_3in1.verify_image_set(root, 'demo_foc')
            del info['inputs']['board_sha256']
            path.write_text(json.dumps(info), encoding='utf-8')
            with self.assertRaisesRegex(SystemExit, 'rebuild'):
                build_3in1.verify_image_set(root, 'demo_foc')
            info['inputs']['layout'] = 'different-layout'
            path.write_text(json.dumps(info), encoding='utf-8')
            with self.assertRaisesRegex(SystemExit, 'canonical product layout'):
                build_3in1.verify_image_set(root, 'demo_foc')

    def test_prebuilt_image_rejects_a_replaced_binary_or_changed_config(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            (root / "test.bin").write_bytes(b"image")
            (root / ".config").write_bytes(b"config")
            info = {"inputs": {"config": hashlib.sha256(b"config").hexdigest()}}
            (root / "build-info.json").write_text(json.dumps(info), encoding="utf-8")
            manifest = {"build": info, "artifacts": {"test.bin": {"sha256": hashlib.sha256(b"image").hexdigest()}}}
            (root / "test.manifest.json").write_text(json.dumps(manifest), encoding="utf-8")
            build_3in1.verify_linked_files(root, "test")
            for name, original in (("test.bin", b"image"), (".config", b"config")):
                with self.subTest(file=name):
                    (root / name).write_bytes(b"another build")
                    with self.assertRaisesRegex(SystemExit, "differ from the linked manifest"):
                        build_3in1.verify_linked_files(root, "test")
                    (root / name).write_bytes(original)

    def test_production_verifies_each_final_image_configuration(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            for image in ('preloader', 'loader', 'app'):
                (root / image).mkdir()
                policy = 'CONFIG_APP_PRODUCTION=y\nCONFIG_APP_WATCHDOG=y\n' if image == 'app' else 'CONFIG_BOOT_PRODUCTION=y\n'
                (root / image / '.config').write_text(policy, encoding='utf-8')
            build_3in1.verify_production_configs(root)
            (root / 'app/.config').write_text('CONFIG_APP_PRODUCTION=y\n', encoding='utf-8')
            with self.assertRaisesRegex(SystemExit, 'lacks watchdog'):
                build_3in1.verify_production_configs(root)
            (root / 'app/.config').write_text('# CONFIG_APP_PRODUCTION is not set\n', encoding='utf-8')
            with self.assertRaisesRegex(SystemExit, 'CONFIG_APP_PRODUCTION'):
                build_3in1.verify_production_configs(root)
    def test_user_paths_are_resolved_before_subprocesses_run(self) -> None:
        relative = Path("out/toolchains/gcc")
        self.assertEqual(relative.resolve(),
                         build_3in1.resolve_user_path(relative))

    def test_optional_user_path_remains_unset(self) -> None:
        self.assertIsNone(build_3in1.resolve_user_path(None))

    def test_cmake_uses_the_invoking_python(self) -> None:
        with mock.patch.object(build_3in1, "run") as run_mock:
            build_3in1.cmake_configure(
                Path("out/test"), "demo_foc", "app", None)

        command = run_mock.call_args.args[0]
        for stale_override in ('APPLICATION', 'BOARD', 'BOARD_ROOT', 'CONF_FILE', 'DTC_OVERLAY_FILE', 'MODULE_ROOTS'):
            self.assertIn(f'-U{stale_override}', command)
        python_arg = (
            "-DPython3_EXECUTABLE="
            f"{Path(sys.executable).absolute().as_posix()}"
        )
        python_args = [
            arg for arg in command
            if arg.startswith("-DPython3_EXECUTABLE=")
        ]
        self.assertEqual([python_arg], python_args)


if __name__ == "__main__":
    unittest.main()
