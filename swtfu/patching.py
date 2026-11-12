"""Safe runtime code patching for the running game.

FrozenThreads suspends every game thread and refuses to proceed while any
thread executes or will return into the bytes being replaced.
apply_code_patch swaps verified original bytes for a same-size replacement.
ShortCode assembles small x86 snippets with short (rel8) jumps.
"""
from bisect import bisect_right
import ctypes as C
from ctypes import wintypes as W
import struct
import time

class ThreadEntry(C.Structure):
    _fields_ = [("dwSize", W.DWORD), ("cntUsage", W.DWORD), ("th32ThreadID", W.DWORD),
                ("th32OwnerProcessID", W.DWORD), ("tpBasePri", W.LONG),
                ("tpDeltaPri", W.LONG), ("dwFlags", W.DWORD)]


class X86Context(C.Structure):
    _fields_ = [("ContextFlags", W.DWORD), ("DebugRegisters", W.DWORD * 6),
                ("FloatSave", C.c_ubyte * 112), ("Segments", W.DWORD * 4),
                ("Registers", W.DWORD * 6), ("Ebp", W.DWORD), ("Eip", W.DWORD),
                ("SegCs", W.DWORD), ("EFlags", W.DWORD), ("Esp", W.DWORD),
                ("SegSs", W.DWORD), ("ExtendedRegisters", C.c_ubyte * 512)]


class MemoryRegion(C.Structure):
    _fields_ = [("BaseAddress", C.c_void_p), ("AllocationBase", C.c_void_p),
                ("AllocationProtect", W.DWORD), ("RegionSize", C.c_size_t),
                ("State", W.DWORD), ("Protect", W.DWORD), ("Type", W.DWORD)]


class CodeBusy(RuntimeError):
    pass


class FrozenThreads:
    """Briefly stop target threads so no thread can execute a partial patch."""
    def __init__(self, game, address, size, ranges=None):
        self.game = game
        self.kernel = game.kernel
        self.handles = []
        self.ids = set()
        self.address, self.size = address, size
        self.ranges = tuple(ranges) if ranges is not None else None
        for name, args, result in [
            ("CreateToolhelp32Snapshot", [W.DWORD, W.DWORD], W.HANDLE),
            ("Thread32First", [W.HANDLE, C.POINTER(ThreadEntry)], W.BOOL),
            ("Thread32Next", [W.HANDLE, C.POINTER(ThreadEntry)], W.BOOL),
            ("OpenThread", [W.DWORD, W.BOOL, W.DWORD], W.HANDLE),
            ("SuspendThread", [W.HANDLE], W.DWORD),
            ("ResumeThread", [W.HANDLE], W.DWORD),
        ]:
            fn = getattr(self.kernel, name)
            fn.argtypes, fn.restype = args, result
        self.suspend = self.kernel.Wow64SuspendThread if C.sizeof(C.c_void_p) == 8 else self.kernel.SuspendThread
        self.suspend.argtypes, self.suspend.restype = [W.HANDLE], W.DWORD
        self.get_context = self.kernel.Wow64GetThreadContext if C.sizeof(C.c_void_p) == 8 else self.kernel.GetThreadContext
        self.get_context.argtypes, self.get_context.restype = [W.HANDLE, C.POINTER(X86Context)], W.BOOL
        self.kernel.VirtualQueryEx.argtypes = [W.HANDLE, C.c_void_p, C.POINTER(MemoryRegion), C.c_size_t]
        self.kernel.VirtualQueryEx.restype = C.c_size_t

    def check_quiet(self):
        # Suspending prevents partial execution, but is insufficient if a
        # thread is stopped inside old instructions or will return into them.
        # Conservatively scan the active committed stack region for return
        # addresses before replacing a function with different boundaries.
        merged = []
        for address, size in sorted(self.ranges if self.ranges is not None else ((self.address, self.size),)):
            if merged and address <= merged[-1][1]:
                merged[-1] = (merged[-1][0], max(merged[-1][1], address + size))
            else:
                merged.append((address, address + size))
        starts = tuple(low for low, _ in merged)
        def contains(value):
            index = bisect_right(starts, value) - 1
            return index >= 0 and value < merged[index][1]
        for handle in self.handles:
            context = X86Context()
            context.ContextFlags = 0x10001  # x86 CONTEXT_CONTROL
            if not self.get_context(handle, C.byref(context)):
                raise C.WinError(C.get_last_error())
            if contains(context.Eip) or not context.Esp:
                raise CodeBusy(f"Thread context EIP={context.Eip:#x}, ESP={context.Esp:#x}")
            region = MemoryRegion()
            if not self.kernel.VirtualQueryEx(self.game.handle, context.Esp, C.byref(region), C.sizeof(region)):
                raise C.WinError(C.get_last_error())
            size = (region.BaseAddress or 0) + region.RegionSize - context.Esp
            if region.State != 0x1000 or region.Protect & 0x101 or not 0 < size <= 8 * 1024 * 1024:
                raise RuntimeError("Cannot verify the active thread stack; no code changed")
            stack = self.game.read(context.Esp, size - size % 4)
            if any(contains(value) for (value,) in struct.iter_unpack("<I", stack)):
                raise CodeBusy()

    def thread_ids(self):
        snapshot = self.kernel.CreateToolhelp32Snapshot(4, 0)
        if snapshot == C.c_void_p(-1).value:
            raise C.WinError(C.get_last_error())
        try:
            entry = ThreadEntry()
            entry.dwSize = C.sizeof(entry)
            ok = self.kernel.Thread32First(snapshot, C.byref(entry))
            found = []
            while ok:
                if entry.th32OwnerProcessID == self.game.pid:
                    found.append(entry.th32ThreadID)
                ok = self.kernel.Thread32Next(snapshot, C.byref(entry))
            return found
        finally:
            self.kernel.CloseHandle(snapshot)

    def __enter__(self):
        deadline = time.monotonic() + 2
        try:
            for _ in range(8):
                pending = set(self.thread_ids()) - self.ids
                if not pending:
                    if not self.handles:
                        raise RuntimeError("Game exited before the renderer fix could be applied")
                    self.check_quiet()
                    return self
                for thread_id in pending:
                    if time.monotonic() > deadline:
                        raise RuntimeError("Could not suspend game threads promptly; no code changed")
                    handle = self.kernel.OpenThread(0xA, False, thread_id)
                    if not handle:
                        if C.get_last_error() == 87:  # Thread exited after the snapshot.
                            continue
                        raise C.WinError(C.get_last_error())
                    if self.suspend(handle) == 0xFFFFFFFF:
                        error = C.get_last_error()
                        self.kernel.CloseHandle(handle)
                        raise C.WinError(error)
                    self.handles.append(handle)
                    self.ids.add(thread_id)
            raise RuntimeError("Game threads did not settle; no code changed")
        except BaseException:
            self.resume()
            raise

    def resume(self):
        errors = []
        for handle in reversed(self.handles):
            if self.kernel.ResumeThread(handle) == 0xFFFFFFFF:
                errors.append(C.get_last_error())
            self.kernel.CloseHandle(handle)
        self.handles.clear()
        if errors:
            raise RuntimeError(f"Could not resume all game threads (errors {errors}); restart the game")

    def __exit__(self, kind, error, trace):
        self.resume()


def apply_code_patch(game, address, original, patched):
    if not original or len(original) != len(patched):
        raise RuntimeError("Invalid fixed-size runtime patch")
    current = game.read(address, len(original))
    if current == patched:
        return False
    if current != original:
        raise RuntimeError(f"Code at {address:#x} differs from the supported build; no code changed")
    kernel = game.kernel
    kernel.VirtualProtectEx.argtypes = [W.HANDLE, C.c_void_p, C.c_size_t, W.DWORD, C.POINTER(W.DWORD)]
    kernel.VirtualProtectEx.restype = W.BOOL
    kernel.FlushInstructionCache.argtypes = [W.HANDLE, C.c_void_p, C.c_size_t]
    kernel.FlushInstructionCache.restype = W.BOOL

    def write(data):
        buffer = C.create_string_buffer(data)
        count = C.c_size_t()
        if not kernel.WriteProcessMemory(game.handle, address, buffer, len(data), C.byref(count)):
            raise C.WinError(C.get_last_error())
        if count.value != len(data) or game.read(address, len(data)) != data:
            raise RuntimeError("Runtime code patch write did not verify")
        if not kernel.FlushInstructionCache(game.handle, address, len(data)):
            raise C.WinError(C.get_last_error())

    deadline = time.monotonic() + 5
    while True:
        try:
            with FrozenThreads(game, address, len(original)):
                if game.read(address, len(original)) != original:
                    raise RuntimeError("Code changed while preparing the patch; no code changed")
                protection = W.DWORD()
                if not kernel.VirtualProtectEx(game.handle, address, len(original), 0x40, C.byref(protection)):
                    raise C.WinError(C.get_last_error())
                try:
                    try:
                        write(patched)
                    except BaseException:
                        write(original)
                        raise
                finally:
                    ignored = W.DWORD()
                    if not kernel.VirtualProtectEx(game.handle, address, len(original), protection.value, C.byref(ignored)):
                        raise RuntimeError("Could not restore code-page protection; restart the game")
            return True
        except CodeBusy:
            if time.monotonic() >= deadline:
                raise RuntimeError("Code is currently in use; no code changed at this site. Retry the helper.") from None
            time.sleep(0.005)


class ShortCode:
    def __init__(self):
        self.data = bytearray()
        self.labels = {}
        self.jumps = []

    def emit(self, text):
        self.data.extend(bytes.fromhex(text))

    def label(self, name):
        self.labels[name] = len(self.data)

    def jump(self, opcode, label):
        self.emit(opcode + "00")
        self.jumps.append((len(self.data) - 1, label))

    def finish(self, size):
        for position, label in self.jumps:
            offset = self.labels[label] - position - 1
            if not -128 <= offset <= 127:
                raise RuntimeError("Runtime patch branch exceeds its encoding")
            self.data[position] = offset & 255
        if len(self.data) > size:
            raise RuntimeError("Runtime patch exceeds the verified function boundary")
        return bytes(self.data).ljust(size, b"\x90")
