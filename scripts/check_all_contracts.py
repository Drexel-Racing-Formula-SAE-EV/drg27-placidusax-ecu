#!/usr/bin/env python3
"""Run the complete DRG27 ECU contract gate in one command.

    python3 scripts/check_all_contracts.py . [build-dir]

Source-only contracts always run. Devicetree contracts need either a completed
target build or an explicit EDT pickle via --edt-pickle.
"""

import argparse
import subprocess
import sys
from pathlib import Path

SOURCE_ONLY = ("check_fail_low_contract.py", "check_null_platform_core.py")
DEVICETREE = ("check_board_contract.py",)


def run(cmd, cwd):
    print("\n>>> " + " ".join(str(x) for x in cmd), flush=True)
    return subprocess.run(cmd, cwd=cwd, check=False).returncode


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("repo_root", type=Path)
    parser.add_argument("build_dir", nargs="?", type=Path)
    parser.add_argument("--edt-pickle", type=Path)
    args = parser.parse_args()

    repo = args.repo_root.resolve()
    scripts = repo / "scripts"
    python = sys.executable
    failed = []

    for script in SOURCE_ONLY:
        if run((python, str(scripts / script), str(repo)), repo) != 0:
            failed.append(script)

    if args.edt_pickle or args.build_dir:
        for script in DEVICETREE:
            cmd = [python, str(scripts / script)]
            if args.edt_pickle:
                cmd += ["--edt-pickle", str(args.edt_pickle.resolve())]
            else:
                cmd += [str(args.build_dir.resolve())]
            if run(tuple(cmd), repo) != 0:
                failed.append(script)
    else:
        print("\nSKIP: devicetree contracts (no build dir or --edt-pickle given)")

    if failed:
        print(f"\nFAIL: {len(failed)} contract(s): {', '.join(failed)}")
        return 1

    print("\nPASS: complete DRG27 ECU contract suite")
    return 0


if __name__ == "__main__":
    sys.exit(main())
