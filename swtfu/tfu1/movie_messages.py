"""Pump the window queue on the owning thread during synchronous movies.

The native movie worker renders independently, while its caller sleeps in
sub_415C80 without returning to WinMain's message pump. Preserve its sleep,
audio updates and worker lifetime; process a bounded batch of messages there.
"""
import struct

from swtfu.patching import ShortCode

ADDRESS = 0x415D41
ORIGINAL = bytes.fromhex("8b354049f300")  # mov esi, [LECSleep]


def message_sleep_code(refresh_cursor=True):
    code = ShortCode()
    code.emit("558bec83ec205356")  # frame, MSG storage, preserve ebx/esi
    code.emit("ff7508ff154049f30083c404")  # original cdecl LECSleep(milliseconds)
    code.emit("bb400000008d75e0")  # max 64 messages; esi = MSG*
    code.label("next")
    code.emit("6a016a006a006a0056ff15644df300")  # PeekMessageA(..., PM_REMOVE)
    code.emit("85c0")
    code.jump("74", "done")
    code.emit("837e0412")  # WM_QUIT: same termination function as WinMain
    code.jump("75", "filter")
    code.emit("ff7608ff150c40f30083c404")
    code.jump("eb", "done")
    code.label("filter")
    code.emit("817e0404010000")  # native WM_SYSKEYDOWN Alt/F10 filter
    code.jump("75", "dispatch")
    code.emit("837e0879")
    code.jump("74", "advance")
    code.emit("837e0812")
    code.jump("74", "advance")
    code.label("dispatch")
    code.emit("56ff15684df30056ff156c4df300")
    code.label("advance")
    code.emit("4b")
    code.jump("75", "next")
    code.label("done")
    if refresh_cursor:
        code.emit("8b0d4004240185c9")  # context -> native input, on owning GUI thread
        code.jump("74", "return")
        code.emit("8b0985c9")
        code.jump("74", "return")
        code.emit("b800da6900ffd0")
    code.label("return")
    code.emit("5e5bc9c3")  # cdecl: caller still removes its sleep argument
    return code.finish(160 if refresh_cursor else 128)


SLEEP_CODE = message_sleep_code()
WAIT_OFFSET = len(SLEEP_CODE)
WAIT_SITES = (0x40726D, 0x415CA6)
WAIT_ORIGINAL = bytes.fromhex("ff15b84bf300")


def message_wait_code(wait_offset=WAIT_OFFSET):
    code = ShortCode()
    code.emit("558bec538b5d0885db")  # preserve task id and nonvolatile registers
    code.jump("74", "finished")
    code.label("poll")
    code.emit("53ff15fc4af30083c40484c0")  # LECMultiprocFinished(task)
    code.jump("75", "finished")
    code.emit("6a0a")
    # Same allocation: relative call from this helper to message_sleep_code.
    displacement = -(wait_offset + len(code.data) + 5)
    code.data.extend(b"\xe8" + struct.pack("<i", displacement))
    code.emit("83c404")
    code.jump("eb", "poll")
    code.label("finished")
    # Retain the original synchronization/finalization call after polling.
    code.emit("53ff15b84bf30083c4045bc9c3")
    return code.finish(64)


WAIT_CODE = message_wait_code()
CODE = SLEEP_CODE + WAIT_CODE
V4_CODE = message_sleep_code(False) + message_wait_code(128)


def wait_hook(site, address, offset=WAIT_OFFSET):
    return b"\xe8" + struct.pack("<I", (address + offset - site - 5) & 0xFFFFFFFF) + b"\x90"
