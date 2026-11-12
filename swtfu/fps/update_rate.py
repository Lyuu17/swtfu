"""Scale per-update amounts by 30 / cap so they keep their 30 Hz rate at 60.

Each site is a movss load of a per-update amount. It is replaced by a call to a
helper that replays the load and multiplies the register by 30 / cap
(x1 at 30 including cinematics, x0.5 at 60).
- Trail_AdvanceUVScroll (0x4677D0), Trail_SimStep (0x469280): UV offset (+0xE74)
  += mUV_Scroll_Speed (+0xE78) once per update, no frame time.
- ActionDirectLocomotionAI_StrafeNormalNav_Update (0x874A40): turn step capped
  at a fixed 5 degrees per update (0xFB9154) before sub_872830.
"""
import ctypes as C
from ctypes import wintypes as W
import struct
from swtfu.patching import apply_code_patch

# (site, original movss load, name, xmm register loaded). Flags and stack untouched.
SITES = (
    (0x46781D, bytes.fromhex("f30f108178"+"0e0000"), "trail_uv_scroll_advance", 0),
    (0x4692D2, bytes.fromhex("f30f108678"+"0e0000"), "trail_uv_scroll_simstep", 0),
    (0x874BBE, bytes.fromhex("f30f10155491fb00"), "ai_strafe_turn_step_cap", 2),
)
STRIDE = 64


def helper_code(original, xmm):
    code = (original                                   # replay the movss load
            + bytes.fromhex("9c50a140042401"           # pushfd; push eax; mov eax,[gApp]
                            "85c07419"                 # test eax,eax; jz done
                            "83b8500100000074" "10")   # cmp dword [eax+150h],0; je done
            + bytes((0xF3, 0x0F, 0x59, 0x05 + 8 * xmm)) + bytes.fromhex("101bf500")  # mulss xmmN,[kThirty 0xF51B10]
            + bytes((0xF3, 0x0F, 0x5E, 0x80 + 8 * xmm)) + bytes.fromhex("50010000")  # divss xmmN,[eax+150h] (cap)
            + bytes.fromhex("589dc3"))                 # done: pop eax; popfd; ret
    assert len(code) <= STRIDE
    return code.ljust(STRIDE, b"\xcc")


CODE = b"".join(helper_code(original, xmm) for _, original, _, xmm in SITES)


def hook(site, original, helper):
    return (b"\xe8" + struct.pack("<I", (helper - site - 5) & 0xFFFFFFFF)).ljust(len(original), b"\x90")


def validate(game):
    root, currents = None, []
    for index, (site, original, _, _) in enumerate(SITES):
        current = game.read(site, len(original)); currents.append(current)
        if current == original:
            continue
        if current[:1] != b"\xe8":
            raise RuntimeError(f"Unsupported per-update code at {site:#x}")
        candidate = ((site + 5 + struct.unpack_from("<i", current, 1)[0]) & 0xFFFFFFFF) - STRIDE * index
        if root is not None and candidate != root:
            raise RuntimeError("Per-update rate hooks refer to different helpers")
        if current != hook(site, original, candidate + STRIDE * index) or game.read(candidate, len(CODE)) != CODE:
            raise RuntimeError("Unrecognized per-update rate helper")
        root = candidate
    return root, currents


def inspect_sites(game):
    _, currents = validate(game)
    return {name: "30" if current == original else "60" for (_, original, name, _), current in zip(SITES, currents)}


def allocate_code(game):
    kernel = game.kernel
    kernel.VirtualAllocEx.argtypes = [W.HANDLE, C.c_void_p, C.c_size_t, W.DWORD, W.DWORD]
    kernel.VirtualAllocEx.restype = C.c_void_p
    kernel.VirtualFreeEx.argtypes = [W.HANDLE, C.c_void_p, C.c_size_t, W.DWORD]
    kernel.VirtualProtectEx.argtypes = [W.HANDLE, C.c_void_p, C.c_size_t, W.DWORD, C.POINTER(W.DWORD)]
    address = kernel.VirtualAllocEx(game.handle, None, 4096, 0x3000, 0x04)
    if not address:
        raise C.WinError(C.get_last_error())
    try:
        if address > 0xFFFFFFFF:
            raise RuntimeError("Per-update rate helper is outside the x86 address space")
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
    for index, ((site, original, _, _), current) in enumerate(zip(SITES, currents)):
        replacement = hook(site, original, root + STRIDE * index) if enabled else original
        changed |= apply_code_patch(game, site, current, replacement)
    # ponytail: helper page stays allocated until exit (in-flight returns), same as the other helpers
    return changed
