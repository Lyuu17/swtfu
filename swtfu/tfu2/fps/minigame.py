"""TFU2: keep the button-mash timing minigame at its 30 Hz pace at 60 updates/s.

MiniGameButtonMash_Update (0x986F80, vtable 0xFF56A4) runs once per update. Its
time limit already uses gGameplayDt, but three steps do not (all must scale
together or the balance of marker speed, fill and decay changes):
- marker box +0x60..+0x6C += velocity (SSE `addps xmm0,xmm1` at 0x987850);
- meter +0x28 += diff * [+0x30] while the marker overlaps the target (0x9878CA);
- meter -= diff * [+0x34] (0x98793E).
Each step is scaled by 30/cap. `[+0x3C] *= -1.0` (0x987872) is the marker
bounce, an event rather than a rate, so it is left alone.
Assumed to be the Gorog QTE (PCGamingWiki: buggy at high FPS); no "Gorog"/"QTE"
strings exist.
"""
from swtfu.patching import ShortCode

GAME_OBJECT = 0x13CB118
THIRTY = 0xF34AF8  # float 30.0
STRIDE = 96

SITES = (
    (0x987850, bytes.fromhex("0f58c10f29442450"), "mash_marker_step"),
    (0x9878CA, bytes.fromhex("d84e30d84628"), "mash_fill"),
    (0x98793E, bytes.fromhex("d84e34dee9"), "mash_decay"),
)
SITE_NAMES = tuple(site[2] for site in SITES)


def _cap_guard(code):
    """pushfd; push eax; eax = gGameObj; skip to 'done' if no object or cap 0."""
    code.emit("9c50a118b13c01")
    code.emit("85c0")
    code.jump("74", "done")
    code.emit("83b86402000000")                    # cmp dword [eax+264h],0
    code.jump("74", "done")


def marker_code():
    code = ShortCode()
    _cap_guard(code)
    code.emit("83ec10" "0f111424")                 # sub esp,16; movups [esp],xmm2
    code.emit("f30f1015f84af300")                  # movss xmm2,[30.0]
    code.emit("f30f5e9064020000")                  # divss xmm2,[eax+264h]
    code.emit("0fc6d200" "0f59c2")                 # shufps xmm2,xmm2,0; mulps xmm0,xmm2 (velocity * 30/cap)
    code.emit("0f101424" "83c410")                 # movups xmm2,[esp]; add esp,16
    code.label("done")
    code.emit("589d")                              # pop eax; popfd
    code.emit("0f58c1" "0f29442454")               # addps xmm0,xmm1; movaps [esp+54h],xmm0 (caller's [esp+50h])
    code.emit("c3")
    return code.finish(STRIDE)


def scaled_code(head, tail):
    code = ShortCode()
    code.emit(head)                                # replay fmul [esi+30h|34h]
    _cap_guard(code)
    code.emit("d80df84af300" "d8b064020000")       # fmul [30.0]; fdiv [eax+264h]
    code.label("done")
    code.emit("589d")                              # pop eax; popfd
    code.emit(tail)                                # fadd [esi+28h] / fsubp st(1),st
    code.emit("c3")
    return code.finish(STRIDE)


CODE = marker_code() + scaled_code("d84e30", "d84628") + scaled_code("d84e34", "dee9")
