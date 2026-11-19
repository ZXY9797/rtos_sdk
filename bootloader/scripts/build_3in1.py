#!/usr/bin/env python3
"""Configure, build, and merge a product three-image firmware."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

from boot_layout import canonical_layout_names, load_layout


ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
from scripts.sdk_image import image_cache_reset_args

STAGES = ("preloader", "loader", "app")
CI_GCC9 = ROOT / "out/toolchains/gcc-9.3.1/xpack-arm-none-eabi-gcc-9.3.1-1.4"
DEFAULT_GCC9 = CI_GCC9 if CI_GCC9.is_dir() else None


def resolve_user_path(path: Path | None) -> Path | None:
    """Resolve a CLI path before subprocesses change their working directory."""
    if path is None:
        return None
    return path.expanduser().resolve()


def run(cmd: list[str], cwd: Path = ROOT) -> None:
    print("+ " + " ".join(cmd), flush=True)
    subprocess.run(cmd, cwd=cwd, check=True)


def cmake_configure(
    build_dir: Path,
    product: str,
    firmware_type: str,
    toolchain_path: Path | None,
    extra_conf_file: Path | None = None,
    profile: str = "default",
) -> None:
    cmd = [
        "cmake",
        "-S",
        str(ROOT),
        "-B",
        str(build_dir),
        "-GNinja",
        *image_cache_reset_args(),
        f"-Dp={product}",
        f"-DFIRMWARE_TYPE={firmware_type}",
        f"-DPROFILE={profile}",
        ("-DPython3_EXECUTABLE="
         f"{Path(sys.executable).absolute().as_posix()}"),
    ]
    if toolchain_path:
        cmd.append(f"-DARMGCC9_TOOLCHAIN_PATH={toolchain_path.as_posix()}")
    if extra_conf_file:
        cmd.append(f"-DEXTRA_CONF_FILE={extra_conf_file.as_posix()}")
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
    extra_conf_file: Path | None = None,
    profile: str = "default",
) -> Path:
    build_dir = out_root / firmware_type
    cmake_configure(
        build_dir, product, firmware_type, toolchain_path, extra_conf_file, profile)
    ninja_build(build_dir)
    return build_dir / f"{firmware_output_name(product, firmware_type)}.bin"


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("project", choices=canonical_layout_names())
    for stage in STAGES:
        parser.add_argument(f"--{stage}-dir", type=Path,
                            help=f"use the existing {stage} build (supply all three directories)")
    parser.add_argument(
        "--out",
        type=Path,
        help="single output root; defaults to out/<project>_3in1",
    )
    parser.add_argument(
        "--toolchain-path",
        type=Path,
        default=DEFAULT_GCC9,
        help="ARM GCC 9 root path, not the bin directory",
    )
    parser.add_argument(
        "--security-version",
        type=int,
        default=0,
        help="monotonic application anti-rollback version",
    )
    parser.add_argument(
        "--signature",
        type=Path,
        help="optional 64-byte raw ECDSA-P256 application signature",
    )
    parser.add_argument(
        "--public-key",
        type=Path,
        help="64-byte raw P-256 x||y key used for offline verification",
    )
    parser.add_argument(
        "--production",
        action="store_true",
        help="require production boot policy and the application release profile",
    )
    return parser.parse_args()


def stage_dirs(out_root: Path) -> dict[str, Path]:
    return {stage: out_root / stage for stage in STAGES}


def verify_production_configs(out_root: Path) -> None:
    required = {"preloader": "CONFIG_BOOT_PRODUCTION=y", "loader": "CONFIG_BOOT_PRODUCTION=y",
                "app": "CONFIG_APP_PRODUCTION=y"}
    for image, policy in required.items():
        config = (out_root / image / ".config").read_text(encoding="utf-8")
        if policy not in config.splitlines():
            raise SystemExit(f"error: {image} final configuration lacks {policy}")
    app_config = (out_root / "app/.config").read_text(encoding="utf-8")
    if "CONFIG_APP_WATCHDOG=y" not in app_config.splitlines():
        raise SystemExit("error: production application lacks watchdog")


def verify_image_set(out_root: Path, product: str, *, required_profile: str | None = None) -> None:
    layout = load_layout(product)
    expected_layout = hashlib.sha256(layout['path'].read_bytes()).hexdigest()
    board = None
    for image in STAGES:
        info = json.loads((out_root / image / 'build-info.json').read_text(encoding='utf-8'))
        if info.get('product') != product or info.get('firmware') != image:
            raise SystemExit(f'error: {image} build identity differs from the requested product/image')
        if required_profile and info.get('profile') != required_profile:
            raise SystemExit(f'error: {image} requires the {required_profile} profile for production')
        if info.get('inputs', {}).get('layout') != expected_layout:
            raise SystemExit(f'error: {image} does not use the canonical product layout')
        hardware = info['inputs'].get('board_sha256')
        if not hardware:
            raise SystemExit(f'error: {image} lacks a base board fingerprint; rebuild this image')
        selected_board = (info.get('board'), hardware)
        if not selected_board[0] or (board is not None and selected_board != board):
            raise SystemExit(f'error: {image} board differs from the rest of the image set')
        board = selected_board


def verify_linked_files(directory: Path, name: str) -> None:
    manifest = json.loads((directory / f"{name}.manifest.json").read_text(encoding="utf-8"))
    metadata = json.loads((directory / "build-info.json").read_text(encoding="utf-8"))
    binary = directory / f"{name}.bin"
    if (metadata != manifest["build"]
            or hashlib.sha256(binary.read_bytes()).hexdigest() != manifest["artifacts"][binary.name]["sha256"]
            or hashlib.sha256((directory / ".config").read_bytes()).hexdigest() != metadata["inputs"]["config"]):
        raise SystemExit(f"error: {name} inputs differ from the linked manifest; rebuild this image")


def main() -> None:
    args = parse_args()
    toolchain_path = resolve_user_path(args.toolchain_path)
    signature_path = resolve_user_path(args.signature)
    public_key_path = resolve_user_path(args.public_key)

    if args.production and (signature_path is None or public_key_path is None):
        raise SystemExit(
            "error: --production requires --signature and --public-key")
    if args.production and args.security_version == 0:
        raise SystemExit("error: --production requires a non-zero security version")
    if args.production:
        try:
            signature = signature_path.read_bytes()
        except OSError as exc:
            raise SystemExit(f"error: cannot read signature: {exc}") from exc
        if len(signature) != 64 or not any(signature):
            raise SystemExit(
                "error: production signature must be a non-zero 64-byte raw value")
    out_root = (args.out or ROOT / "out" / f"{args.project}_3in1").resolve()
    out_root.mkdir(parents=True, exist_ok=True)

    supplied = {stage: getattr(args, f"{stage}_dir") for stage in STAGES}
    directories = stage_dirs(out_root)
    if any(supplied.values()):
        if not all(supplied.values()):
            raise SystemExit("error: provide --app-dir, --loader-dir and --preloader-dir together")
        directories = {stage: path.resolve() for stage, path in supplied.items()}
    else:
        for stage in STAGES:
            build_firmware(out_root, args.project, stage, toolchain_path,
                           profile="release" if args.production else "default")
    with tempfile.TemporaryDirectory(prefix="factory-", dir=out_root) as tmp:
        assemble(args, directories, Path(tmp), out_root, signature_path, public_key_path)


def assemble(args, directories, work, out_root, signature_path, public_key_path):
    # Copy first, then verify and pack those same bytes. A concurrent link can
    # change an input directory without changing an already validated package.
    binaries = {}
    try:
        for stage, source in directories.items():
            directory = work / stage
            directory.mkdir()
            shutil.copyfile(source / "build-info.json", directory / "build-info.json")
        verify_image_set(work, args.project, required_profile="release" if args.production else None)
        for stage, source in directories.items():
            directory = work / stage
            name = firmware_output_name(args.project, stage)
            for filename in (f"{name}.bin", f"{name}.manifest.json", ".config"):
                shutil.copyfile(source / filename, directory / filename)
            verify_linked_files(directory, name)
            binaries[stage] = directory / f"{name}.bin"
        if args.production:
            verify_production_configs(work)
    except (OSError, ValueError, KeyError) as error:
        raise SystemExit(f"error: incomplete factory image set: {error}; rebuild the selected images") from error
    app_image = work / f"{args.project}_app.img"
    pack_cmd = [
        sys.executable,
        str(ROOT / "bootloader" / "scripts" / "pack_image.py"),
        "--layout",
        args.project,
        "--input",
        str(binaries["app"]),
        "--output",
        str(app_image),
        "--security-version",
        str(args.security_version),
    ]
    if signature_path:
        pack_cmd.extend(["--signature", str(signature_path)])
    if public_key_path:
        pack_cmd.extend(["--public-key", str(public_key_path)])
    run(pack_cmd)

    output = work / f"{args.project}_3in1.bin"
    run(
        [
            sys.executable,
            str(ROOT / "bootloader" / "scripts" / "merge_firmware.py"),
            "--layout",
            args.project,
            "--preloader",
            str(binaries["preloader"]),
            "--loader",
            str(binaries["loader"]),
            "--app",
            str(app_image),
            "--output",
            str(output),
        ]
    )
    app_image.replace(out_root / app_image.name)
    final_image = output.replace(out_root / output.name)
    print(f"three-image firmware: {final_image}", flush=True)


if __name__ == "__main__":
    main()
