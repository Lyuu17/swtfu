"""Borderless fullscreen companion for the supported SWTFU Steam build.

Run locally with --apply (or --apply --fps60), --inspect, or --restore.
An exclusive-fullscreen game needs one normal close/relaunch. This helper
waits for that close, prepares native windowed mode, and launches via Steam.
It never activates the game window or changes the desktop display mode.
"""
import argparse
import ctypes as C
from ctypes import wintypes as W
import hashlib
import json
import os
from pathlib import Path
import re
import struct
import tempfile
import time
import xml.etree.ElementTree as ET

from SWTFU60 import GameMemory, find_game
from RendererResetFix import apply_fix
from WindowedPlaybackFix import apply_playback_fixes

ROOT = Path(__file__).resolve().parent
STATE = ROOT / "borderless-state.json"
AWL_HASH = "a1bb7219476245aa9a5ceac92448133f1a805dee947de6603f52a5e6f8de77fe"
KEY = "VideoWindowed"
ENTRY = re.compile(r'<s\b[^>]*\bid\s*=\s*([\"\'])VideoWindowed\1[^>]*>.*?</s\s*>', re.S)


class ModuleEntry(C.Structure):
    _fields_ = [("dwSize", W.DWORD), ("th32ModuleID", W.DWORD), ("th32ProcessID", W.DWORD),
                ("GlblcntUsage", W.DWORD), ("ProccntUsage", W.DWORD),
                ("modBaseAddr", C.c_void_p), ("modBaseSize", W.DWORD), ("hModule", W.HMODULE),
                ("szModule", W.WCHAR * 256), ("szExePath", W.WCHAR * 260)]


def module_base(game, name):
    k = game.kernel
    k.CreateToolhelp32Snapshot.argtypes = [W.DWORD, W.DWORD]
    k.CreateToolhelp32Snapshot.restype = W.HANDLE
    k.Module32FirstW.argtypes = [W.HANDLE, C.POINTER(ModuleEntry)]
    k.Module32FirstW.restype = W.BOOL
    k.Module32NextW.argtypes = k.Module32FirstW.argtypes
    k.Module32NextW.restype = W.BOOL
    snapshot = k.CreateToolhelp32Snapshot(0x18, game.pid)
    if snapshot == C.c_void_p(-1).value:
        raise C.WinError(C.get_last_error())
    try:
        entry = ModuleEntry()
        entry.dwSize = C.sizeof(entry)
        ok = k.Module32FirstW(snapshot, C.byref(entry))
        while ok:
            if entry.szModule.lower() == name.lower():
                return entry.modBaseAddr, Path(entry.szExePath)
            ok = k.Module32NextW(snapshot, C.byref(entry))
    finally:
        k.CloseHandle(snapshot)
    raise RuntimeError(f"{name} is not loaded")


def active_config(game):
    base, dll_path = module_base(game, "AWL_Release.dll")
    if hashlib.sha256(dll_path.read_bytes()).hexdigest() != AWL_HASH:
        raise RuntimeError("Unsupported AWL library; cannot identify the active configuration safely")

    def wide_string(offset):
        obj = game.read(base + offset, 28)
        length, capacity = struct.unpack_from("<II", obj, 20)
        if not 0 < length <= 200 or capacity < length:
            raise RuntimeError("Unexpected AWL configuration path metadata")
        pointer = struct.unpack_from("<I", obj, 4)[0] if capacity >= 8 else base + offset + 4
        text = game.read(pointer, 2 * length).decode("utf-16-le")
        if any(char in text for char in "\\/:\0") or text in (".", ".."):
            raise RuntimeError("Unexpected configuration folder name")
        return text

    company, title = wide_string(0x54D40 + 60), wide_string(0x54D40 + 88)
    if company != "Aspyr" or not title.startswith("Star Wars "):
        raise RuntimeError("Unexpected game configuration location")
    return Path(os.environ["LOCALAPPDATA"]) / company / title / "Config.xml"


def config_value(data):
    root = ET.fromstring(data)
    if root.tag != "r":
        raise RuntimeError("Unexpected configuration root")
    nodes = [node for node in root.findall("s") if node.get("id") == KEY]
    if len(nodes) > 1:
        raise RuntimeError("Duplicate VideoWindowed keys; configuration left unchanged")
    return nodes[0].text if nodes else None


def edit_config(data, value):
    old = config_value(data)
    if old == value:
        return data
    text = data.decode("utf-8")
    matches = list(ENTRY.finditer(text))
    if len(matches) != (0 if old is None else 1):
        raise RuntimeError("Unsupported VideoWindowed XML format; configuration left unchanged")
    if matches:
        match = matches[0]
        if value is None:
            line_start = text.rfind("\n", 0, match.start()) + 1
            line_end = text.find("\n", match.end())
            if (line_end >= 0 and not text[line_start:match.start()].strip()
                    and not text[match.end():line_end].strip()):
                text = text[:line_start] + text[line_end + 1:]
            else:
                text = text[:match.start()] + text[match.end():]
        else:
            entry = match.group()
            start, end = entry.index(">") + 1, entry.rindex("</")
            text = text[:match.start()] + entry[:start] + value + entry[end:] + text[match.end():]
    elif value is not None:
        close = text.rfind("</r>")
        if close < 0:
            raise RuntimeError("Unsupported configuration format")
        newline = "\r\n" if "\r\n" in text else "\n"
        text = text[:close] + f'\t<s id="{KEY}">{value}</s>{newline}' + text[close:]
    result = text.encode("utf-8")
    if config_value(result) != value:
        raise RuntimeError("Configuration edit did not verify")
    return result


def save_state(state):
    temp = STATE.with_suffix(".json.tmp")
    temp.write_text(json.dumps(state, indent=2), encoding="utf-8")
    temp.replace(STATE)


def write_config(path, data):
    path = path.resolve()
    allowed = (Path(os.environ["LOCALAPPDATA"]) / "Aspyr").resolve()
    if path.name != "Config.xml" or not path.is_relative_to(allowed):
        raise RuntimeError("Refusing to change a file outside the game's Aspyr configuration folder")
    ET.fromstring(data)
    temporary = None
    try:
        with tempfile.NamedTemporaryFile(dir=path.parent, prefix="Config.swtfu-", suffix=".tmp", delete=False) as stream:
            temporary = Path(stream.name)
            stream.write(data)
            stream.flush()
            os.fsync(stream.fileno())
        temporary.replace(path)
    finally:
        if temporary and temporary.exists():
            temporary.unlink()


def wait_for_exit(game):
    game.kernel.GetExitCodeProcess.argtypes = [W.HANDLE, C.POINTER(W.DWORD)]
    game.kernel.GetExitCodeProcess.restype = W.BOOL
    print("Close the game normally once. Waiting here; no need to keep it focused. Ctrl+C cancels.", flush=True)
    while True:
        code = W.DWORD()
        if not game.kernel.GetExitCodeProcess(game.handle, C.byref(code)):
            raise C.WinError(C.get_last_error())
        if code.value != 259:
            return
        time.sleep(0.5)


def prepare_config(path, state):
    original = path.read_bytes()
    value = config_value(original)
    if value is not None and value.strip() not in ("0", "1"):
        raise RuntimeError("Unexpected VideoWindowed value; configuration left unchanged")
    if "config" not in state:
        state["config"] = {"path": str(path), "original_value": config_value(original)}
        (ROOT / "Config.before-borderless.xml").write_bytes(original)
        save_state(state)
    elif Path(state["config"]["path"]).resolve() != path.resolve():
        raise RuntimeError("A different configuration already has a backup; restore it first")
    updated = edit_config(original, "1")
    if updated != original:
        write_config(path, updated)
    if config_value(path.read_bytes()) != "1":
        raise RuntimeError("Could not verify native windowed-mode configuration")


def game_display(game):
    renderer = game.u32(0x13BB5D4)
    if renderer < 0x10000:
        raise RuntimeError("Renderer is not initialized")
    return {"hwnd": game.u32(renderer + 408), "windowed": bool(game.u32(renderer + 80)),
            "device": game.u32(renderer + 44),
            "reset_pending": bool(game.read(renderer + 420, 1)[0]),
            "reset_resources_released": bool(game.read(renderer + 421, 1)[0]),
            "width": game.u32(renderer + 48), "height": game.u32(renderer + 52)}


class Windows:
    def __init__(self):
        self.user = C.WinDLL("user32", use_last_error=True)
        self.get_style = getattr(self.user, "GetWindowLongPtrW", self.user.GetWindowLongW)
        self.set_style = getattr(self.user, "SetWindowLongPtrW", self.user.SetWindowLongW)
        self.get_style.argtypes = [W.HWND, C.c_int]
        self.get_style.restype = C.c_ssize_t
        self.set_style.argtypes = [W.HWND, C.c_int, C.c_ssize_t]
        self.set_style.restype = C.c_ssize_t
        self.user.GetWindowThreadProcessId.argtypes = [W.HWND, C.POINTER(W.DWORD)]
        self.user.GetWindowRect.argtypes = [W.HWND, C.POINTER(W.RECT)]
        self.user.GetWindowRect.restype = W.BOOL
        self.user.SetWindowPos.argtypes = [W.HWND, W.HWND, C.c_int, C.c_int, C.c_int, C.c_int, W.UINT]
        self.user.SetWindowPos.restype = W.BOOL
        self.user.MonitorFromWindow.argtypes = [W.HWND, W.DWORD]
        self.user.MonitorFromWindow.restype = W.HANDLE
        self.user.GetMonitorInfoW.argtypes = [W.HANDLE, C.c_void_p]
        self.user.GetMonitorInfoW.restype = W.BOOL
        self.previous_dpi = None
        if hasattr(self.user, "SetThreadDpiAwarenessContext"):
            self.user.SetThreadDpiAwarenessContext.argtypes = [C.c_void_p]
            self.user.SetThreadDpiAwarenessContext.restype = C.c_void_p
            self.previous_dpi = self.user.SetThreadDpiAwarenessContext(C.c_void_p(-4))

    def close(self):
        if self.previous_dpi:
            self.user.SetThreadDpiAwarenessContext(self.previous_dpi)

    def verify_owner(self, hwnd, pid):
        owner = W.DWORD()
        if not self.user.GetWindowThreadProcessId(hwnd, C.byref(owner)) or owner.value != pid:
            raise RuntimeError("Game window is unavailable from this desktop; run this tool in your normal terminal")

    def rect(self, hwnd):
        rect = W.RECT()
        if not self.user.GetWindowRect(hwnd, C.byref(rect)):
            raise C.WinError(C.get_last_error())
        return [rect.left, rect.top, rect.right, rect.bottom]

    def monitor(self, hwnd):
        class MonitorInfo(C.Structure):
            _fields_ = [("cbSize", W.DWORD), ("rcMonitor", W.RECT), ("rcWork", W.RECT), ("dwFlags", W.DWORD)]
        info = MonitorInfo()
        info.cbSize = C.sizeof(info)
        monitor = self.user.MonitorFromWindow(hwnd, 2)
        if not monitor or not self.user.GetMonitorInfoW(monitor, C.byref(info)):
            raise C.WinError(C.get_last_error())
        r = info.rcMonitor
        return [r.left, r.top, r.right, r.bottom]

    def change(self, hwnd, style, exstyle, rect):
        topmost_changed = bool(self.get_style(hwnd, -20) & 8) != bool(exstyle & 8)
        for index, value in [(-16, style), (-20, exstyle)]:
            C.set_last_error(0)
            previous = self.set_style(hwnd, index, value)
            if not previous and C.get_last_error():
                raise C.WinError(C.get_last_error())
        left, top, right, bottom = rect
        # Preserve the existing Z order unless changing topmost status.
        # SWP_NOACTIVATE keeps keyboard focus on the user's current app.
        flags = 0x230 | (0 if topmost_changed else 4)
        order = -1 if exstyle & 8 else -2
        if not self.user.SetWindowPos(hwnd, W.HWND(order), left, top, right-left, bottom-top, flags):
            raise C.WinError(C.get_last_error())
        if self.get_style(hwnd, -16) & 0xFFFFFFFF != style & 0xFFFFFFFF:
            raise RuntimeError("Window-style change did not verify")
        if self.get_style(hwnd, -20) & 0xFFFFFFFF != exstyle & 0xFFFFFFFF:
            raise RuntimeError("Extended window-style change did not verify")


def apply_window(game, state):
    display = game_display(game)
    if not display["windowed"]:
        raise RuntimeError("D3D is still in exclusive fullscreen; native windowed mode requires a restart")
    win = Windows()
    try:
        hwnd = display["hwnd"]
        win.verify_owner(hwnd, game.pid)
        current = state.get("window")
        if (current and current["pid"] == game.pid and current["hwnd"] == hwnd
                and current.get("creation_time") == game.creation_time):
            original = current
        else:
            original = {"pid": game.pid, "creation_time": game.creation_time, "hwnd": hwnd,
                        "style": win.get_style(hwnd, -16) & 0xFFFFFFFF,
                        "exstyle": win.get_style(hwnd, -20) & 0xFFFFFFFF, "rect": win.rect(hwnd)}
            state["window"] = original
            save_state(state)
        target = win.monitor(hwnd)
        try:
            win.change(hwnd, (original["style"] & ~0x00CF0000) | 0x80000000,
                       original["exstyle"] & ~0x00020309, target)
            if win.rect(hwnd) != target:
                raise RuntimeError("Window rectangle does not match the monitor")
        except BaseException:
            win.change(hwnd, original["style"], original["exstyle"], original["rect"])
            raise
        return {"native_windowed": True, "monitor_rect": target, "window_rect": win.rect(hwnd),
                "focus_forced": False, "always_on_top": False}
    finally:
        win.close()


def launch_and_wait():
    os.startfile("steam://rungameid/32430")
    print("Waiting for the Steam game to initialize (up to 180 seconds).", flush=True)
    deadline = time.monotonic() + 180
    last_error = "Game not started"
    while time.monotonic() < deadline:
        game = None
        try:
            game = GameMemory(find_game())
            display = game_display(game)
            if display["hwnd"] and display["device"]:
                return game
            game.close()
            game = None
        except (OSError, RuntimeError) as error:
            last_error = str(error)
            if game:
                game.close()
        time.sleep(0.5)
    raise RuntimeError(f"Game did not initialize: {last_error}. Launch it through Steam and run this command again.")


def capture_background_timing(game):
    # Sampling is read-only and does not activate the game. The new background
    # loop makes ordinary update cadence observable while the terminal is open.
    time.sleep(0.25)
    before = game.inspect()
    simulation_before = game.f32(0x1240460)
    start = time.perf_counter()
    time.sleep(5)
    after = game.inspect()
    elapsed = time.perf_counter() - start
    frame_count = (after["game_frame_counter"] - before["game_frame_counter"]) & 0xFFFFFFFF
    return {"duration_seconds": elapsed, "game_updates": frame_count,
            "game_updates_per_second": frame_count / elapsed,
            "simulation_seconds": game.f32(0x1240460) - simulation_before,
            "before": before, "after": after,
            "note": "Game-loop telemetry only; not a GPU-present or physics regression test."}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    action = parser.add_mutually_exclusive_group(required=True)
    action.add_argument("--apply", action="store_true")
    action.add_argument("--restore", action="store_true")
    action.add_argument("--inspect", action="store_true")
    parser.add_argument("--pid", type=int)
    parser.add_argument("--fps60", action="store_true", help="also apply the 60 FPS candidate")
    parser.add_argument("--watch-hangs", action="store_true", help="capture one unresponsive-window hang automatically")
    args = parser.parse_args()
    if args.fps60 and not args.apply:
        parser.error("--fps60 requires --apply")
    if args.watch_hangs and not args.apply:
        parser.error("--watch-hangs requires --apply")
    game = None
    diagnostic = {"patch_profile": "native60-borderless-playback-v16",
                  "validation": "experimental; live rendering and gameplay checks pending"}
    try:
        state = json.loads(STATE.read_text(encoding="utf-8")) if STATE.exists() else {}
        if args.restore:
            if not state:
                print("No borderless changes recorded.")
                return
            try:
                game = GameMemory(args.pid or find_game())
            except RuntimeError as error:
                if "Launch SWTFU through Steam first" not in str(error):
                    raise
            if game:
                saved = state.get("window")
                if (saved and saved["pid"] == game.pid and saved["hwnd"] == game_display(game)["hwnd"]
                        and saved.get("creation_time") == game.creation_time):
                    win = Windows()
                    try:
                        win.verify_owner(saved["hwnd"], game.pid)
                        win.change(saved["hwnd"], saved["style"], saved["exstyle"], saved["rect"])
                    finally:
                        win.close()
                # AWL saves its cached XML when exiting. Restore the saved
                # key afterward so that the game cannot overwrite our undo.
                wait_for_exit(game)
            saved = state.get("config")
            if saved:
                path = Path(saved["path"])
                data = path.read_bytes()
                if config_value(data) not in ("1", saved["original_value"]):
                    raise RuntimeError("VideoWindowed was changed elsewhere; leaving it unchanged")
                write_config(path, edit_config(data, saved["original_value"]))
            STATE.unlink()
            print("Borderless configuration restored. Next launch uses the original display mode.")
            return
        try:
            game = GameMemory(args.pid or find_game())
        except RuntimeError as error:
            if args.apply and not args.pid and "Launch SWTFU through Steam first" in str(error):
                game = launch_and_wait()
            else:
                raise
        config = active_config(game)
        display = game_display(game)
        diagnostic.update({"pid": game.pid, "creation_time": game.creation_time,
                           "active_config": str(config), "display_before": display})
        if args.inspect:
            print(json.dumps({"pid": game.pid, "active_config": str(config), **display}, indent=2))
            return
        if not display["windowed"]:
            print("A restart into native windowed mode is required.", flush=True)
            wait_for_exit(game)
            game.close()
            game = None
            prepare_config(config, state)
            game = launch_and_wait()
        # Fix the healthy-device resize path before WM_SIZE is generated by
        # changing the border or the window's dimensions.
        writable = GameMemory(game.pid, writable=True)
        try:
            # First: launch movies are already playing; skippability is rechecked every frame.
            from ControllerFix import apply as apply_controller_fixes
            diagnostic["controller_fixes_changed"] = apply_controller_fixes(writable)
            diagnostic["playback_fixes_changed"] = apply_playback_fixes(writable)
            diagnostic["renderer_reset_fix_changed"] = apply_fix(writable)
            if args.fps60:
                from FrameRateFix import set_profile
                diagnostic["fps_limit_changed"] = set_profile(writable, 60)
                diagnostic["native_fps_limit"] = writable.inspect()["native_fps_limit"]
        finally:
            writable.close()
        result = apply_window(game, state)
        diagnostic.update(result)
        diagnostic["display_after"] = game_display(game)
        print("Applied. Recording five seconds of read-only timing; no need to focus the game.", flush=True)
        try:
            diagnostic["background_timing"] = capture_background_timing(game)
            diagnostic["display_after_capture"] = game_display(game)
        except (OSError, RuntimeError) as error:
            diagnostic["timing_capture_error"] = str(error)
        result.update({key: diagnostic[key] for key in ("playback_fixes_changed", "native_fps_limit", "background_timing")
                       if key in diagnostic})
        print(json.dumps(result, indent=2))
        print("Borderless applied. Runtime gameplay validation remains pending.")
        if args.watch_hangs:
            diagnostic["hang_watch"] = {"status": "watching", "watch_pid": os.getpid(), "duration_seconds": 600}
            (ROOT / "borderless-last-run.json").write_text(json.dumps(diagnostic, indent=2), encoding="utf-8")
            from HangDiagnostics import watch
            diagnostic["hang_watch"] = watch(game)
    except KeyboardInterrupt:
        parser.exit(1, "Cancelled. No forced game close was performed.\n")
    except (OSError, RuntimeError, ValueError, ET.ParseError) as error:
        diagnostic["error"] = str(error)
        parser.exit(1, f"{error}\n")
    finally:
        if args.apply:
            (ROOT / "borderless-last-run.json").write_text(json.dumps(diagnostic, indent=2), encoding="utf-8")
        if game:
            game.close()


if __name__ == "__main__":
    main()
