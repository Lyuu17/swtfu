"""Developer check: run after changing swtfu/ or helper/. No game needed.

1. Python tests of the patch logic (install/rollback against a disposable child).
2. helper/tables.h is up to date with swtfu/*.py.
3. SWTFU-Helper.exe selftest (if built).
"""
import os
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
TESTS = [
    "test_camera_ownership", "test_fps_profile", "test_input_ownership",
    "test_profile_install", "test_smoothing_install", "test_smoothing_ownership",
    "test_thread_quiescence", "audit_runtime_profile",
]


def run(label, command):
    result = subprocess.run(command, cwd=ROOT, env=dict(os.environ, PYTHONPATH=str(ROOT)),
                            capture_output=True, text=True, timeout=300)
    print(f"{'ok  ' if result.returncode == 0 else 'FAIL'} {label}")
    if result.returncode:
        print(result.stdout[-2000:] + result.stderr[-2000:])
    return result.returncode == 0


def main():
    ok = all([run(name, [sys.executable, str(ROOT / "analysis" / f"{name}.py")]) for name in TESTS])
    tables = ROOT / "helper" / "tables.h"
    before = tables.read_bytes()
    ok &= run("tables.h generation", [sys.executable, str(ROOT / "tools" / "gen_tables.py")])
    if tables.read_bytes() != before:
        print("FAIL tables.h was stale (now regenerated): rebuild the helper")
        ok = False
    exe = ROOT / "SWTFU-Helper.exe"
    if exe.exists():
        ok &= run("SWTFU-Helper selftest", [str(exe), "selftest"])
    else:
        print("skip SWTFU-Helper selftest (not built)")
    sys.exit(0 if ok else 1)


if __name__ == "__main__":
    main()
