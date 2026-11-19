#!/usr/bin/env python3
"""Generate reproducible build identity, ELF resources and device topology."""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "bootloader/scripts"))
from boot_layout import load_layout


def digest(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def write_changed(path: Path, text: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    if not path.exists() or path.read_text(encoding="utf-8") != text:
        temporary = path.with_suffix(path.suffix + ".tmp")
        temporary.write_text(text, encoding="utf-8")
        temporary.replace(path)


def normalize_paths(text: str, root: Path, build: Path) -> str:
    # Replace the more specific path first: the build normally lives in root.
    for path, label in ((build, "BUILD_ROOT"), (root, "SDK_ROOT")):
        for spelling in (str(path), path.as_posix(), str(path).replace('\\', '\\\\')):
            text = text.replace(spelling, label)
    return text


def git_identity(root: Path, excluded: Path | None = None) -> dict:
    def git(*args):
        return subprocess.check_output(["git", "-C", str(root), *args], stderr=subprocess.DEVNULL)
    try:
        revision = git("rev-parse", "HEAD").decode().strip()
        # Deleted vendor trees can be very large. HEAD already identifies their
        # original bytes: hash the raw change records and surviving file bytes,
        # rather than materializing a binary patch for every firmware build.
        changes = git("diff", "HEAD", "--raw", "--no-renames", "--abbrev=40", "-z", "--relative", "--", ".")
        modified = []
        for name in git("diff", "HEAD", "--name-only", "--no-renames", "-z", "--relative", "--", ".").split(b"\0"):
            if not name:
                continue
            path = root / name.decode('utf-8')
            if excluded is not None and path.resolve().is_relative_to(excluded.resolve()):
                continue
            if path.is_symlink():
                content = str(path.readlink()).encode()
            elif path.is_file():
                content = path.read_bytes()
            else:
                continue
            modified.append((name.decode('utf-8'), digest(content)))
        extra = []
        for name in git("ls-files", "--others", "--exclude-standard", "-z").split(b"\0"):
            if name:
                path = root / name.decode("utf-8")
                if excluded is not None and path.resolve().is_relative_to(excluded.resolve()): continue
                if path.is_file(): extra.append((name.decode("utf-8"), digest(path.read_bytes())))
        return {"revision": revision, "dirty": bool(changes or extra),
                "changes_sha256": digest(changes + json.dumps([modified, extra], sort_keys=True).encode())}
    except (OSError, subprocess.CalledProcessError):
        files = [(p.relative_to(root).as_posix(), digest(p.read_bytes()))
                 for p in sorted(root.rglob("*")) if p.is_file()
                 and not any(x in p.relative_to(root).parts for x in ("out", "build", ".git", "__pycache__"))
                 and (excluded is None or not p.resolve().is_relative_to(excluded.resolve()))]
        return {"revision": None, "dirty": None,
                "tree_sha256": digest(json.dumps(files).encode())}


def native_dts_inputs(source: Path, text: str):
    if "/include/" not in text and "/incbin/" not in text:
        return []
    # dtlib resolves relative/nested inputs and CPP line locations. Record
    # files when opened, before it prunes unreferenced pin definitions.
    sys.path.insert(0, str(Path(__file__).parent / "dts/python-devicetree/src"))
    from devicetree import dtlib
    inputs = []

    class TrackingTree(dtlib.DT):
        def _open(self, filename, mode="r", **kwargs):
            stream = super()._open(filename, mode, **kwargs)
            inputs.append((Path(stream.name).resolve(), "b" in mode))
            return stream

    TrackingTree(str(source))
    return inputs


def dts_token_digest(text: str) -> str:
    text = re.sub(r'^#\s*\d+[^\n]*', '', text, flags=re.M)
    pattern = r'''"(?:\\.|[^"\\])*"|'(?:\\.|[^'\\])*'|//[^\n]*|/\*[\s\S]*?\*/|\w+|[^\s]'''
    tokens = [t for t in re.findall(pattern, text) if not t.startswith(("//", "/*"))]
    return digest(json.dumps(tokens, separators=(",", ":")).encode())


def board_fingerprint(source: Path) -> str:
    text = source.read_text(encoding='utf-8')
    effective = dts_token_digest(text)
    inputs = native_dts_inputs(source, text)
    if not inputs:
        return effective
    # The source contains include names and nesting. Digest content in parse
    # order so absolute workspace paths do not become part of board identity.
    included = [digest(path.read_bytes()) if binary else dts_token_digest(path.read_text(encoding='utf-8'))
                for path, binary in inputs]
    return digest(json.dumps([effective, included], separators=(",", ":")).encode())


def prepare(args) -> dict:
    identity = {"schema": "rtos-sdk.build.v1", "product": args.product,
                "firmware": args.firmware, "profile": args.profile,
                "sdk": git_identity(args.root, args.build), "application": git_identity(args.application, args.build),
                "compiler": subprocess.check_output([str(args.compiler), "--version"], text=True).splitlines()[0],
                "inputs": {}}
    identity['modules'] = [git_identity(path.resolve(), args.build) for path in args.module_root]
    identity['board'] = args.board
    identity['inputs']['board_sha256'] = board_fingerprint(args.base_board)
    app = args.application.resolve()
    if app.is_relative_to(args.root.resolve()):
        identity['application_path'] = 'SDK_ROOT/' + app.relative_to(args.root.resolve()).as_posix()
    else:
        identity['application_path'] = app.as_posix()
    if args.application_id:
        identity['application_id'] = args.application_id
    identity['configuration'] = {}
    for name, paths in (('fragments', args.config_fragment), ('devicetree_sources', args.overlay)):
        identity['configuration'][name] = [
            {'path': normalize_paths(path.as_posix(), args.root, args.build),
             'sha256': digest(path.read_bytes())} for path in paths]
    for name, path in (("config", args.build / ".config"),
                       ("devicetree", args.build / "firmware.dts"), ("layout", args.layout)):
        identity["inputs"][name] = digest(path.read_bytes())
    # Absolute workspace paths in generated DTS comments must not define identity.
    normalized = (args.build / "firmware.dts").read_text(encoding="utf-8")
    normalized = normalize_paths(normalized, args.root, args.build)
    identity["inputs"]["devicetree"] = digest(normalized.encode())
    ledger = args.root / 'tools/dependencies.json'
    if ledger.exists(): identity['inputs']['dependencies'] = digest(ledger.read_bytes())
    commands = args.build / 'compile_commands.json'
    if commands.exists():
        policy = normalize_paths(commands.read_text(encoding='utf-8'), args.root, args.build)
        identity['inputs']['compile_commands'] = digest(policy.encode())
    identity["build_id"] = digest(json.dumps(identity, sort_keys=True).encode())
    write_changed(args.build / "build-info.json", json.dumps(identity, indent=2, sort_keys=True) + "\n")
    header = ("#pragma once\nnamespace sdk_build {\n"
              f'inline constexpr char id[] = "{identity["build_id"]}";\n'
              f'inline constexpr char product[] = "{args.product}";\n'
              f'inline constexpr char profile[] = "{args.profile}";\n'
              "}\n")
    write_changed(args.build / "include/generated/build_info.h", header)
    return identity


def device_graph(report: dict) -> str:
    lines = ["digraph devices {", "  rankdir=LR;"]
    for dev in report.get("devices", []):
        if not dev["enabled"]: continue
        name = dev.get("alias") or dev["path"]
        lines.append(f'  n{dev["ord"]} [label={json.dumps(name)}];')
        for dep in dev.get("dependencies", []):
            lines.append(f'  n{dep["ord"]} -> n{dev["ord"]} [label={json.dumps(dep["reason"])}];')
    return "\n".join([*lines, "}", ""])


def elf_resources(path: Path) -> dict:
    try:
        from elftools.elf.elffile import ELFFile
    except ImportError as error:
        raise RuntimeError('Install SDK Python dependencies: '
                           'python -m pip install -r tools/requirements-ci.txt') from error
    with path.open("rb") as stream:
        elf = ELFFile(stream)
        sections = [{"name": s.name, "address": s["sh_addr"], "size": s["sh_size"],
                     "nobits": s["sh_type"] == "SHT_NOBITS"}
                    for s in elf.iter_sections() if s["sh_flags"] & 2 and s["sh_size"]]
        symbols = []
        table = elf.get_section_by_name(".symtab")
        if table:
            for sym in table.iter_symbols():
                if sym["st_info"]["type"] != "STT_OBJECT" or not sym["st_size"]: continue
                if any(word in sym.name.lower() for word in ("stack", "queue", "buffer", "storage", "heap")):
                    symbols.append({"name": sym.name, "address": sym["st_value"], "size": sym["st_size"]})
        segments = [{"physical_address": seg["p_paddr"], "virtual_address": seg["p_vaddr"],
                     "file_bytes": seg["p_filesz"], "memory_bytes": seg["p_memsz"]}
                    for seg in elf.iter_segments() if seg["p_type"] == "PT_LOAD"]
    return {"schema": "rtos-sdk.resources.v1", "sections": sections, "segments": segments,
            "static_resource_symbols": sorted(symbols, key=lambda x: x["size"], reverse=True),
            "measurement": "ELF allocation only; no measured stack watermarks or target timing"}


def image_usage(info, layout, binary_bytes, sections):
    firmware = info["firmware"]
    partitions = layout["partitions"]
    if firmware == "app":
        limit = min(partitions["slot0"]["size"], partitions["upgrade"]["size"])
        image_bytes = binary_bytes + layout["image_header_size"]
    else:
        limit = partitions[firmware]["size"]
        image_bytes = binary_bytes
    ram_start = layout["ram_base"]
    ram_end = ram_start + layout["ram_size"]
    ram_bytes = sum(section["size"] for section in sections
                    if ram_start <= section["address"] < ram_end)
    return {"image_bytes": image_bytes, "image_limit_bytes": limit,
            "remaining_bytes": limit - image_bytes, "static_ram_bytes": ram_bytes,
            "ram_limit_bytes": layout["ram_size"]}


def resource_configuration(info: dict, rtos: str) -> dict:
    # Compare the same build variant across revisions, including changes to the
    # contents of its selected Kconfig fragments and application overlays.
    sources = info["configuration"]
    app_root = info["application_path"].rstrip("/") + "/"

    def selected_paths(group):
        paths = [entry["path"] for entry in sources[group]]
        return ["APPLICATION_ROOT/" + path[len(app_root):] if path.startswith(app_root) else path
                for path in paths]

    return {
        "product": info["product"], "firmware": info["firmware"], "profile": info["profile"],
        "application": info.get("application_id", info["application_path"]), "board": info["board"],
        "board_sha256": info["inputs"]["board_sha256"], "rtos": rtos,
        "layout_sha256": info["inputs"]["layout"],
        "compiler": info["compiler"].replace("gcc.exe ", "gcc "),
        "fragments": selected_paths("fragments"),
        "devicetree_sources": selected_paths("devicetree_sources"),
    }


def resource_delta(current, baseline):
    if current["configuration"] != baseline.get("configuration"):
        raise ValueError("Resource baseline must match application, board, compiler and image configuration")
    return {key: current["usage"][key] - baseline["usage"][key]
            for key in ("image_bytes", "static_ram_bytes")}


def postprocess(args) -> None:
    # The compressor rewrites RAM section VMAs and sizes in the distribution ELF.
    resources = elf_resources(Path(str(args.elf) + "_origin"))
    info_path = args.build / "build-info.json"
    info = json.loads(info_path.read_text(encoding="utf-8"))
    layout = load_layout(str(args.layout))
    config = (args.build / ".config").read_text(encoding="utf-8").splitlines()
    rtos = next(name for name in ("FREERTOS", "RT_THREAD", "BAREMETAL")
                if f"CONFIG_RTOS_{name}=y" in config)
    usage = image_usage(info, layout, args.elf.with_suffix(".bin").stat().st_size,
                        resources["sections"])
    resources["configuration"] = resource_configuration(info, rtos)
    resources["usage"] = usage
    resources["build_id"] = info["build_id"]
    resources["measurement"] = "Linked RAM sections before compression; image includes the App header."
    if args.baseline:
        reference = json.loads(args.baseline.read_text(encoding="utf-8"))
        resources["baseline_delta"] = resource_delta(resources, reference)
    print(f"{info['product']} {info['firmware']} ({info['profile']}): "
          f"image {usage['image_bytes']}/{usage['image_limit_bytes']} B "
          f"({100 * usage['image_bytes'] / usage['image_limit_bytes']:.2f}%), "
          f"remaining {usage['remaining_bytes']} B; static RAM {usage['static_ram_bytes']} B")
    if args.baseline:
        delta = resources["baseline_delta"]
        print(f"Baseline delta: image {delta['image_bytes']:+d} B, "
              f"static RAM {delta['static_ram_bytes']:+d} B")
    if usage["remaining_bytes"] < 0:
        raise ValueError(f"{info['firmware']} exceeds its physical image budget")
    output = args.elf.with_suffix(".resources.json")
    write_changed(output, json.dumps(resources, indent=2) + "\n")
    report_path = args.build / "include/generated/devices.json"
    report = json.loads(report_path.read_text(encoding="utf-8"))
    write_changed(args.elf.with_suffix(".devices.dot"), device_graph(report))
    lines = [f'# {info["product"]} architecture report', "", f'Build: `{info["build_id"]}`', "",
             "| Device | Type | Capabilities | Dependencies |", "|---|---|---|---|"]
    for dev in report["devices"]:
        if dev["enabled"]:
            dependencies = ", ".join(x.get("alias") or x["path"] for x in dev["dependencies"])
            lines.append(f'| {dev.get("alias") or dev["path"]} | {dev["type"]} | {", ".join(dev.get("capabilities", []))} | {dependencies} |')
    lines += ["", "Resources come from the linked ELF. Board timing, DMA reachability,",
              "hardware break response and stack watermarks require target measurements.", ""]
    write_changed(args.elf.with_suffix(".architecture.md"), "\n".join(lines))
    manifest = {"schema": "rtos-sdk.artifacts.v1", "build": info,
                "artifacts": {}}
    for path in (args.elf, args.elf.with_suffix(".bin"), args.elf.with_suffix(".hex"),
                 args.elf.with_suffix(".map"), output, report_path):
        if path.exists(): manifest["artifacts"][path.name] = {"sha256": digest(path.read_bytes()), "bytes": path.stat().st_size}
    write_changed(args.elf.with_suffix(".manifest.json"), json.dumps(manifest, indent=2) + "\n")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", type=Path, required=True)
    parser.add_argument("--elf", type=Path)
    parser.add_argument("--baseline", type=Path)
    parser.add_argument("--root", type=Path)
    parser.add_argument("--application", type=Path)
    parser.add_argument("--application-id")
    parser.add_argument("--compiler", type=Path)
    parser.add_argument("--layout", type=Path)
    parser.add_argument("--product")
    parser.add_argument("--firmware")
    parser.add_argument("--profile", default="default")
    parser.add_argument("--board")
    parser.add_argument("--base-board", type=Path)
    parser.add_argument("--native-deps", nargs=2, type=Path, metavar=("MERGED_DTS", "OUTPUT"))
    parser.add_argument("--config-fragment", action="append", type=Path, default=[])
    parser.add_argument("--overlay", action="append", type=Path, default=[])
    parser.add_argument("--module-root", action="append", type=Path, default=[])
    args = parser.parse_args()
    if args.elf:
        postprocess(args)
    else:
        prepare(args)
        if args.native_deps:
            source, output = args.native_deps
            inputs = native_dts_inputs(source, source.read_text(encoding="utf-8"))
            write_changed(output, "".join(path.as_posix() + "\n" for path in sorted({p for p, _ in inputs})))


if __name__ == "__main__":
    main()
