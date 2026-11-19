import hashlib
import io
import json
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from osal_acceptance import capture, expected_run


class ChunkedPort(io.BytesIO):
    in_waiting = 7


class OsalAcceptanceTest(unittest.TestCase):
    def test_uart_requires_a_complete_matching_record(self):
        marker, build_id = "OSAL_CONTRACT_PASS", "ab" * 32
        record = f"{marker} {build_id}\r\n".encode()
        data = b"boot\n" + b"x" * 600 + record + record
        log = io.BytesIO()
        capture(ChunkedPort(data), log, marker, build_id, 1)
        self.assertEqual(log.getvalue(), data)
        with self.assertRaisesRegex(ValueError, "different build"):
            capture(ChunkedPort(record), io.BytesIO(), marker, "cd" * 32, 1)
        with self.assertRaises(TimeoutError):
            capture(ChunkedPort(record.rstrip()), io.BytesIO(), marker, build_id, 0.01)

    def test_backend_comes_from_the_linked_config(self):
        with tempfile.TemporaryDirectory() as tmp:
            build = Path(tmp)
            for backend, name in (("FREERTOS", "freertos"), ("RT_THREAD", "rt-thread")):
                config = f"CONFIG_RTOS_{backend}=y\n".encode()
                (build / ".config").write_bytes(config)
                info = {"application_path": "SDK_ROOT/samples/osal_contract",
                        "product": "demo_foc", "firmware": "app", "build_id": "ac" * 32,
                        "inputs": {"config": hashlib.sha256(config).hexdigest()}}
                (build / "build-info.json").write_text(json.dumps(info), encoding="utf-8")
                (build / "demo_foc.manifest.json").write_text(json.dumps({"build": info}), encoding="utf-8")
                self.assertEqual(expected_run(build), (info["build_id"], name, "OSAL_CONTRACT_PASS"))
                (build / ".config").write_bytes(config + b"CONFIG_EXTRA=y\n")
                with self.assertRaisesRegex(ValueError, "rebuild"):
                    expected_run(build)
