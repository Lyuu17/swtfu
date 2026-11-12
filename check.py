"""Run the tests that cover the shipped runtime helpers in swtfu/.

Usage: python check.py
None of these need the game running; install tests use the disposable
TestFrameTiming.exe / TestRendererReset.exe children in analysis/.
"""
import os
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parent
TESTS = [
    "test_borderless_config", "test_camera_ownership", "test_fps_profile",
    "test_frame_pacing_trace", "test_hang_dump", "test_hang_watch",
    "test_input_ownership", "test_profile_install", "test_smoothing_install",
    "test_smoothing_ownership", "test_thread_quiescence", "audit_runtime_profile",
]


def main():
    env = dict(os.environ, PYTHONPATH=str(ROOT))
    failed = []
    for name in TESTS:
        result = subprocess.run([sys.executable, str(ROOT / "analysis" / f"{name}.py")], cwd=ROOT, env=env,
                                capture_output=True, text=True, timeout=300)
        print(f"{'ok  ' if result.returncode == 0 else 'FAIL'} {name}")
        if result.returncode:
            failed.append(name)
            print(result.stdout[-2000:] + result.stderr[-2000:])
    sys.exit(1 if failed else 0)


if __name__ == "__main__":
    main()
