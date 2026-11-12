"""TFU2: keep per-update camera smoothing at its 30 Hz response at 60 updates/s.

Both steps are inside camera behaviour updates (TFUCameraBehaviorFollow_Update
0xA00580, virtual Update(dt) at vtable slot 0xFFBA94) but ignore dt:
- camera_smooth_fixed_alpha (sub_9DC0C0): step = (target - current) * alpha,
  alpha from [ecx+20h] or [ecx+24h]; both paths meet at 0x9DC1FF
  (`fstp [esp+8]`). At 60 use 1 - sqrt(1 - alpha), as TFU1's camera_smoothing.
- ForceGrip_DecayStep_0p6 (0x9EF190, called from the follow camera): value
  +0x9C *= 0.6 per update (0x9EF1A4 `fmul qword [0xF92850]`). At 60 use sqrt(0.6).
Both only change when the cap is exactly 60 and the value is in range.
"""
from swtfu.patching import ShortCode

GAME_OBJECT = 0x13CB118
STRIDE = 80

SITES = (
    (0x9DC1FF, bytes.fromhex("d95c24080f57d2"), "camera_fixed_alpha"),
    (0x9EF1A4, bytes.fromhex("dc0d5028f900"), "camera_grip_decay"),
)
SITE_NAMES = tuple(site[2] for site in SITES)


def _cap60(code):
    """pushfd; push eax; continue only if [gGameObj]+0x264 == 60.0, else jump 'done'."""
    code.emit("9c50a118b13c01")
    code.emit("85c0")
    code.jump("74", "done")
    code.emit("81b86402000000007042")
    code.jump("75", "done")


def alpha_code():
    # st0 = alpha. At 60: alpha' = 1 - sqrt(1 - alpha) for 0 < alpha < 1.
    code = ShortCode()
    _cap60(code)
    code.emit("83ec04" "d91424" "58")              # sub esp,4; fst [esp]; pop eax (alpha bits)
    code.emit("85c0")
    code.jump("7e", "done")                        # <= 0 (or negative bits): unchanged
    code.emit("3d0000803f")
    code.jump("73", "done")                        # >= 1.0, inf, NaN: unchanged
    code.emit("d9e8" "dee1" "d9fa" "d9e8" "dee1")  # fld1; fsubrp; fsqrt; fld1; fsubrp
    code.label("done")
    code.emit("589d")                              # pop eax; popfd
    code.emit("d95c240c")                          # fstp [esp+0Ch] (caller's [esp+8])
    code.emit("0f57d2")                            # xorps xmm2,xmm2 (replayed)
    code.emit("c3")
    return code.finish(STRIDE)


def decay_code():
    code = ShortCode()
    code.emit("dd055028f900")                      # fld qword [0.6]
    _cap60(code)
    code.emit("d9fa")                              # fsqrt
    code.label("done")
    code.emit("589d")
    code.emit("dec9")                              # fmulp st(1),st
    code.emit("c3")
    return code.finish(STRIDE)


CODE = alpha_code() + decay_code()
