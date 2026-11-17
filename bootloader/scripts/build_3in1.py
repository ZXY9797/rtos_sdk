#!/usr/bin/env python3
"""Configure, build, and merge a product three-image firmware."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys

from boot_layout import canonical_layout_names, load_layout


ROOT = Path(__file__).resolve().parents[2]
TOOLCHAIN_CANDIDATES = (
    ROOT / "out/toolchains/gcc-9.3.1/xpack-arm-none-eabi-gcc-9.3.1-1.4",
    Path("D:/code/mcu_prebuilt/win_x64/xpack-arm-none-eabi-gcc-9.3.1-1.4"),
)
DEFAULT_GCC9 = next(
    (path for path in TOOLCHAIN_CANDIDATES if path.exists()),
    None,
)


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
        "-ULINKER_SCRIPT",
        "-UCMAKE_EXE_LINKER_FLAGS",
        "-UBOOT_LAYOUT",
        "-UCONFIG_DIR",
        "-UAPPLICATION",
        "-UBOARD",
        "-UBOARD_ROOT",
        "-UCONF_FILE",
        "-UDTC_OVERLAY_FILE",
        "-UMODULE_ROOTS",
        f"-Dp={product}",
        f"-DFIRMWARE_TYPE={firmware_type}",
        f"-DPROFILE={profile}",
        "-UEXTRA_CONF_FILE",
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


def verify_image_set(out_root: Path, product: str) -> None:
    layout = load_layout(product)
    expected_layout = hashlib.sha256(layout['path'].read_bytes()).hexdigest()
    board = None
    for image in ('preloader', 'loader', 'app'):
        info = json.loads((out_root / image / 'build-info.json').read_text(encoding='utf-8'))
        if info.get('product') != product or info.get('firmware') != image:
            raise SystemExit(f'error: {image} build identity differs from the requested product/image')
        if info.get('inputs', {}).get('layout') != expected_layout:
            raise SystemExit(f'error: {image} does not use the canonical product layout')
        selected_board = info.get('board')
        if not selected_board or (board is not None and selected_board != board):
            raise SystemExit(f'error: {image} board differs from the rest of the image set')
        board = selected_board


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

    preloader_bin = build_firmware(
        out_root,
        args.project,
        "preloader",
        toolchain_path,
        profile="release" if args.production else "default",
    )
    loader_bin = build_firmware(
        out_root,
        args.project,
        "loader",
        toolchain_path,
        profile="release" if args.production else "default",
    )
    app_bin = build_firmware(out_root, args.project, "app", toolchain_path,
                             profile="release" if args.production else "default")
    verify_image_set(out_root, args.project)
    if args.production:
        verify_production_configs(out_root)
    app_image = out_root / f"{args.project}_app.img"
    pack_cmd = [
        sys.executable,
        str(ROOT / "bootloader" / "scripts" / "pack_image.py"),
        "--layout",
        args.project,
        "--input",
        str(app_bin),
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
            str(app_image),
            "--output",
            str(output),
        ]
    )
    print(f"three-image firmware: {output}", flush=True)


if __name__ == "__main__":
    main()
