#!/usr/bin/env python3
"""Generate reproducible build identity, ELF resources and device topology."""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import subprocess


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


def prepare(args) -> dict:
    identity = {"schema": "rtos-sdk.build.v1", "product": args.product,
                "firmware": args.firmware, "profile": args.profile,
                "sdk": git_identity(args.root, args.build), "application": git_identity(args.application, args.build),
                "compiler": subprocess.check_output([str(args.compiler), "--version"], text=True).splitlines()[0],
                "inputs": {}}
    identity['modules'] = [git_identity(path.resolve(), args.build) for path in args.module_root]
    identity['board'] = args.board
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


def postprocess(args) -> None:
    resources = elf_resources(args.elf)
    info_path = args.build / "build-info.json"
    info = json.loads(info_path.read_text(encoding="utf-8"))
    resources["build_id"] = info["build_id"]
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
    parser.add_argument("--root", type=Path)
    parser.add_argument("--application", type=Path)
    parser.add_argument("--compiler", type=Path)
    parser.add_argument("--layout", type=Path)
    parser.add_argument("--product")
    parser.add_argument("--firmware")
    parser.add_argument("--profile", default="default")
    parser.add_argument("--board")
    parser.add_argument("--config-fragment", action="append", type=Path, default=[])
    parser.add_argument("--overlay", action="append", type=Path, default=[])
    parser.add_argument("--module-root", action="append", type=Path, default=[])
    args = parser.parse_args()
    if args.elf:
        postprocess(args)
    else:
        prepare(args)


if __name__ == "__main__":
    main()
