import importlib.util
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest
from types import SimpleNamespace
from unittest.mock import patch


ROOT = Path(__file__).resolve().parents[3]
CLI = ROOT / "tools/sdk.py"
spec = importlib.util.spec_from_file_location("sdk_cli", CLI)
sdk = importlib.util.module_from_spec(spec)
with patch.object(sys, "path", [str(ROOT / "tools"), *sys.path]):
    spec.loader.exec_module(sdk)


class SdkCliTest(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix="sdk cli ")
        self.addCleanup(temporary.cleanup)
        self.work = Path(temporary.name)
        self.build = self.work / "build"

    @unittest.skipUnless(shutil.which("cmake") and shutil.which("ninja"), "CMake and Ninja required")
    def test_reconfigure_replaces_image_inputs_and_combines_backend(self):
        source = self.work / "source"
        source.mkdir()
        (source / "CMakeLists.txt").write_text(
            'cmake_minimum_required(VERSION 3.20)\nproject(test LANGUAGES NONE)\n'
            f'set(SDK_ROOT "{ROOT.as_posix()}")\n'
            'set(PROJECT_NAME "${p}")\n'
            f'include("{ROOT.as_posix()}/tools/cmake/env.cmake")\n'
            'file(WRITE "${CMAKE_BINARY_DIR}/selection" '
            '"${PROJECT_DIR}\n${FIRMWARE_TYPE}\n${SDK_IMAGE_CONFIG_FILES}\n")\n',
            encoding="utf-8")
        args = SimpleNamespace(build=self.build, product="demo_foc", profile=None,
                               toolchain=None, backend="rt-thread", toolchain_path=None,
                               application=ROOT / "samples/osal_contract",
                               cmake_arg=["-DEXTRA_CONF_FILE=tests/config/demo_foc_motion.conf"])
        with patch.object(sdk, "ROOT", source):
            sdk.configure(args)
            app, image, fragments = (self.build / "selection").read_text().splitlines()
            self.assertEqual(Path(app), args.application)
            self.assertEqual(image, "app")
            fragments = [Path(item) for item in fragments.split(";")]
            self.assertEqual(fragments[-2], ROOT / "tests/config/demo_foc_motion.conf")
            self.assertEqual(fragments[-1].read_text().strip(), "CONFIG_RTOS_RT_THREAD=y")
            args.application = args.backend = None
            overrides = ("BOOT_LAYOUT", "LINKER_SCRIPT", "LOADER_PAYLOAD_FILE", "SDK_APPLICATION_ID")
            args.cmake_arg = ["-DFIRMWARE_TYPE=loader", *(f"-D{key}=previous" for key in overrides)]
            sdk.configure(args)
            self.assertEqual((self.build / "selection").read_text().splitlines()[1], "loader")
            args.cmake_arg = []
            sdk.configure(args)
            app, image, fragments = (self.build / "selection").read_text().splitlines()
            self.assertEqual(Path(app), ROOT / "app/product/demo_foc")
            self.assertEqual(image, "app")
            self.assertNotIn("sdk-backend.conf", fragments)
            self.assertNotIn("demo_foc_motion.conf", fragments)
            cache = (self.build / "CMakeCache.txt").read_text()
            for key in overrides:
                self.assertNotIn(f"{key}:UNINITIALIZED=previous", cache)

    def test_inspect_external_build_from_caller_directory(self):
        image = self.build / "sdk"
        image.mkdir(parents=True)
        info = {"product": "demo_foc", "firmware": "loader", "build_id": "current"}
        (image / "build-info.json").write_text(json.dumps(info), encoding="utf-8")
        (image / "demo_foc.resources.json").write_text('{"build_id":"previous"}')
        report = image / "demo_foc_loader.resources.json"
        report.write_text('{"build_id":"current"}')
        database = self.build / "compile_commands.json"
        database.write_text("[]")
        for kind, expected in (("build", info), ("resources", {"build_id": "current"}),
                               ("clangd", {"database": str(database),
                                           "arguments": [f"--compile-commands-dir={self.build}"]})):
            result = subprocess.run([sys.executable, str(CLI), "inspect", "-B", "build", "--kind", kind],
                                    cwd=self.work, capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(json.loads(result.stdout), expected)
        report.write_text('{"build_id":"stale"}')
        with self.assertRaisesRegex(ValueError, "earlier build"):
            sdk.inspect_build(self.build, "resources")

    def test_conflicting_product_is_rejected_before_configuring(self):
        for arguments in (["--cmake-arg=-Dp:STRING=gimbal"],
                          ["--cmake-arg=-D", "--cmake-arg=p=gimbal"]):
            with self.subTest(arguments=arguments):
                result = subprocess.run([sys.executable, str(CLI), "configure", "-p", "demo_foc",
                                         "-B", str(self.build), *arguments], capture_output=True, text=True)
                self.assertNotEqual(result.returncode, 0)
                self.assertIn("--product", result.stderr)
                self.assertFalse(self.build.exists())


if __name__ == "__main__":
    unittest.main()
