"""Experimental 60 FPS patch for the supported Steam build of SWTFU.

Launch the game normally through Steam first, then run:
    python SWTFU60.py --apply
    python SWTFU60.py --inspect
    python SWTFU60.py --restore

Changes the running game's native setting and the investigated cinematic/DMM
timing sites. Restarting restores defaults. No executable, DLL, asset, or save
file is modified.
Runtime frame-pacing and gameplay validation is still pending.
"""
import argparse
import ctypes as C
from ctypes import wintypes as W
import hashlib
import json
from pathlib import Path
import struct

EXPECTED_HASH = "1c156705548fce9e33fa3423e75b5cbe36248319b96f4b4850bf2e8125692032"
CODE_SIGNATURE = bytes.fromhex("6a006a00e80702000083c408c21000cc")


class ProcessEntry(C.Structure):
    _fields_ = [("dwSize", W.DWORD), ("cntUsage", W.DWORD), ("th32ProcessID", W.DWORD),
                ("th32DefaultHeapID", C.c_size_t), ("th32ModuleID", W.DWORD),
                ("cntThreads", W.DWORD), ("th32ParentProcessID", W.DWORD),
                ("pcPriClassBase", W.LONG), ("dwFlags", W.DWORD), ("szExeFile", W.WCHAR * 260)]


class GameMemory:
    def __init__(self, pid, writable=False):
        self.kernel = C.WinDLL("kernel32", use_last_error=True)
        for name, args, result in [
            ("OpenProcess", [W.DWORD, W.BOOL, W.DWORD], W.HANDLE),
            ("CloseHandle", [W.HANDLE], W.BOOL),
            ("ReadProcessMemory", [W.HANDLE, C.c_void_p, C.c_void_p, C.c_size_t, C.POINTER(C.c_size_t)], W.BOOL),
            ("WriteProcessMemory", [W.HANDLE, C.c_void_p, C.c_void_p, C.c_size_t, C.POINTER(C.c_size_t)], W.BOOL),
            ("QueryFullProcessImageNameW", [W.HANDLE, W.DWORD, W.LPWSTR, C.POINTER(W.DWORD)], W.BOOL),
            ("GetProcessTimes", [W.HANDLE, C.POINTER(W.FILETIME), C.POINTER(W.FILETIME),
                                 C.POINTER(W.FILETIME), C.POINTER(W.FILETIME)], W.BOOL),
        ]:
            function = getattr(self.kernel, name)
            function.argtypes = args
            function.restype = result
        self.pid = pid
        self.handle = self.kernel.OpenProcess(0x1010 | (0x428 if writable else 0), False, pid)
        if not self.handle:
            raise C.WinError(C.get_last_error())
        try:
            times = [W.FILETIME() for _ in range(4)]
            if not self.kernel.GetProcessTimes(self.handle, *(C.byref(value) for value in times)):
                raise C.WinError(C.get_last_error())
            self.creation_time = (times[0].dwHighDateTime << 32) | times[0].dwLowDateTime
            name = C.create_unicode_buffer(32768)
            length = W.DWORD(len(name))
            if not self.kernel.QueryFullProcessImageNameW(self.handle, 0, name, C.byref(length)):
                raise C.WinError(C.get_last_error())
            self.path = Path(name.value)
            if hashlib.sha256(self.path.read_bytes()).hexdigest() != EXPECTED_HASH:
                raise RuntimeError("Unsupported executable hash; no changes made")
            if self.read(0x401000, 16) != CODE_SIGNATURE:
                raise RuntimeError("Game code is not initialized or differs from the supported build")
            self.context = self.u32(0x1240440)
            if self.context < 0x10000:
                raise RuntimeError("Game is not initialized yet; leave it at the main menu")
        except BaseException:
            self.close()
            raise

    def close(self):
        if self.handle:
            self.kernel.CloseHandle(self.handle)
            self.handle = None

    def read(self, address, size):
        data = C.create_string_buffer(size)
        count = C.c_size_t()
        if not self.kernel.ReadProcessMemory(self.handle, address, data, size, C.byref(count)):
            raise C.WinError(C.get_last_error())
        if count.value != size:
            raise RuntimeError("Incomplete process-memory read")
        return data.raw

    def u32(self, address):
        return struct.unpack("<I", self.read(address, 4))[0]

    def f32(self, address):
        return struct.unpack("<f", self.read(address, 4))[0]

    def inspect(self):
        from FrameRateFix import inspect_sites
        if self.u32(0x1240440) != self.context:
            raise RuntimeError("Game context changed; retry from the main menu")
        return {"pid": self.pid, "creation_time": self.creation_time,
                "image": str(self.path), "context": hex(self.context),
                "native_fps_limit": self.f32(self.context + 0x150),
                "simulation_timestep": self.f32(0x10D4008),
                "physics_target_timestep": self.f32(self.context + 0x6C),
                "physics_scaled_timestep": self.f32(self.context + 0x70),
                "game_focus_flag": bool(self.read(self.context + 0xF8, 1)[0]),
                "timing_mode": self.u32(0x124046C), "game_frame_counter": self.u32(0x1240464),
                "fps_profile_sites": inspect_sites(self)}

    def set_limit(self, expected, target):
        address = self.context + 0x150
        current = self.f32(address)
        if current == target:
            return False
        if current != expected or self.u32(0x1240440) != self.context:
            raise RuntimeError(f"Expected native limit {expected:g}, found {current:g}; no changes made")
        data = C.create_string_buffer(struct.pack("<f", target))
        written = C.c_size_t()
        if not self.kernel.WriteProcessMemory(self.handle, address, data, 4, C.byref(written)):
            raise C.WinError(C.get_last_error())
        if written.value != 4 or self.f32(address) != target:
            raise RuntimeError("Frame-limit write could not be verified; restart the game to reset it")
        return True


def find_game():
    kernel = C.WinDLL("kernel32", use_last_error=True)
    kernel.CreateToolhelp32Snapshot.argtypes = [W.DWORD, W.DWORD]
    kernel.CreateToolhelp32Snapshot.restype = W.HANDLE
    kernel.Process32FirstW.argtypes = [W.HANDLE, C.POINTER(ProcessEntry)]
    kernel.Process32FirstW.restype = W.BOOL
    kernel.Process32NextW.argtypes = kernel.Process32FirstW.argtypes
    kernel.Process32NextW.restype = W.BOOL
    kernel.CloseHandle.argtypes = [W.HANDLE]
    snapshot = kernel.CreateToolhelp32Snapshot(2, 0)
    if snapshot == C.c_void_p(-1).value:
        raise C.WinError(C.get_last_error())
    pids = []
    try:
        entry = ProcessEntry()
        entry.dwSize = C.sizeof(entry)
        ok = kernel.Process32FirstW(snapshot, C.byref(entry))
        while ok:
            if entry.szExeFile.lower() == "swtfu.exe":
                pids.append(entry.th32ProcessID)
            ok = kernel.Process32NextW(snapshot, C.byref(entry))
    finally:
        kernel.CloseHandle(snapshot)
    if len(pids) != 1:
        raise RuntimeError("Launch SWTFU through Steam first" if not pids else "Multiple games found; specify --pid")
    return pids[0]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    actions = parser.add_mutually_exclusive_group(required=True)
    actions.add_argument("--apply", action="store_true")
    actions.add_argument("--restore", action="store_true")
    actions.add_argument("--inspect", action="store_true")
    parser.add_argument("--pid", type=int)
    args = parser.parse_args()
    game = None
    try:
        game = GameMemory(args.pid or find_game(), writable=not args.inspect)
        before = game.inspect()
        changed = False
        from FrameRateFix import set_profile
        if args.apply:
            changed = set_profile(game, 60)
        elif args.restore:
            changed = set_profile(game, 30)
        result = {"status": "experimental; runtime validation pending", "changed": changed,
                  "before": before, "after": game.inspect()}
        print(json.dumps(result, indent=2))
    except (OSError, RuntimeError) as error:
        parser.exit(1, f"{error}\n")
    finally:
        if game:
            game.close()


if __name__ == "__main__":
    main()
