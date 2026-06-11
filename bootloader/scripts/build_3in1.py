#!/usr/bin/env python3
"""Configure, build, and merge a product three-image firmware."""

from __future__ import annotations

import argparse
from pathlib import Path
import subprocess
import sys


ROOT = Path(__file__).resolve().parents[2]
DEFAULT_GCC9 = Path("D:/code/mcu_prebuilt/win_x64/xpack-arm-none-eabi-gcc-9.3.1-1.4")


def run(cmd: list[str], cwd: Path = ROOT) -> None:
    print("+ " + " ".join(cmd), flush=True)
    subprocess.run(cmd, cwd=cwd, check=True)


def cmake_configure(
    build_dir: Path,
    product: str,
    firmware_type: str,
    toolchain_path: Path | None,
) -> None:
    cmd = [
        "cmake",
        "-S",
        str(ROOT),
        "-B",
        str(build_dir),
        "-GNinja",
        "-ULINKER_SCRIPT",
        "-UCMAKE_EXE_LINKER_FLAGS",
        "-UBOOT_LAYOUT",
        "-UCONFIG_DIR",
        f"-Dp={product}",
        f"-DFIRMWARE_TYPE={firmware_type}",
    ]
    if toolchain_path:
        cmd.append(f"-DARMGCC9_TOOLCHAIN_PATH={toolchain_path.as_posix()}")
    run(cmd)


def ninja_build(build_dir: Path) -> None:
    run(["ninja", "-C", str(build_dir)])


def firmware_output_name(product: str, firmware_type: str) -> str:
    if firmware_type == "app":
        return product
    return f"{product}_{firmware_type}"


def build_firmware(
    out_root: Path,
    product: str,
    firmware_type: str,
    toolchain_path: Path | None,
) -> Path:
    build_dir = out_root / firmware_type
    cmake_configure(build_dir, product, firmware_type, toolchain_path)
    ninja_build(build_dir)
    return build_dir / f"{firmware_output_name(product, firmware_type)}.bin"


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("project", choices=("demo", "demo_ble"))
    parser.add_argument(
        "--out",
        type=Path,
        help="single output root; defaults to out/<project>_3in1",
    )
    parser.add_argument(
        "--toolchain-path",
        type=Path,
        default=DEFAULT_GCC9 if DEFAULT_GCC9.exists() else None,
        help="ARM GCC 9 root path, not the bin directory",
    )
    return parser.parse_args()


def main() -> None:
    args = parse_args()
    out_root = (args.out or ROOT / "out" / f"{args.project}_3in1").resolve()
    out_root.mkdir(parents=True, exist_ok=True)

    preloader_bin = build_firmware(
        out_root,
        args.project,
        "preloader",
        args.toolchain_path,
    )
    loader_bin = build_firmware(
        out_root,
        args.project,
        "loader",
        args.toolchain_path,
    )
    app_bin = build_firmware(out_root, args.project, "app", args.toolchain_path)

    output = out_root / f"{args.project}_3in1.bin"
    run(
        [
            sys.executable,
            str(ROOT / "bootloader" / "scripts" / "merge_firmware.py"),
            "--layout",
            args.project,
            "--preloader",
            str(preloader_bin),
            "--loader",
            str(loader_bin),
            "--app",
            str(app_bin),
            "--output",
            str(output),
        ]
    )
    print(f"three-image firmware: {output}", flush=True)


if __name__ == "__main__":
    main()
