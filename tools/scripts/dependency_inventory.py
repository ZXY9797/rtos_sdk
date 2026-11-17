#!/usr/bin/env python3
"""Verify vendored source digests without guessing missing upstream revisions."""
from __future__ import annotations
import argparse
import hashlib
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SOURCES = [
    ("FreeRTOS-Kernel", "embedded/osal/rtos/freertos", "11.3.0", "https://github.com/FreeRTOS/FreeRTOS-Kernel"),
    ("RT-Thread", "embedded/osal/rtos/rt-thread", "4.1.1", "https://github.com/RT-Thread/rt-thread"),
    ("CMSIS", "embedded/modules/cmsis/CMSIS", None, "https://github.com/ARM-software/CMSIS_6"),
    ("python-devicetree", "tools/scripts/dts/python-devicetree", None, "https://github.com/zephyrproject-rtos/zephyr"),
    ("Kconfiglib", "tools/scripts/kconfig", None, "https://github.com/ulfalizer/Kconfiglib"),
    ("GD32 backend sources", "embedded/soc/gd/gd32f50x", None, "https://www.gigadevice.com/"),
    ("STM32Cube", "embedded/soc/st/stm32cube", None, "https://github.com/STMicroelectronics"),
    ("Goodix backend sources", "embedded/soc/goodix/gr5525x", None, "https://www.goodix.com/"),
    ("Goodix BLE libraries", "component/ble/goodix/libraries", None, "https://github.com/goodix-ble/GR5525.SDK"),
    ("Goodix BLE profiles", "component/ble/goodix/profiles", None, "https://github.com/goodix-ble/GR5525.SDK"),
]


def tree_digest(root: Path) -> str:
    digest = hashlib.sha256()
    for path in sorted(root.rglob("*"), key=lambda p: p.relative_to(root).as_posix()):
        if not path.is_file() or any(x in path.parts for x in ("__pycache__", ".git")):
            continue
        data = path.read_bytes()
        if b"\0" not in data[:4096]: data = data.replace(b"\r\n", b"\n")
        digest.update(path.relative_to(root).as_posix().encode() + b"\0")
        digest.update(hashlib.sha256(data).digest())
    return digest.hexdigest()


def inventory() -> dict:
    result = {"schema": "rtos-sdk.dependencies.v1", "normalization": "UTF-8 path order; CRLF normalized for non-binary files",
            "dependencies": [{"name": name, "path": path, "version": version,
                              "upstream": url, "upstream_revision": None,
                              "revision_status": "not recorded in original vendor import",
                              "content_sha256": tree_digest(ROOT / path)}
                             for name, path, version, url in SOURCES],
            "adaptation_boundaries": ["embedded/osal/freertos", "embedded/osal/rt-thread",
                                       "tools/cmake/modules/upstream-helpers.json"],
            "toolchain": {"name": "xPack ARM GCC", "version": "9.3.1-1.4",
                          "linux_archive_sha256": "720dd38a6abadc0286f2b3c8dbc7239c7dbfdd64bc052345d149657e5478b914"}}
    result['source_policy_sha256'] = hashlib.sha256((ROOT / 'tools/source-policy.json').read_bytes()).hexdigest()
    result['python_requirements'] = {
        'path': 'tools/requirements-ci.txt',
        'sha256': hashlib.sha256((ROOT / 'tools/requirements-ci.txt').read_bytes()).hexdigest()}
    for item in result['dependencies']:
        if item['name'] == 'FreeRTOS-Kernel':
            item['upstream_revision'] = '9b777ae5c5b8e9e456065a00294d1e5f5f9facf5'
            item['revision_status'] = 'verified clean nested vendor checkout; original package restored from verified backups'
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--write", action="store_true")
    args = parser.parse_args()
    path = ROOT / "tools/dependencies.json"
    current = inventory()
    if args.write:
        path.write_text(json.dumps(current, indent=2) + "\n", encoding="utf-8")
    elif json.loads(path.read_text(encoding="utf-8")) != current:
        raise SystemExit("Vendor source digest changed: review the import and regenerate the inventory explicitly")
    print("Vendored dependency inventory verified")


if __name__ == "__main__": main()
