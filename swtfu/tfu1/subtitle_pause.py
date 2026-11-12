"""Keep the current subtitle line across the pause menu (original game bug, any FPS).

Subtitles: ui.ShowSubtitle (sub_4DE860) -> UI_ShowSubtitleText (0x541A20) sets
strSubtitle/subtitleDuration on the visible "UIContract" element; the HUD page
then counts the duration on the UI clock. Opening the pause menu (Anark event
onPauseGame -> sub_53F650(1), pause byte 0x12A288D) hides it, and the line is
gone when the game resumes, although the dialogue resumes with it.

Fix (one helper page + one data page):
- show:  record text, duration and start time (KUSER_SHARED_DATA interrupt time).
- hide:  ui.HideSubtitle (sub_541AC0) forgets the line; while paused it is skipped
         (the dialogue engine hides lines when their audio stops, and pause stops it).
- pause: on sub_53F650(1) keep the remaining time; on (0) queue a re-show with
         the time left at pause (paused time excluded).
- frame: before UI_FrameUpdate in Frame_UpdateWorld (0x418B97), once unpaused,
         call UI_ShowSubtitleText(text, remaining) (same thread as Lua). Re-showing
         while the menu is up was tried and is the suspect of a game crash.
The code is position independent; DATA_SLOT holds the data page address.
"""
import struct

from swtfu.patching import ShortCode

SHOW_SITE, SHOW_ORIGINAL = 0x541A20, bytes.fromhex("568b35b043f300")      # push esi; mov esi,[_stricmp]
HIDE_SITE, HIDE_ORIGINAL = 0x541AC0, bytes.fromhex("a1f4bb0d01")          # mov eax,[off_10DBBF4]
PAUSE_SITE, PAUSE_ORIGINAL = 0x53F650, bytes.fromhex("8a4c2404a140042401")  # mov cl,[esp+4]; mov eax,[gApp]
FRAME_SITE, FRAME_ORIGINAL = 0x418B97, bytes.fromhex("e854fa1200")       # call UI_FrameUpdate (0x5485F0)
UI_FRAME_UPDATE = 0x5485F0
PAUSED_FLAG = 0x12A288D

STRIDE = 0xC0
SHOW, HIDE, PAUSE, FRAME = (STRIDE * i for i in range(4))  # entry offsets
DATA_SLOT = 4 * STRIDE
SIZE = DATA_SLOT + 4
# data page: +0 state (0 none, 1 shown, 3 re-show pending), +1 re-show on resume, +2 hides skipped while paused, +4 duration,
# +8 start (u64, 100 ns), +16 remaining, +20 scratch u64, +28 remaining at pause, +32 text[256]


def _push_ret(address):  # absolute jump back into the game
    return "68" + struct.pack("<I", address).hex() + "c3"


def _data(code):  # ebx = data page (call/pop, then load DATA_SLOT)
    code.emit("e800000000")
    pop = len(code.data)
    code.emit("5b8b9b" + struct.pack("<i", DATA_SLOT - pop).hex())


def _now(code, label):  # edx:eax = interrupt time (KSYSTEM_TIME, torn-read safe)
    code.label(label)
    code.emit("8b150c00fe7f" "a10800fe7f" "3b151000fe7f")
    code.jump("75", label)


def _entry(offset, build):
    code = ShortCode()
    code.data.extend(b"\x90" * offset)  # keeps _data's displacement page-relative
    build(code)
    return bytes(code.finish(offset + STRIDE))[offset:]


def _show(code):
    code.emit("60")
    _data(code)
    code.emit("8b742424" "8d7b20" "39fe")  # esi = text, edi = buffer; same pointer = re-show
    code.jump("74", "copied")
    code.emit("c6430100" "b9ff000000")  # a new line cancels the resume re-show
    code.label("copy")
    code.emit("acaa84c0")
    code.jump("74", "copied")
    code.jump("e2", "copy")
    code.emit("c60700")  # truncate long lines
    code.label("copied")
    code.emit("8b442428" "894304")  # duration
    _now(code, "now")
    code.emit("894308" "89530c" "c60301" "61")
    code.emit(SHOW_ORIGINAL.hex() + _push_ret(SHOW_SITE + len(SHOW_ORIGINAL)))


def _hide(code):
    code.emit("53")
    _data(code)
    code.emit("803d" + struct.pack("<I", PAUSED_FLAG).hex() + "00")
    code.jump("75", "paused")
    code.emit("66c7030000" "5b" + HIDE_ORIGINAL.hex() + _push_ret(HIDE_SITE + len(HIDE_ORIGINAL)))
    code.label("paused")  # time is frozen, so no line can end: a hide now is the pause itself
    code.emit("fe4302" "5bc3")  # count it (+2), keep the line


def _pause(code):
    code.emit("60")
    _data(code)
    code.emit("8a4c2424" "84c9")
    code.jump("74", "resume")
    code.emit("803b01")  # pausing: only a line that is still shown
    code.jump("75", "done")
    code.emit("807b0100")  # repeated pause event: keep the first remaining time
    code.jump("75", "done")
    _now(code, "now")
    code.emit("2b4308" "1b530c" "894314" "895318")  # elapsed (100 ns)
    code.emit("df6b14" "6895bfd633" "d80c24" "58")  # fild; fmul 1e-7
    code.emit("d86b04" "d95310")                    # remaining = duration - elapsed
    code.emit("68cdcccc3d" "d81c24" "dfe0" "59" "9e")  # fcomp 0.1
    code.jump("76", "expired")
    code.emit("8b4310" "89431c" "c6430101" "c60300")  # keep remaining for the resume re-show
    code.jump("eb", "done")
    code.label("expired")
    code.emit("c60300")
    code.jump("eb", "done")
    code.label("resume")
    code.emit("807b0100")
    code.jump("74", "done")
    code.emit("c6430100" "8b431c" "894310" "c60303")
    code.label("done")
    code.emit("61" + PAUSE_ORIGINAL.hex() + _push_ret(PAUSE_SITE + len(PAUSE_ORIGINAL)))


def _frame(code):
    code.emit("60")
    _data(code)
    code.emit("803b03")
    code.jump("75", "out")
    code.emit("803d" + struct.pack("<I", PAUSED_FLAG).hex() + "00")  # never while the menu is up
    code.jump("75", "out")
    code.emit("c60300" "ff7310" "8d4320" "50")  # UI_ShowSubtitleText(text, remaining); re-records via SHOW
    code.emit("b8" + struct.pack("<I", SHOW_SITE).hex() + "ffd0" "83c408")
    code.label("out")
    code.emit("61" + _push_ret(UI_FRAME_UPDATE))  # tail jump: caller's return address is on the stack


CODE = b"".join(_entry(o, f) for o, f in ((SHOW, _show), (HIDE, _hide), (PAUSE, _pause), (FRAME, _frame)))
assert len(CODE) == DATA_SLOT

SITES = (  # (name, address, original, entry offset, opcode)
    ("subtitle_show", SHOW_SITE, SHOW_ORIGINAL, SHOW, 0xE9),
    ("subtitle_hide", HIDE_SITE, HIDE_ORIGINAL, HIDE, 0xE9),
    ("subtitle_pause", PAUSE_SITE, PAUSE_ORIGINAL, PAUSE, 0xE9),
    ("subtitle_frame", FRAME_SITE, FRAME_ORIGINAL, FRAME, 0xE8),
)


def page(data_address):
    return CODE + struct.pack("<I", data_address)


if __name__ == "__main__":  # disassemble the helper for review
    import capstone
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    for name, _, _, offset, _ in SITES:
        print(f"-- {name}")
        for ins in md.disasm(CODE[offset:offset + STRIDE], 0x10000000 + offset):
            if ins.mnemonic == "nop":
                break
            print(f"  {ins.address:08x} {ins.mnemonic} {ins.op_str}")
