from __future__ import annotations

from pathlib import Path
import sys
import unittest
from unittest import mock


SCRIPT_DIR = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(SCRIPT_DIR))

import build_3in1  # noqa: E402


class BuildThreeInOneTest(unittest.TestCase):
    def test_user_paths_are_resolved_before_subprocesses_run(self) -> None:
        relative = Path("out/toolchains/gcc")
        self.assertEqual(relative.resolve(),
                         build_3in1.resolve_user_path(relative))

    def test_optional_user_path_remains_unset(self) -> None:
        self.assertIsNone(build_3in1.resolve_user_path(None))

    def test_cmake_uses_the_invoking_python(self) -> None:
        with mock.patch.object(build_3in1, "run") as run_mock:
            build_3in1.cmake_configure(
                Path("out/test"), "demo", "app", None)

        command = run_mock.call_args.args[0]
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
