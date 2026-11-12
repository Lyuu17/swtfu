"""Controller reconnect and launch-movie skip (runtime only; game files unchanged).

Reconnect: sub_6A2940 polls XInput pad 0 every frame. When it fails, sub_6B8CD0
latches a DirectInput fallback flag ([[input+0xCDC]]) that is never cleared.
From then on the DirectInput branch runs after every successful XInput read
and overwrites it (zeros when the stale device cannot be acquired), so a
reconnected pad is read but ignored. Hook: after a successful XInput read,
return through the native epilogue instead of entering the fallback branch.
Controllers without XInput still use the DirectInput path unchanged.

Launch-movie skip: any key/mouse/pad press already requests a skip, but the
load-time movie runner sub_419080 marks movies skippable only after loading
finishes, and never while booting into the main menu (byte_10D4006). The launch
movies end about when that load finishes, so they never skip. Hook the runner's
per-frame SetSkippable call: while booting, every movie except the loading-screen
loop is skippable immediately. Level-load (story) movies keep the original rule.
"""
import struct


PAD_SITE = 0x6A2A80
PAD_ORIGINAL = bytes.fromhex("538bceffd2eb6d")  # push ebx; mov ecx,esi; call edx; jmp short 6A2AF4
PAD_EPILOGUE = 0x6A2C6C                          # pop edi/ebp/esi/ebx; add esp,84h; ret
SKIP_SITE = 0x4190E6
# mov ecx,[esp+1Ch] (v14 skippable); mov edx,[eax]; mov edx,[edx+48h]; push ecx; mov ecx,eax; call edx
SKIP_ORIGINAL = bytes.fromhex("8b4c241c8b108b5248518bc8ffd2")
SKIP_OFFSET = 64  # second helper in the same page


def pad_code(cave):
    # Replayed submit, original flag-clear return value, then native epilogue.
    head = bytes.fromhex("538bceffd28b86dc0c0000")
    return head + b"\xe9" + struct.pack("<i", PAD_EPILOGUE - (cave + len(head) + 5))


def skip_code(_cave):
    # Called with one extra return address: v14 at [esp+20h], v13 (loading-screen movie) at [esp+18h].
    return bytes.fromhex(
        "8b4c2420"            # mov ecx,[esp+20h]
        "803d06400d0100"      # cmp byte [10D4006h],0   ; booting into the main menu?
        "7409"                # jz go
        "807c241800"          # cmp byte [esp+18h],0    ; loading-screen loop stays as is
        "7502"                # jnz go
        "b101"                # mov cl,1                ; skippable now
        "8b108b5248518bc8ffd2"  # go: original SetSkippable call
        "c3")


def hooks(cave):
    return ((PAD_SITE, PAD_ORIGINAL, b"\xe9" + struct.pack("<i", cave - (PAD_SITE + 5)) + b"\x90\x90"),
            (SKIP_SITE, SKIP_ORIGINAL, b"\xe8" + struct.pack("<i", cave + SKIP_OFFSET - (SKIP_SITE + 5)) + b"\x90" * 9))


def page(cave):
    return pad_code(cave).ljust(SKIP_OFFSET, b"\xcc") + skip_code(cave + SKIP_OFFSET)
