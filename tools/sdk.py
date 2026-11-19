#!/usr/bin/env python3
"""SDK entry point; orchestrates CMake/CTest without replacing either."""
from __future__ import annotations
import argparse
import json
from pathlib import Path
import subprocess
import sys

from scripts.sdk_image import image_cache_reset_args

ROOT = Path(__file__).resolve().parents[1]


def run(command):
    print(" ".join(map(str, command)), flush=True)
    subprocess.run(list(map(str, command)), cwd=ROOT, check=True, stderr=subprocess.STDOUT)


def configure(args):
    command = ["cmake", "-S", ROOT, "-B", args.build, "-G", "Ninja", *image_cache_reset_args(),
               f"-Dp={args.product}", f"-DPROFILE={args.profile or 'default'}",
               f"-Dt={args.toolchain or 'armgcc9'}"]
    selectors = {"p": "--product", "PROFILE": "--profile" if args.profile else None,
                 "t": "--toolchain" if args.toolchain else None,
                 "APPLICATION": "--application" if args.application else None,
                 "SDK_RTOS_BACKEND": "--backend" if args.backend else None}
    cmake_args = iter(args.cmake_arg)
    for argument in cmake_args:
        if not argument.startswith("-D"):
            continue
        definition = next(cmake_args, "") if argument == "-D" else argument[2:]
        key = definition.split("=", 1)[0].split(":", 1)[0]
        if selectors.get(key):
            raise ValueError(f"Use {selectors[key]} to set {key} once")
    command.extend(args.cmake_arg)
    if args.application:
        command.append(f"-DAPPLICATION={args.application.resolve().as_posix()}")
    if args.backend:
        command.append(f"-DSDK_RTOS_BACKEND={args.backend}")
    if args.toolchain_path:
        command.append(f"-DARMGCC9_TOOLCHAIN_PATH={args.toolchain_path.resolve().as_posix()}")
    run(command)


def tests(args):
    from scripts.sdk_tests import execute
    raise SystemExit(execute(args))


def inspect_build(build, kind):
    # find_package(RtosSdk) builds the firmware under <application-build>/sdk.
    image = build if (build / "build-info.json").is_file() else build / "sdk"
    metadata = image / "build-info.json"
    if not metadata.is_file():
        raise ValueError(f"No SDK build-info.json in {build} or {build / 'sdk'}. Configure the image first.")
    info = json.loads(metadata.read_text(encoding="utf-8"))
    if kind == "build":
        return info
    if kind == "clangd":
        directory = image
        if image.name == "sdk" and not (image / "CMakeCache.txt").is_file():
            directory = image.parent
        for directory in (directory, image):
            database = directory / "compile_commands.json"
            if database.is_file():
                return {"database": str(database), "arguments": [f"--compile-commands-dir={directory}"]}
        raise ValueError(f"No compilation database for {image}. Enable CMAKE_EXPORT_COMPILE_COMMANDS and reconfigure.")
    if kind == "devices":
        path = image / "include/generated/devices.json"
    else:
        name = info["product"]
        if info["firmware"] != "app":
            name += "_" + info["firmware"]
        path = image / f"{name}.resources.json"
    if not path.is_file():
        raise ValueError(f"Missing {path}. {'Build' if kind == 'resources' else 'Configure'} the selected image first.")
    report = json.loads(path.read_text(encoding="utf-8"))
    if kind == "resources" and report.get("build_id") != info["build_id"]:
        raise ValueError(f"{path} belongs to an earlier build. Build the selected image again.")
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    config = commands.add_parser("configure", help="Configure a product image with CMake")
    config.add_argument("-p", "--product", required=True)
    config.add_argument("-B", "--build", type=Path, required=True)
    config.add_argument("--profile", help="image profile (default: default)")
    config.add_argument("--toolchain", help="CMake toolchain (default: armgcc9)")
    config.add_argument("--toolchain-path", type=Path)
    config.add_argument("--backend", choices=["freertos", "rt-thread", "baremetal"],
                        help="select the final OSAL backend (default: image Kconfig)")
    config.add_argument("--application", type=Path)
    config.add_argument("--cmake-arg", action="append", default=[])
    build = commands.add_parser("build", help="Build an existing CMake configuration")
    build.add_argument("-B", "--build", type=Path, required=True)
    test = commands.add_parser("test", help="Run host, firmware or integration checks")
    test.add_argument("--suite", choices=["quick", "host", "contracts", "firmware", "all"], default="host")
    select = test.add_mutually_exclusive_group()
    select.add_argument("--label", help="test domain or firmware capability (see --list)")
    select.add_argument("--case", help="exact CTest name, Python unittest ID, or matrix case")
    test.add_argument("--generator", default="Ninja", help="CMake generator for host tests")
    test.add_argument("--list", action="store_true", help="show cases and labels without running them")
    test.add_argument("--toolchain-path", type=Path)
    test.add_argument("-B", "--build", type=Path, default=ROOT / "out/sdk-tests")
    inspect = commands.add_parser("inspect", help="Read image metadata, devices, resources or clangd settings")
    inspect.add_argument("-B", "--build", type=Path, required=True)
    inspect.add_argument("--kind", choices=["build", "devices", "resources", "clangd"], default="build")
    from scripts import osal_acceptance
    osal_acceptance.add_arguments(commands.add_parser("hil", help="Run the OSAL serial acceptance check"))
    args = parser.parse_args()
    if args.command == "hil":
        return osal_acceptance.run(args)
    args.build = args.build.resolve()
    try:
        if args.command == "configure":
            configure(args)
        elif args.command == "build":
            run(["cmake", "--build", args.build, "--parallel", "8"])
        elif args.command == "test":
            tests(args)
        else:
            print(json.dumps(inspect_build(args.build, args.kind), indent=2, ensure_ascii=False))
    except (OSError, ValueError) as error:
        parser.exit(1, f"sdk: {error}\n")
    except subprocess.CalledProcessError as error:
        return error.returncode


if __name__ == "__main__":
    sys.exit(main())
