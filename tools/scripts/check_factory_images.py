#!/usr/bin/env python3
"""Build each boot chain and package its app from the firmware test matrix."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys


ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "bootloader/scripts"))
from boot_layout import canonical_layout_names


def run(*args) -> None:
    command = [str(arg) for arg in args]
    print("+ " + " ".join(command), flush=True)
    subprocess.run(command, cwd=ROOT, check=True)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("-B", "--build", required=True, type=Path, help="firmware matrix output")
    parser.add_argument("--toolchain-path", type=Path)
    args = parser.parse_args()
    matrix = args.build.resolve()
    factory = matrix / "factory"
    factory.mkdir(parents=True, exist_ok=True)
    checksum_path = factory / "SHA256SUMS"
    checksum_path.unlink(missing_ok=True)
    checksums = []
    plan = json.loads((ROOT / "tools/test_matrix.json").read_text(encoding="utf-8"))
    if set(plan["factory"]) != set(canonical_layout_names()):
        parser.error("factory cases must cover every product layout")
    for product, configuration in plan["factory"].items():
        case = configuration["app_case"]
        app = matrix / case
        if not (app / "build-info.json").is_file():
            parser.error(f"build firmware case {case} in {matrix} first")
        output = factory / product
        toolchain = configuration["boot_toolchain"]
        for stage in ("preloader", "loader", "upgrade"):
            directory = output / stage
            options = []
            if args.toolchain_path and toolchain == "armgcc9":
                options.extend(["--toolchain-path", args.toolchain_path.resolve()])
            if stage == "upgrade":
                loader = output / "loader" / f"{product}_loader.bin"
                options.append(f"--cmake-arg=-DLOADER_PAYLOAD_FILE={loader.as_posix()}")
            run(sys.executable, ROOT / "tools/sdk.py", "configure", "-p", product,
                "--toolchain", toolchain, "-B", directory, f"--cmake-arg=-DFIRMWARE_TYPE={stage}",
                f"--cmake-arg=-DPython3_EXECUTABLE={Path(sys.executable).as_posix()}", *options)
            run("cmake", "--build", directory, "--parallel", "8")
        run(sys.executable, ROOT / "bootloader/scripts/build_3in1.py", product,
            "--out", output, "--app-dir", app, "--loader-dir", output / "loader",
            "--preloader-dir", output / "preloader")
        image = output / f"{product}_3in1.bin"
        checksum = hashlib.sha256(image.read_bytes()).hexdigest()
        checksums.append(f"{checksum}  {image.relative_to(factory).as_posix()}")
    checksum_path.write_text("\n".join(checksums) + "\n", encoding="ascii")


if __name__ == "__main__":
    main()
