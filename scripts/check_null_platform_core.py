#!/usr/bin/env python3
"""Mechanically prove that lib/ecu_core is platform-free.

Builds the REAL lib/ecu_core/CMakeLists.txt in an ordinary host CMake project,
runs its tests, then audits compile_commands.json for leaked platform include
paths or defines. Source review is not sufficient: if a future core file needs
k_mutex, osDelay, a HAL handle, Devicetree or a board header to compile, this
gate must fail.

    python3 scripts/check_null_platform_core.py <repo-root>
"""

import argparse
import json
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

# Substrings that must never appear in a core translation unit's compile line.
FORBIDDEN_FRAGMENTS = (
    "zephyr",
    "freertos",
    "cmsis",
    "stm32",
    "hal_driver",
    "ecu_platform",
    "boards/",
    "/soc/",
)

# Headers no core source may include.
FORBIDDEN_INCLUDES = (
    "zephyr/",
    "FreeRTOS.h",
    "cmsis_os",
    "task.h",
    "queue.h",
    "semphr.h",
    "stm32f7xx",
    "core_cm7.h",
    "devicetree.h",
    "board.h",
    "ecu_platform/",
)


def run(cmd, cwd=None):
    return subprocess.run(cmd, cwd=cwd, capture_output=True, text=True)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("repo_root", type=Path)
    args = parser.parse_args()
    repo = args.repo_root.resolve()

    core = repo / "lib" / "ecu_core"
    project = repo / "tests" / "null_platform"

    if not (core / "CMakeLists.txt").is_file():
        print(f"FAIL: missing {core / 'CMakeLists.txt'}")
        return 1

    failures = []

    # -- 1. No forbidden include directives anywhere in the core -----------
    for path in sorted(core.rglob("*")):
        if path.suffix not in (".c", ".h"):
            continue
        text = path.read_text(encoding="utf-8", errors="replace")
        for lineno, line in enumerate(text.splitlines(), 1):
            stripped = line.strip()
            if not stripped.startswith("#include"):
                continue
            for bad in FORBIDDEN_INCLUDES:
                if bad in stripped:
                    rel = path.relative_to(repo)
                    failures.append(
                        f"{rel}:{lineno} core includes platform header: {stripped}"
                    )

    # -- 2. Build and test with no platform available ----------------------
    if shutil.which("cmake") is None:
        print("FAIL: cmake not available")
        return 1

    with tempfile.TemporaryDirectory() as tmp:
        build = Path(tmp) / "build"
        cfg = run(["cmake", "-S", str(project), "-B", str(build)])
        if cfg.returncode != 0:
            print("FAIL: null-platform configure failed")
            print(cfg.stdout[-3000:])
            print(cfg.stderr[-3000:])
            return 1

        bld = run(["cmake", "--build", str(build)])
        if bld.returncode != 0:
            print("FAIL: null-platform build failed")
            print(bld.stdout[-3000:])
            print(bld.stderr[-3000:])
            return 1

        tst = run(["ctest", "--output-on-failure"], cwd=build)
        if tst.returncode != 0:
            print("FAIL: null-platform tests failed")
            print(tst.stdout[-3000:])
            return 1

        # -- 3. Audit the actual compile lines -----------------------------
        ccjson = build / "compile_commands.json"
        if not ccjson.is_file():
            failures.append("compile_commands.json was not generated")
        else:
            entries = json.loads(ccjson.read_text())
            core_str = str(core)
            for entry in entries:
                if core_str not in entry["file"]:
                    continue
                command = entry.get("command") or " ".join(entry.get("arguments", []))
                lowered = command.lower()
                for bad in FORBIDDEN_FRAGMENTS:
                    if bad in lowered:
                        failures.append(
                            f"core compile line references '{bad}': {entry['file']}"
                        )

    if failures:
        print(f"FAIL: {len(failures)} portability violation(s)")
        for f in failures:
            print(f"  - {f}")
        return 1

    print("PASS: lib/ecu_core builds and tests with no platform available")
    return 0


if __name__ == "__main__":
    sys.exit(main())
