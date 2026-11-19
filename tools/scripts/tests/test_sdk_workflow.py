import contextlib
import io
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch
import xml.etree.ElementTree as ET

TOOLS = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(TOOLS))
from scripts import sdk_tests


class WorkflowTest(unittest.TestCase):
    def test_reports_only_executed_tests_and_counts_failed_subtests_once(self):
        with tempfile.TemporaryDirectory() as temp:
            directory = Path(temp)
            directory.joinpath("test_reporting_fixture.py").write_text(
                "import unittest\n"
                "class SetupError(unittest.TestCase):\n"
                "    @classmethod\n"
                "    def setUpClass(cls): raise ValueError('class setup failed')\n"
                "    def test_unreached(self): pass\n"
                "class Completed(unittest.TestCase):\n"
                "    def test_pass(self): pass\n"
                "    @unittest.skip('missing simulated device')\n"
                "    def test_skip(self): pass\n"
                "    def test_subcases(self):\n"
                "        for n in range(2):\n"
                "            with self.subTest(n=n): self.fail('failed subcase')\n",
                encoding="utf-8")
            results = {}
            with contextlib.redirect_stderr(io.StringIO()):
                sdk_tests.python_tests(directory, results, strict=True)
            self.addCleanup(sys.modules.pop, "test_reporting_fixture", None)
        prefix = "test_reporting_fixture.Completed."
        self.assertEqual(len(results), 4)
        self.assertEqual(results[prefix + "test_pass"]["status"], "passed")
        self.assertTrue(results[prefix + "test_skip"]["required"])
        self.assertEqual(results[prefix + "test_skip"]["status"], "skipped")
        self.assertEqual(results[prefix + "test_subcases"]["status"], "failed")
        self.assertEqual(results["setUpClass (test_reporting_fixture.SetupError)"]["status"], "failed")

    def test_ctest_320_dashboard_preserves_failure_and_exit_status(self):
        with tempfile.TemporaryDirectory() as temp:
            source = Path(temp)
            build = source / "build"
            source.joinpath("CMakeLists.txt").write_text(
                "cmake_minimum_required(VERSION 3.20)\nproject(t NONE)\n"
                "enable_testing()\nadd_test(NAME passes COMMAND ${CMAKE_COMMAND} -E true)\n"
                "add_test(NAME fails COMMAND ${CMAKE_COMMAND} -E false)\n", encoding="utf-8")
            subprocess.run(["cmake", "-S", source, "-B", build, "-G", "Ninja"], check=True,
                           capture_output=True)
            report = build / "ctest.xml"
            def capture_ctest(*argv, check):
                return subprocess.run(argv, check=check, capture_output=True)
            with patch.object(sdk_tests, "command", side_effect=capture_ctest):
                run = sdk_tests.legacy_ctest(["ctest", "--test-dir", build], build, report)
            self.assertNotEqual(run.returncode, 0)
            result = ET.parse(report)
            self.assertIsNone(result.find(".//testcase[@name='passes']/failure"))
            self.assertIsNotNone(result.find(".//testcase[@name='passes']"))
            self.assertIsNotNone(result.find(".//testcase[@name='fails']/failure"))
