#!/usr/bin/env python3
"""Check linked image and static RAM sizes for the selected firmware cases."""
import argparse
import json
from pathlib import Path


MATRIX = Path(__file__).resolve().parents[1] / "test_matrix.json"
METRICS = ("image_bytes", "static_ram_bytes")


def check_cases(build: Path, cases: list[dict]) -> tuple[str, bool]:
    rows = ["### Firmware resource usage (bytes)", "",
            "| Case | Image | Static RAM | Image ceiling | RAM ceiling | Result |",
            "|---|---:|---:|---:|---:|---|"]
    passed = True
    for case in cases:
        name = case["name"]
        paths = list((build / name).glob("*.resources.json"))
        try:
            if len(paths) != 1:
                raise ValueError(f"expected one resource report, found {len(paths)}")
            usage = json.loads(paths[0].read_text(encoding="utf-8"))["usage"]
            limits = case.get("budget")
            values = [usage[key] for key in METRICS]
            if any(type(value) is not int or value < 0 for value in values):
                raise ValueError("invalid resource usage")
            if limits is None:
                ceilings = ["-", "-"]
                status = "measured"
            else:
                ceilings = [limits[key] for key in METRICS]
                if any(type(value) is not int or value <= 0 for value in ceilings):
                    raise ValueError("invalid resource budget")
                excess = [f"{key} +{value - ceiling}" for key, value, ceiling
                          in zip(METRICS, values, ceilings) if value > ceiling]
                status = ", ".join(excess) if excess else "pass"
                if excess:
                    passed = False
            rows.append("| " + " | ".join(map(str, [name, *values, *ceilings, status])) + " |")
        except (OSError, ValueError, KeyError, TypeError) as error:
            rows.append(f"| {name} | - | - | - | - | error: {error} |")
            passed = False
    return "\n".join(rows) + "\n", passed


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("-B", "--build", required=True, type=Path)
    parser.add_argument("--case", action="append", default=[])
    args = parser.parse_args()
    cases = json.loads(MATRIX.read_text(encoding="utf-8"))["firmware"]
    names = set(args.case)
    unknown = names - {case["name"] for case in cases}
    if unknown:
        parser.error("unknown firmware case: " + ", ".join(sorted(unknown)))
    if names:
        cases = [case for case in cases if case["name"] in names]
    if not cases:
        parser.error("no firmware cases selected")
    summary, passed = check_cases(args.build, cases)
    args.build.mkdir(parents=True, exist_ok=True)
    (args.build / "resource-summary.md").write_text(summary, encoding="utf-8")
    print(summary)
    return 0 if passed else 1


if __name__ == "__main__":
    raise SystemExit(main())
