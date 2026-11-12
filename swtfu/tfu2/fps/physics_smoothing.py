"""TFU2: keep the 30 Hz timescale/physics smoothing duration at 60 updates/s.

sub_405180 moves gTimeScale (0x111D4C8) toward its target [this+0xE4] and
sub_405220 moves the physics scale [this+0x90] toward [this+0x8C]*gTimeScale,
each by at most a factor r per update ([this+0xE8] / [this+0x94]), x87 code.
Same logic as TFU1 GameLoop_FrameStep 0x41BD4D/0x41BDDD (see
swtfu/tfu1/fps/physics_smoothing.py). At 60 updates/s, sqrt(r) per update
keeps the per-second rate. Every read of r is a 6-byte x87 memory operand:
`fld`, `fmul` or `fdiv`; each becomes `call helper; nop`. The helper replays
the load, takes the root only when the cap is exactly 60 and 1 < r < inf,
then completes `fmul`/`fdiv` with the popping form.
"""
import struct

from swtfu.patching import ShortCode

GAME_OBJECT = 0x13CB118  # gGameObj; cap float at +0x264
STRIDE = 64

# (site, original bytes, name, kind, field offset)
SITES = (
    (0x4051BF, bytes.fromhex("d981e8000000"), "timescale_ratio_up_check", "fld", 0xE8),
    (0x4051CE, bytes.fromhex("d889e8000000"), "timescale_ratio_up", "fmul", 0xE8),
    (0x4051F6, bytes.fromhex("d981e8000000"), "timescale_ratio_down_check", "fld", 0xE8),
    (0x405205, bytes.fromhex("d8b1e8000000"), "timescale_ratio_down", "fdiv", 0xE8),
    (0x40526F, bytes.fromhex("d98194000000"), "physics_ratio_up_check", "fld", 0x94),
    (0x405280, bytes.fromhex("d98194000000"), "physics_ratio_up", "fld", 0x94),
    (0x40529D, bytes.fromhex("d98194000000"), "physics_ratio_down_check", "fld", 0x94),
    (0x4052B4, bytes.fromhex("d8b194000000"), "physics_ratio_down", "fdiv", 0x94),
)
SITE_NAMES = tuple(site[2] for site in SITES)


def helper_code(kind, offset):
    field = struct.pack("<I", offset)
    code = ShortCode()
    code.data.extend(b"\xd9\x81" + field)                      # fld dword [ecx+off] (replayed load)
    code.emit("9c50a1" + struct.pack("<I", GAME_OBJECT).hex())  # pushfd; push eax; mov eax,[gGameObj]
    code.emit("85c0")
    code.jump("74", "done")
    code.emit("81b86402000000007042")                          # cap exactly 60.0?
    code.jump("75", "done")
    code.data.extend(b"\x81\xb9" + field + struct.pack("<I", 0x3F800000))
    code.jump("76", "done")                                    # r <= 1: unchanged
    code.data.extend(b"\x81\xb9" + field + struct.pack("<I", 0x7F800000))
    code.jump("73", "done")                                    # inf, NaN, negative: unchanged
    code.emit("d9fa")                                          # fsqrt
    code.label("done")
    code.emit("589d")                                          # pop eax; popfd
    code.emit({"fld": "", "fmul": "dec9", "fdiv": "def9"}[kind])  # fmulp/fdivp st(1),st
    code.emit("c3")
    return code.finish(STRIDE)


CODE = b"".join(helper_code(kind, offset) for _, _, _, kind, offset in SITES)
