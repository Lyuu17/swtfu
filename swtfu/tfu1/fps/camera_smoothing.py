"""Preserve the native camera lerp response when the cap changes from 30 to 60."""
import ctypes as C
from ctypes import wintypes as W
import struct
from swtfu.patching import apply_code_patch, ShortCode

# Loads AFTER the native 0.15..0.99 clamp, BEFORE distance multiplication.
# MOVSS memory loads zero the upper lanes and leave EFLAGS unchanged.
SITES = (
    (0x455C85, bytes.fromhex("f30f10642450"), 4, 0x50),
    (0x455CAE, bytes.fromhex("f30f104c2450"), 1, 0x50),
    (0x45623D, bytes.fromhex("f30f10542430"), 2, 0x30),
    (0x4562A3, bytes.fromhex("f30f10442430"), 0, 0x30),
)
STRIDE = 128


def helper_code(register, stack_offset):
    code = ShortCode()
    # CALL added one stack word; replay the replaced load from its old slot.
    code.data.extend(bytes((0xF3, 0x0F, 0x10, 0x44 + register * 8, 0x24, stack_offset + 4)))
    code.emit("9c50a14004240185c0")  # pushfd; push eax; context; null guard
    code.jump("74", "done")
    code.emit("81b85001000000007042")  # cap exactly 60
    code.jump("75", "done")
    code.emit("83ec100f113c24680000803ff30f103c24")  # preserve xmm7; scalar one
    code.data.extend(bytes((0xF3, 0x0F, 0x5C, 0xF8 + register)))  # 1-alpha
    code.emit("f30f51ff")  # sqrt(1-alpha)
    code.data.extend(bytes((0xF3, 0x0F, 0x10, 0x04 + register * 8, 0x24)))  # alpha=1
    code.data.extend(bytes((0xF3, 0x0F, 0x5C, 0xC7 + register * 8)))  # 1-sqrt(1-alpha)
    code.emit("83c4040f103c2483c410")  # restore scratch
    code.label("done")
    code.emit("589dc3")
    return code.finish(STRIDE)


CODE = b"".join(helper_code(register, offset) for _, _, register, offset in SITES)


def hook(site, original, helper):
    return (b"\xe8" + struct.pack("<I", (helper-site-5) & 0xFFFFFFFF)).ljust(len(original), b"\x90")


def validate(game):
    root = None
    currents = []
    for index, (site, original, _, _) in enumerate(SITES):
        current = game.read(site, len(original))
        currents.append(current)
        if current == original:
            continue
        if current[:1] != b"\xe8":
            raise RuntimeError(f"Unsupported camera smoothing code at {site:#x}")
        relative, = struct.unpack_from("<i", current, 1)
        candidate = ((site+5+relative) & 0xFFFFFFFF) - STRIDE*index
        if root is not None and candidate != root:
            raise RuntimeError("Camera hooks refer to different helpers")
        if current != hook(site, original, candidate+STRIDE*index) or game.read(candidate, len(CODE)) != CODE:
            raise RuntimeError("Unrecognized camera smoothing helper")
        root = candidate
    return root, currents


def inspect_sites(game):
    _, currents = validate(game)
    names = ("camera_eye_xz", "camera_eye_y", "camera_target_xz", "camera_target_y")
    return {name: "30" if current == site[1] else "60" for name, current, site in zip(names, currents, SITES)}


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
            raise RuntimeError("Camera helper is outside the x86 address space")
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
    for index, (site, current) in enumerate(zip(SITES, currents)):
        address, original, _, _ = site
        replacement = hook(address, original, root+STRIDE*index) if enabled else original
        changed |= apply_code_patch(game, address, current, replacement)
    # Published pages live until process exit, allowing in-flight returns.
    return changed
