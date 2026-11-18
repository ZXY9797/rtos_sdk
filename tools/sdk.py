#!/usr/bin/env python3
"""SDK entry point; orchestrates CMake/CTest without replacing either."""
from __future__ import annotations
import argparse
import json
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]


def run(command):
    print(" ".join(map(str, command)), flush=True)
    subprocess.run(list(map(str, command)), cwd=ROOT, check=True, stderr=subprocess.STDOUT)


def configure(args):
    command = ["cmake", "-S", ROOT, "-B", args.build, "-G", "Ninja", f"-Dp={args.product}",
               f"-DPROFILE={args.profile}", f"-Dt={args.toolchain}", "-UBOARD", "-UBOARD_ROOT"]
    if args.application: command.append(f"-DAPPLICATION={args.application.resolve()}")
    if args.backend == "rt-thread": command.append("-DEXTRA_CONF_FILE=tests/config/rtthread.conf")
    else: command.append("-UEXTRA_CONF_FILE")
    command.extend(args.cmake_arg)
    if args.toolchain_path: command.append(f"-DARMGCC9_TOOLCHAIN_PATH={args.toolchain_path.resolve().as_posix()}")
    run(command)


def tests(args):
    if args.suite in ("all", "host"):
        host = args.build / "host"
        run(["cmake", "-S", ROOT / "tests", "-B", host, "-G", "Ninja"])
        run(["cmake", "--build", host, "--parallel", "8"])
        run(["ctest", "--test-dir", host, "--output-on-failure", "--no-tests=error"])
        for directory in ("tools/scripts/tests", "bootloader/scripts/tests"):
            run([sys.executable, "-m", "unittest", "discover", "-s", directory, "-p", "test_*.py"])
    if args.suite in ("all", "firmware"):
        manifest = json.loads((ROOT / "tools/test_matrix.json").read_text(encoding="utf-8"))
        for case in manifest["firmware"]:
            build = args.build / case["name"]
            command = ["cmake", "-S", ROOT, "-B", build, "-G", "Ninja", *case["arguments"]]
            if args.toolchain_path: command.append(f"-DARMGCC9_TOOLCHAIN_PATH={args.toolchain_path.resolve().as_posix()}")
            run(command)
            run(["cmake", "--build", build, "--parallel", "8"])
        print("Firmware configurations built; no target runtime claim.", flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    config = commands.add_parser("configure")
    config.add_argument("-p", "--product", required=True)
    config.add_argument("-B", "--build", type=Path, required=True)
    config.add_argument("--profile", default="default")
    config.add_argument("--toolchain", default="armgcc9")
    config.add_argument("--toolchain-path", type=Path)
    config.add_argument("--backend", choices=["freertos", "rt-thread"], default="freertos")
    config.add_argument("--application", type=Path)
    config.add_argument("--cmake-arg", action="append", default=[])
    build = commands.add_parser("build")
    build.add_argument("-B", "--build", type=Path, required=True)
    test = commands.add_parser("test")
    test.add_argument("--suite", choices=["host", "firmware", "all"], default="host")
    test.add_argument("--toolchain-path", type=Path)
    test.add_argument("-B", "--build", type=Path, default=ROOT / "out/sdk-tests")
    inspect = commands.add_parser("inspect")
    inspect.add_argument("-B", "--build", type=Path, required=True)
    inspect.add_argument("--kind", choices=["build", "devices", "resources"], default="build")
    args = parser.parse_args()
    if args.command == "configure": configure(args)
    elif args.command == "build": run(["cmake", "--build", args.build, "--parallel", "8"])
    elif args.command == "test": tests(args)
    else:
        path = args.build / "build-info.json"
        if args.kind == "devices": path = args.build / "include/generated/devices.json"
        if args.kind == "resources":
            matches = list(args.build.glob("*.resources.json"))
            if len(matches) != 1: parser.error("Select a build containing exactly one resource report")
            path = matches[0]
        print(json.dumps(json.loads(path.read_text(encoding="utf-8")), indent=2, ensure_ascii=False))


if __name__ == "__main__":
    main()
