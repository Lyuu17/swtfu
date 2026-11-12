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
import argparse
import ctypes as C
from ctypes import wintypes as W
import struct

from swtfu.patching import apply_code_patch
from swtfu.game import GameMemory, find_game

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


def installed_cave(game):
    current = game.read(PAD_SITE, len(PAD_ORIGINAL))
    skip = game.read(SKIP_SITE, len(SKIP_ORIGINAL))
    if current == PAD_ORIGINAL and skip == SKIP_ORIGINAL:
        return None
    cave = (PAD_SITE + 5 + struct.unpack_from("<i", current, 1)[0]) & 0xFFFFFFFF
    if (current[:1] != b"\xe9" or [game.read(a, len(o)) for a, o, _ in hooks(cave)] != [p for _, _, p in hooks(cave)]
            or game.read(cave, len(page(cave))) != page(cave)):
        raise RuntimeError("Unsupported controller/movie code; no code changed")
    return cave


def allocate_cave(game):
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
            raise RuntimeError("Controller helper is outside the x86 address space")
        code = page(address)
        apply_code_patch(game, address, bytes(len(code)), code)
        old = W.DWORD()
        if not kernel.VirtualProtectEx(game.handle, address, 4096, 0x20, C.byref(old)):
            raise C.WinError(C.get_last_error())
        return address
    except BaseException:
        kernel.VirtualFreeEx(game.handle, address, 0, 0x8000)
        raise


def apply(game):
    """Install both hooks; returns True if anything changed. Idempotent."""
    if installed_cave(game) is not None:
        return False
    cave = allocate_cave(game)  # ponytail: page stays allocated until exit; returns may still be in flight
    for address, original, patched in hooks(cave):
        apply_code_patch(game, address, original, patched)
    return True


def inspect(game):
    return {"controller_reconnect_and_launch_skip": installed_cave(game) is not None}


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--inspect", action="store_true")
    parser.add_argument("--pid", type=int)
    args = parser.parse_args()
    game = GameMemory(args.pid or find_game(), writable=not args.inspect)
    try:
        if not args.inspect:
            print("Changed." if apply(game) else "Already applied.")
        print(inspect(game))
    finally:
        game.close()


if __name__ == "__main__":
    main()
