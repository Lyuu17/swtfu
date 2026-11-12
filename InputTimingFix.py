"""Preserve nominal 30 Hz mouse response and angular filter duration at 60 Hz."""
import ctypes as C
from ctypes import wintypes as W
import struct
from RendererResetFix import apply_code_patch
from WindowedPlaybackFix import ShortCode

SITES = (
    (0xC34CE8, bytes.fromhex("e87392a6ff"), "mouse", 8),
    (0xC34EA8, bytes.fromhex("e8c390a6ff"), "mouse", 12),
    (0x7AEA90, bytes.fromhex("f30f109614560000"), "angular", 0x5614),
    (0x7AEB87, bytes.fromhex("f30f108608560000"), "angular", 0x5608),
)
SOURCES = ((0x69DF60,bytes.fromhex("8b81e00c0000d94008c3")),
           (0x69DF70,bytes.fromhex("8b81e00c0000d9400cc3")))
STRIDE = 128


def helper_code(kind, offset):
    code=ShortCode()
    if kind == "mouse":
        # Replay the complete tiny raw-axis accessor, excluding RET.
        code.data.extend(bytes.fromhex("8b81e00c0000d940")+bytes((offset,)))
    else:
        # Replay the replaced velocity load; selected alpha is already XMM3.
        register=2 if offset==0x5614 else 0
        code.data.extend(bytes((0xF3,0x0F,0x10,0x86+8*register))+struct.pack('<I',offset))
    code.emit("9c50a14004240185c0")
    code.jump("74","done")
    code.emit("81b85001000000007042")
    code.jump("75","done")
    if kind == "mouse":
        # Raw deltas are counts per accepted-update poll. Express them as
        # counts in a nominal 1/30 s interval before the original binning.
        code.emit("a10c400d0185c0")
        code.jump("74","fallback")
        code.emit("3d0000807f")
        code.jump("73","fallback")  # negative/NaN/infinite elapsed time
        # Divide stored nominal period by elapsed first. At nominal 30/60,
        # these float periods differ by exactly two, avoiding boundary drift
        # from first multiplying elapsed by a rounded float 30.
        code.emit("d905041bf500d8350c400d01dec9")
        code.jump("eb","done")
        code.label("fallback")
        code.emit("d8c0")  # unavailable clock: nominal 60/30 scale
    else:
        code.emit("660f7ed885c0")  # alpha bits, preserve unsupported values
        code.jump("74","done")
        code.emit("3d0000803f")
        code.jump("77","done")
        # Keep native filter/deadband decisions at their original cadence.
        # The held finite velocity is still integrated every render update.
        # Merely using sqrt response caused small inputs to be reset to zero
        # by the native 0.02 deadband on every half step.
        code.emit("f6056404240101")
        code.jump("74","done")
        code.emit("0f57db")  # odd frame: alpha=0, hold current velocity
    code.label("done")
    code.emit("589dc3")
    return code.finish(STRIDE)


CODE=b''.join(helper_code(kind,offset) for _,_,kind,offset in SITES)


def hook(site,original,helper):
    return (b'\xe8'+struct.pack('<I',(helper-site-5)&0xFFFFFFFF)).ljust(len(original),b'\x90')


def validate(game):
    for address,original in SOURCES:
        if game.read(address,len(original))!=original:
            raise RuntimeError(f"Unsupported raw mouse accessor at {address:#x}")
    root=None
    currents=[]
    for index,(site,original,_,_) in enumerate(SITES):
        current=game.read(site,len(original));currents.append(current)
        if current==original:
            continue
        if current[:1]!=b'\xe8':
            raise RuntimeError(f"Unsupported input timing code at {site:#x}")
        relative,=struct.unpack_from('<i',current,1)
        candidate=((site+5+relative)&0xFFFFFFFF)-STRIDE*index
        if root is not None and candidate!=root:
            raise RuntimeError('Input hooks refer to different helpers')
        if current!=hook(site,original,candidate+STRIDE*index) or game.read(candidate,len(CODE))!=CODE:
            raise RuntimeError('Unrecognized input timing helper')
        root=candidate
    return root,currents


def inspect_sites(game):
    _,currents=validate(game)
    names=('mouse_horizontal_timing','mouse_vertical_timing','angular_yaw_smoothing','angular_pitch_smoothing')
    return {name:'30' if current==site[1] else '60' for name,current,site in zip(names,currents,SITES)}


def allocate_code(game):
    kernel=game.kernel
    kernel.VirtualAllocEx.argtypes=[W.HANDLE,C.c_void_p,C.c_size_t,W.DWORD,W.DWORD]
    kernel.VirtualAllocEx.restype=C.c_void_p
    kernel.VirtualFreeEx.argtypes=[W.HANDLE,C.c_void_p,C.c_size_t,W.DWORD]
    kernel.VirtualFreeEx.restype=W.BOOL
    address=kernel.VirtualAllocEx(game.handle,None,4096,0x3000,0x04)
    if not address:
        raise C.WinError(C.get_last_error())
    try:
        if address>0xFFFFFFFF:
            raise RuntimeError('Input helper is outside the x86 address space')
        apply_code_patch(game,address,bytes(len(CODE)),CODE)
        old=W.DWORD()
        if not kernel.VirtualProtectEx(game.handle,address,4096,0x20,C.byref(old)):
            raise C.WinError(C.get_last_error())
        return address
    except BaseException:
        kernel.VirtualFreeEx(game.handle,address,0,0x8000)
        raise


def set_enabled(game,enabled):
    root,currents=validate(game)
    if enabled and root is None:
        root=allocate_code(game)
    changed=False
    for index,(site,current) in enumerate(zip(SITES,currents)):
        address,original,_,_=site
        replacement=hook(address,original,root+STRIDE*index) if enabled else original
        changed|=apply_code_patch(game,address,current,replacement)
    # Published pages stay allocated until process exit for in-flight returns.
    return changed
