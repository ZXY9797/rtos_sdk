"""Run SDK checks through the same CMake, CTest and unittest entry points on every host."""
from __future__ import annotations

import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import traceback
import unittest
import xml.etree.ElementTree as ET

from scripts.sdk_image import image_cache_reset_args

ROOT = Path(__file__).resolve().parents[2]


def command(*argv, capture=False, check=True):
    print("+", subprocess.list2cmdline(list(map(str, argv))), flush=True)
    return subprocess.run(list(map(str, argv)), cwd=ROOT, check=check,
                          stdout=subprocess.PIPE if capture else None,
                          stderr=subprocess.STDOUT, text=True, errors="replace")


def cases_in(suite):
    for item in suite:
        if isinstance(item, unittest.TestSuite):
            yield from cases_in(item)
        else:
            yield item


def python_cases(directory, pattern="test_*.py"):
    # A fresh loader per root is necessary because unittest caches its top-level directory.
    suite = unittest.TestLoader().discover(str(ROOT / directory), pattern=pattern)
    return list(cases_in(suite))


class HostTestResult(unittest.TextTestResult):
    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)
        self.passed = []

    def addSuccess(self, test):
        super().addSuccess(test)
        self.passed.append(test.id())


def python_tests(directory, results, *, case=None, pattern="test_*.py", strict=False):
    try:
        selected = [test for test in python_cases(directory, pattern)
                    if not case or test.id() == case]
        if not selected:
            if not case:
                results[str(directory)] = {"status": "failed", "reason": f"no tests match {pattern}"}
            return
        result = unittest.TextTestRunner(verbosity=2, resultclass=HostTestResult).run(
            unittest.TestSuite(selected))
    except Exception:
        results[str(directory)] = {"status": "failed", "reason": traceback.format_exc()}
        return
    for name in result.passed:
        results[name] = {"status": "passed"}
    for test, _ in result.expectedFailures:
        results[test.id()] = {"status": "passed", "reason": "expected failure"}
    for test, reason in result.skipped:
        results[test.id()] = {"status": "skipped", "required": strict, "reason": reason}
    for test, trace in result.errors + result.failures:
        parent = getattr(test, "test_case", test)
        results[parent.id()] = {"status": "failed", "reason": trace}
    for test in result.unexpectedSuccesses:
        results[test.id()] = {"status": "failed", "reason": "unexpected success"}


def configure_host(args):
    build = args.build / "host"
    configure = ["cmake", "-S", ROOT / "tests", "-B", build, "-G", args.generator]
    if args.generator.startswith("Visual Studio"):
        configure.extend(["-A", "x64"])
    command(*configure)
    return build


def legacy_ctest(test, build, report):
    # CTest 3.20 supports dashboard XML. Native JUnit output was added in 3.21.
    run = command(*test, "-T", "Test", "--no-compress-output", check=False)
    testing = build / "Testing"
    tag = (testing / "TAG").read_text(encoding="utf-8").splitlines()[0]
    dashboard = ET.parse(testing / tag / "Test.xml")
    suite = ET.Element("testsuite", name="host")
    for test_case in dashboard.findall("./Testing/Test"):
        case = ET.SubElement(suite, "testcase", name=test_case.findtext("Name"))
        status = test_case.attrib["Status"]
        if status != "passed":
            kind = "skipped" if status == "notrun" else "failure"
            ET.SubElement(case, kind).text = test_case.findtext("Results/Measurement/Value")
    ET.ElementTree(suite).write(report, encoding="utf-8", xml_declaration=True)
    return run


def cpp_tests(args, label, results):
    build = configure_host(args)
    test = ["ctest", "--test-dir", build, "-C", "Debug"]
    build_args = ["cmake", "--build", build, "--config", "Debug", "--parallel", "8"]
    if args.case or label:
        if label:
            test.extend(["-L", "^" + re.escape(label) + "$"])
            target = "host-label-" + label
        else:
            test.extend(["-R", "^" + re.escape(args.case) + "$"])
            target = "host-case-" + args.case
        discovery = json.loads(command(*test, "--show-only=json-v1", capture=True).stdout)
        if not discovery["tests"]:
            return
        build_args += ["--target", target]
    command(*build_args)
    report = build / "ctest.xml"
    report.unlink(missing_ok=True)
    test += ["--output-on-failure", "--no-tests=error"]
    if "--output-junit" in command("ctest", "--help", capture=True).stdout:
        run = command(*test, "--output-junit", report, check=False)
    else:
        run = legacy_ctest(test, build, report)
    if report.exists():
        for item in ET.parse(report).iterfind(".//testcase"):
            status = item.get("status", "run")
            name = item.attrib["name"]
            if item.find("failure") is not None or item.find("error") is not None:
                results[name] = {"status": "failed", "reason": f"see {report}"}
            elif status in ("notrun", "disabled") or item.find("skipped") is not None:
                results[name] = {"status": "skipped", "required": bool(args.case)}
            else:
                results[name] = {"status": "passed"}
    if not any(case["status"] == "failed" for case in results.values()):
        run.check_returncode()
    if not results:
        raise ValueError(f"CTest did not report test cases: {report}")


def check_step(results, name, action):
    try:
        action()
    except (subprocess.CalledProcessError, OSError, ValueError) as error:
        print(f"{name} failed: {error}", file=sys.stderr, flush=True)
        results[name] = {"status": "failed", "reason": str(error)}
    else:
        results[name] = {"status": "passed"}


def host_checks(args, plan, results):
    label = "quick" if args.suite == "quick" else args.label
    (args.build / "host/ctest.xml").unlink(missing_ok=True)
    audits = plan["audit"]
    python_case = args.case and "." in args.case
    if not python_case and args.case not in audits:
        try:
            cpp_tests(args, label, results)
        except (OSError, ValueError, ET.ParseError, subprocess.CalledProcessError) as error:
            results["ctest"] = {"status": "failed", "reason": str(error)}
            print(error, file=sys.stderr)
    if not args.case or python_case:
        for domain, directory in plan["python"].items():
            if not label or label == domain:
                python_tests(directory, results, case=args.case,
                             strict=bool(args.toolchain_path or args.case or args.suite == "all"))
    if label in (None, "tools"):
        for name, arguments in audits.items():
            if not args.case or name == args.case:
                check_step(results, name, lambda: command(sys.executable, *arguments))


def negative_config(args, case):
    build = args.build / case["name"]
    run = command("cmake", "-S", ROOT, "-B", build, "-G", "Ninja",
                  *image_cache_reset_args(), *case["arguments"],
                  capture=True, check=False)
    log = args.build / (case["name"] + ".log")
    log.write_text(run.stdout, encoding="utf-8")
    if not run.returncode or case["expected"] not in " ".join(run.stdout.split()):
        print(run.stdout, file=sys.stderr)
        raise ValueError(f"expected rejection '{case['expected']}'; see {log}")


def matches(args, name, labels):
    return (not args.case or args.case == name) and (not args.label or args.label in labels)


def integration_checks(args, plan, results, include_image=True):
    for case in plan["rejected_configurations"]:
        if matches(args, case["name"], ["config"]):
            check_step(results, case["name"], lambda: negative_config(args, case))

    if include_image and matches(args, "image-config", ["config"]):
        python_tests(plan["host"]["python"]["tools"], results, pattern="test_image_devices.py",
                     strict=True)

    if matches(args, "rtos-ports", ["osal"]):
        def ports():
            compiler = shutil.which("arm-none-eabi-gcc")
            pinned = os.environ.get("ARMGCC9_TOOLCHAIN_PATH")
            if pinned:
                compiler = shutil.which("arm-none-eabi-gcc", path=str(Path(pinned) / "bin"))
            if not compiler:
                raise ValueError("RTOS ports require --toolchain-path or arm-none-eabi-gcc on PATH")
            command(sys.executable, ROOT / "tools/scripts/check_rtos_ports.py", "--compiler",
                    compiler, "--build", args.build / "rtos-ports")
        check_step(results, "rtos-ports", ports)

    if matches(args, "external-app", ["sdk"]):
        def external():
            build = args.build / "external-app"
            configure = ["cmake", "-S", ROOT / "tests/fixtures/external_app", "-B", build,
                         "-G", "Ninja"]
            options = [f"-DSDK_ROOT={ROOT.as_posix()}", "-Dt=armgcc9"]
            command(*configure, *image_cache_reset_args(), *options)
            command(*configure, *options)
            command("cmake", "--build", build, "--parallel", "8")
        check_step(results, "external-app", external)


def firmware_checks(args, cases, results):
    (args.build / "resource-summary.md").unlink(missing_ok=True)
    selected = [case for case in cases if matches(args, case["name"], case["capabilities"])]
    for case in selected:
        def build_case():
            build = args.build / case["name"]
            command("cmake", "-S", ROOT, "-B", build, "-G", "Ninja",
                    *image_cache_reset_args(), *case["arguments"])
            command("cmake", "--build", build, "--parallel", "8")
        check_step(results, case["name"], build_case)
    if selected and all(results[case["name"]]["status"] == "passed" for case in selected):
        selection = []
        if args.case or args.label:
            for case in selected:
                selection.extend(["--case", case["name"]])
        check_step(results, "resource-budget", lambda: command(
            sys.executable, ROOT / "tools/scripts/resource_budget.py", "-B", args.build, *selection))


def execute(args):
    args.build = args.build.resolve()
    args.build.mkdir(parents=True, exist_ok=True)
    report = args.build / "test-results.json"
    if not args.list:
        report.unlink(missing_ok=True)
    if (args.label or args.case) and args.suite in ("all", "quick"):
        raise SystemExit("Select --suite host, contracts or firmware to use --label or --case")
    if args.toolchain_path:
        path = args.toolchain_path.resolve()
        if not shutil.which("arm-none-eabi-gcc", path=str(path / "bin")):
            raise SystemExit(f"--toolchain-path does not contain an ARM compiler in bin: {path}")
        os.environ["ARMGCC9_TOOLCHAIN_PATH"] = path.as_posix()

    plan = json.loads((ROOT / "tools/test_matrix.json").read_text(encoding="utf-8"))
    if args.list:
        return list_cases(args, plan)
    results = {}
    if args.suite in ("quick", "host", "all"):
        host_checks(args, plan["host"], results)
    if args.suite in ("contracts", "all"):
        integration_checks(args, plan, results, include_image=args.suite != "all")
    if args.suite in ("firmware", "all"):
        firmware_checks(args, plan["firmware"], results)
    if args.suite == "all" and all(results[case["name"]]["status"] == "passed"
                                   for case in plan["firmware"]):
        check_step(results, "factory-images", lambda: command(
            sys.executable, ROOT / "tools/scripts/check_factory_images.py", "-B", args.build))
    if not results:
        results["selection"] = {"status": "failed", "reason": f"no test matched {args.case or args.label}"}
    counts = {status: sum(case["status"] == status for case in results.values())
              for status in ("passed", "failed", "skipped")}
    success = counts["failed"] == 0 and not any(case.get("required") and case["status"] == "skipped"
                                                for case in results.values())
    report.write_text(json.dumps({"suite": args.suite, "success": success, "counts": counts,
                                  "cases": results}, indent=2)
                      + "\n", encoding="utf-8")
    print(f"{args.suite}: {counts['passed']} passed, {counts['failed']} failed, "
          f"{counts['skipped']} skipped. Report: {report}", flush=True)
    for name, case in results.items():
        if case["status"] != "passed":
            requirement = " (required)" if case.get("required") else ""
            print(f"  {case['status']}{requirement} {name}: {case.get('reason', '')}")
    return int(not success)


def list_cases(args, plan):
    cases = {}
    if args.suite in ("host", "quick", "all"):
        build = configure_host(args)
        listing = command("ctest", "--test-dir", build, "-C", "Debug",
                          "--show-only=json-v1", capture=True)
        for case in json.loads(listing.stdout)["tests"]:
            labels = next((prop["value"] for prop in case["properties"] if prop["name"] == "LABELS"), [])
            cases[case["name"]] = labels
        for label, directory in plan["host"]["python"].items():
            cases.update((test.id(), [label]) for test in python_cases(directory))
        cases.update((name, ["tools"]) for name in plan["host"]["audit"])
    if args.suite in ("contracts", "all"):
        names = [case["name"] for case in plan["rejected_configurations"]]
        cases.update((name, ["config"]) for name in names + ["image-config"])
        cases.update({"external-app": ["sdk"], "rtos-ports": ["osal"]})
    if args.suite in ("firmware", "all"):
        cases.update((case["name"], case["capabilities"]) for case in plan["firmware"])
    selected = {name: labels for name, labels in cases.items() if matches(args, name, labels)
                and (args.suite != "quick" or "quick" in labels)}
    for name, labels in selected.items():
        print(f"{name} [{', '.join(labels)}]")
    return int(not selected)
