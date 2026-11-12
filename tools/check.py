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


IMAGES = {"tfu1": ROOT / "analysis" / "SWTFU.decoded.exe", "tfu2": ROOT / "analysis" / "tfu2" / "SWTFU2.decoded.exe"}


def check_sites():
    """Every profile site's original bytes must be at that address in the decoded image."""
    import importlib
    import pefile
    ok = True
    for game, image in IMAGES.items():
        if not image.exists():
            print(f"skip {game} site check (no {image.name}; see tools/decode_exe.py)")
            continue
        pe = pefile.PE(str(image), fast_load=True)
        base = pe.OPTIONAL_HEADER.ImageBase
        profile = importlib.import_module(f"swtfu.{game}.fps.profile")
        sites = [(name, address, original) for name, address, original, _ in profile.PATCHES]
        sites += [(f"{m.__name__}[{i}]", s[0], s[1]) for m in profile.HELPERS for i, s in enumerate(m.SITES)]
        if game == "tfu2":
            movie = importlib.import_module("swtfu.tfu2.movie_messages")
            sites += [(f"movie_messages[{i}]", s[0], s[1]) for i, s in enumerate(movie.SITES)]
        bad = [name for name, address, original in sites if pe.get_data(address - base, len(original)) != original]
        print(f"{'ok  ' if not bad else 'FAIL'} {game} patch sites match decoded image ({len(sites)} sites){': ' + ', '.join(bad) if bad else ''}")
        ok &= not bad
    return ok


def main():
    sys.path.insert(0, str(ROOT))
    ok = all([run(name, [sys.executable, str(ROOT / "analysis" / f"{name}.py")]) for name in TESTS])
    ok &= check_sites()
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
