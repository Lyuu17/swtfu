"""Read-only SWTFU hang diagnostics. Never changes focus or game memory.

--capture saves a dump immediately. --watch waits for a previously responsive
game window to stop responding, then captures once. Files stay in analysis/hangs.
"""
import argparse
import ctypes as C
from ctypes import wintypes as W
from datetime import datetime, timezone
import json
from pathlib import Path
import subprocess
import time

from SWTFU60 import GameMemory, find_game

ROOT = Path(__file__).resolve().parent


class GuiThreadInfo(C.Structure):
    _fields_ = [("cbSize", W.DWORD), ("flags", W.DWORD),
                ("active", W.HWND), ("focus", W.HWND), ("capture", W.HWND),
                ("menu_owner", W.HWND), ("move_size", W.HWND), ("caret", W.HWND),
                ("caret_rect", W.RECT)]


def write_dump(pid, destination):
    temporary = destination.with_suffix(".dmp.part")
    try:
        # Match the game's architecture: a 64-bit writer captures native WOW64
        # transition contexts rather than the x86 stacks needed for this hang.
        helper = ROOT / "tools" / "Dump32.exe"
        result = subprocess.run([str(helper), str(pid), str(temporary)], capture_output=True,
                                text=True, timeout=30, creationflags=subprocess.CREATE_NO_WINDOW)
        if result.returncode:
            raise RuntimeError(f"Dump writer failed: {result.stderr.strip() or result.stdout.strip()}")
        temporary.replace(destination)
    except subprocess.TimeoutExpired as error:
        raise RuntimeError("Dump writer timed out; no completed dump saved") from error
    finally:
        if temporary.exists():
            temporary.unlink()


def window_api():
    user = C.WinDLL("user32", use_last_error=True)
    user.GetWindowThreadProcessId.argtypes = [W.HWND, C.POINTER(W.DWORD)]
    user.GetWindowThreadProcessId.restype = W.DWORD
    user.SendMessageTimeoutW.argtypes = [W.HWND, W.UINT, W.WPARAM, W.LPARAM,
                                        W.UINT, W.UINT, C.POINTER(C.c_size_t)]
    user.SendMessageTimeoutW.restype = C.c_ssize_t
    user.GetGUIThreadInfo.argtypes = [W.DWORD, C.POINTER(GuiThreadInfo)]
    user.GetGUIThreadInfo.restype = W.BOOL
    return user


def snapshot(game):
    result = {"pid": game.pid, "creation_time": game.creation_time,
              "utc": datetime.now(timezone.utc).isoformat(), "read_only": True}
    try:
        result["game"] = game.inspect()
        renderer = game.u32(0x13BB5D4)
        result["renderer"] = {"address": hex(renderer), "hwnd": game.u32(renderer + 408),
                              "windowed": game.u32(renderer + 80),
                              "reset_pending": game.read(renderer + 420, 1)[0],
                              "resources_released": game.read(renderer + 421, 1)[0]}
        native_input = game.u32(game.context)
        result["input"] = {"address": hex(native_input), "window": game.u32(native_input + 24),
                           "cursor_mode": game.u32(native_input + 44),
                           "mouse_acquired": game.read(native_input + 3316, 1)[0],
                           "cursor_suppressed": game.read(native_input + 3349, 1)[0],
                           "native_cursor_shown": game.u32(0x10ED0A0)}
        owner = W.DWORD()
        result["window_thread_id"] = window_api().GetWindowThreadProcessId(
            result["renderer"]["hwnd"], C.byref(owner))
        result["window_owner_pid"] = owner.value
        if result["window_thread_id"]:
            gui = GuiThreadInfo()
            gui.cbSize = C.sizeof(gui)
            if window_api().GetGUIThreadInfo(result["window_thread_id"], C.byref(gui)):
                result["gui"] = {"flags": hex(gui.flags), "active": gui.active, "focus": gui.focus,
                                 "capture": gui.capture, "menu_owner": gui.menu_owner,
                                 "move_size": gui.move_size,
                                 "in_menu_mode": bool(gui.flags & 0x1C),
                                 "in_move_size": bool(gui.flags & 2)}
            else:
                result["gui_query_error"] = C.get_last_error()
    except (OSError, RuntimeError) as error:
        result["snapshot_error"] = str(error)
    try:
        from MovieMessageFix import ADDRESS, WAIT_SITES, CODE, SLEEP_CODE, wait_hook
        hook = game.read(ADDRESS, 6)
        patch = {"sleep_hook": hook.hex(),
                 "worker_wait_hooks": {hex(site): game.read(site, 6).hex() for site in WAIT_SITES}}
        if hook[:1] == b"\xbe" and hook[-1:] == b"\x90":
            address = int.from_bytes(hook[1:5], "little")
            patch["helper_address"] = hex(address)
            patch["sleep_helper_verified"] = game.read(address, len(SLEEP_CODE)) == SLEEP_CODE
            patch["worker_wait_helper_verified"] = game.read(address, len(CODE)) == CODE
            patch["worker_wait_hooks_verified"] = all(
                game.read(site, 6) == wait_hook(site, address) for site in WAIT_SITES)
        result["movie_message_patch"] = patch
    except (OSError, RuntimeError) as error:
        result["movie_patch_query_error"] = str(error)
    return result


def capture(game, reason, output_root=None):
    base = Path(output_root) if output_root is not None else ROOT / "analysis" / "hangs"
    folder = base / (datetime.now(timezone.utc).strftime("%Y%m%d-%H%M%S-%f") + f"-{game.pid}")
    folder.mkdir(parents=True)
    metadata = snapshot(game)
    metadata["reason"] = reason
    metadata["dump_path"] = str(folder / "game.dmp")
    try:
        write_dump(game.pid, folder / "game.dmp")
        metadata["dump_bytes"] = (folder / "game.dmp").stat().st_size
        metadata["status"] = "captured"
    except (OSError, RuntimeError) as error:
        metadata["status"] = "dump failed"
        metadata["dump_error"] = str(error)
    (folder / "snapshot.json").write_text(json.dumps(metadata, indent=2), encoding="utf-8")
    print(f"Hang diagnostics saved: {folder}", flush=True)
    return metadata


def watch(game, seconds=600):
    user = window_api()
    renderer = game.u32(0x13BB5D4)
    hwnd = game.u32(renderer + 408)
    owner = W.DWORD()
    thread_id = user.GetWindowThreadProcessId(hwnd, C.byref(owner))
    if not thread_id or owner.value != game.pid:
        raise RuntimeError("Game window is not accessible; run the diagnostic helper in your normal terminal")
    game.kernel.GetExitCodeProcess.argtypes = [W.HANDLE, C.POINTER(W.DWORD)]
    game.kernel.GetExitCodeProcess.restype = W.BOOL
    deadline = time.monotonic() + seconds
    healthy_since = None
    blocked_since = None
    armed = False
    print(f"Patch applied. Watching for a hang for up to {seconds:g} seconds; you may minimize this terminal.", flush=True)
    print("If clicking back into the game freezes it, leave it running briefly for automatic capture.", flush=True)
    while time.monotonic() < deadline:
        code = W.DWORD()
        if not game.kernel.GetExitCodeProcess(game.handle, C.byref(code)):
            raise C.WinError(C.get_last_error())
        if code.value != 259:
            return {"status": "game exited", "armed": armed}
        current_owner = W.DWORD()
        if user.GetWindowThreadProcessId(hwnd, C.byref(current_owner)) != thread_id or current_owner.value != game.pid:
            return {"status": "game window changed", "armed": armed}
        result = C.c_size_t()
        responsive = bool(user.SendMessageTimeoutW(hwnd, 0, 0, 0, 3, 250, C.byref(result)))
        now = time.monotonic()
        if responsive:
            healthy_since = healthy_since or now
            blocked_since = None
            if now - healthy_since >= 2:
                armed = True
        else:
            healthy_since = None
            blocked_since = blocked_since or now
            limit = 5 if armed else 8
            if now - blocked_since >= limit:
                reason = ("Previously responsive window stopped processing WM_NULL for five seconds" if armed else
                          "Window never became responsive during eight seconds of startup monitoring")
                return capture(game, reason)
        time.sleep(0.5)
    return {"status": "watch timed out without capture", "armed": armed}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    actions = parser.add_mutually_exclusive_group(required=True)
    actions.add_argument("--capture", action="store_true")
    actions.add_argument("--watch", action="store_true")
    parser.add_argument("--pid", type=int)
    args = parser.parse_args()
    game = None
    try:
        game = GameMemory(args.pid or find_game())
        result = capture(game, "Manual hang capture") if args.capture else watch(game)
        print(json.dumps(result, indent=2))
    except KeyboardInterrupt:
        print("Diagnostic watch stopped; game state unchanged.")
    except (OSError, RuntimeError) as error:
        parser.exit(1, f"{error}\n")
    finally:
        if game:
            game.close()


if __name__ == "__main__":
    main()
