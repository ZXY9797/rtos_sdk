from __future__ import annotations

from pathlib import Path
import sys
import unittest


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


if __name__ == "__main__":
    unittest.main()
