"""Preserve native 30 Hz ratio smoothing duration at the selected 60 Hz rate."""
import ctypes as C
from ctypes import wintypes as W
import struct
from RendererResetFix import apply_code_patch
from WindowedPlaybackFix import ShortCode

SITES = (
    (0x41BD4D, bytes.fromhex("f30f1096d4000000"), 0xD4),
    (0x41BDDD, bytes.fromhex("f30f105674"), 0x74),
)


def helper_code(offset):
    code = ShortCode()
    code.emit("9c")  # MOVSS did not alter flags; following JBE needs old CF/ZF.
    code.data.extend(b"\xf3\x0f\x10\x96" + struct.pack("<I", offset))
    code.emit("81be5001000000007042")  # native cap exactly 60
    code.jump("75", "done")
    code.data.extend(b"\x81\xbe" + struct.pack("<I", offset) + struct.pack("<I", 0x3F800000))
    code.jump("76", "done")          # preserve <=1, invalid/disabled smoothing
    code.data.extend(b"\x81\xbe" + struct.pack("<I", offset) + struct.pack("<I", 0x7F800000))
    code.jump("73", "done")          # preserve negative, infinity and NaN
    code.emit("f30f51d2")             # sqrt(r): two 60 Hz updates ~= one 30 Hz update
    code.label("done")
    code.emit("9dc3")
    return code.finish(64)


CODE = b"".join(helper_code(offset) for _, _, offset in SITES)


def hook(site, original, helper):
    return (b"\xe8" + struct.pack("<I", (helper - site - 5) & 0xFFFFFFFF)).ljust(len(original), b"\x90")


def validate(game):
    root = None
    currents = []
    for index, (site, original, _) in enumerate(SITES):
        current = game.read(site, len(original))
        currents.append(current)
        if current == original:
            continue
        if current[:1] != b"\xe8":
            raise RuntimeError(f"Unsupported smoothing code at {site:#x}")
        relative, = struct.unpack_from("<i", current, 1)
        candidate = ((site + 5 + relative) & 0xFFFFFFFF) - 64 * index
        if root is not None and candidate != root:
            raise RuntimeError("Smoothing hooks refer to different helpers")
        if current != hook(site, original, candidate + 64 * index) or game.read(candidate, len(CODE)) != CODE:
            raise RuntimeError("Unrecognized smoothing helper")
        root = candidate
    return root, currents


def inspect_sites(game):
    _, currents = validate(game)
    return {name: "30" if current == original else "60"
            for name, current, (_, original, _) in zip(
                ("timescale_smoothing", "physics_smoothing"), currents, SITES)}


def allocate_code(game):
    kernel = game.kernel
    kernel.VirtualAllocEx.argtypes = [W.HANDLE, C.c_void_p, C.c_size_t, W.DWORD, W.DWORD]
    kernel.VirtualAllocEx.restype = C.c_void_p
    kernel.VirtualFreeEx.argtypes = [W.HANDLE, C.c_void_p, C.c_size_t, W.DWORD]
    kernel.VirtualFreeEx.restype = W.BOOL
    address = kernel.VirtualAllocEx(game.handle, None, 4096, 0x3000, 0x04)
    if not address:
        raise C.WinError(C.get_last_error())
    try:
        if address > 0xFFFFFFFF:
            raise RuntimeError("Smoothing helper is outside the x86 address space")
        apply_code_patch(game, address, bytes(len(CODE)), CODE)
        old = W.DWORD()
        if not kernel.VirtualProtectEx(game.handle, address, 4096, 0x20, C.byref(old)):
            raise C.WinError(C.get_last_error())
        return address
    except BaseException:
        kernel.VirtualFreeEx(game.handle, address, 0, 0x8000)
        raise


def set_enabled(game, enabled):
    root, currents = validate(game)
    if enabled and root is None:
        root = allocate_code(game)
    changed = False
    for index, ((site, original, _), current) in enumerate(zip(SITES, currents)):
        replacement = hook(site, original, root + 64 * index) if enabled else original
        changed |= apply_code_patch(game, site, current, replacement)
    # Retain published pages until process exit: a thread may still be
    # returning from the helper when its call site has already been restored.
    return changed
