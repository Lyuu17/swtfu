"""TFU2: scale per-update amounts by 30 / cap so they keep their 30 Hz rate at 60.

x87 form of swtfu/tfu1/fps/update_rate.py. Each site is a 6-byte `fld` of a
per-update amount, replaced by `call helper; nop`. The helper replays the
load, then multiplies by 30.0 and divides by the cap ([gGameObj]+0x264).
- trail_accum_a (sub_A87870) / trail_accum_b (sub_A89590): trail accumulator
  +0x91C += step +0x920 once per call, no frame time (step from asset data,
  default 1.0 in sub_8C4680).
"""
from swtfu.patching import ShortCode

GAME_OBJECT = 0x13CB118
THIRTY = 0xF34AF8  # float 30.0 in .rdata
STRIDE = 64

SITES = (
    (0xA87885, bytes.fromhex("d98720090000"), "trail_step_a"),
    (0xA895E1, bytes.fromhex("d98320090000"), "trail_step_b"),
)
SITE_NAMES = tuple(site[2] for site in SITES)


def helper_code(original):
    code = ShortCode()
    code.data.extend(original)                                 # replay the fld
    code.emit("9c50a118b13c01")                                # pushfd; push eax; mov eax,[gGameObj]
    code.emit("85c0")
    code.jump("74", "done")
    code.emit("83b86402000000")                                # cmp dword [eax+264h],0
    code.jump("74", "done")
    code.emit("d80df84af300")                                  # fmul dword [30.0]
    code.emit("d8b064020000")                                  # fdiv dword [eax+264h]
    code.label("done")
    code.emit("589dc3")                                        # pop eax; popfd; ret
    return code.finish(STRIDE)


CODE = b"".join(helper_code(original) for _, original, _ in SITES)
