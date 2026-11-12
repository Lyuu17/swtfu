"""TFU2: dispatch window messages while waiting for a movie (TFU1 movie_messages twin).

movie_wait_pump_msgs (sub_56B550) loops `while (this->busy[+0xB4]) {
PeekMessageA(&msg, 0, 0, 0, PM_NOREMOVE); Sleep(1); }`, so input, paint and
focus messages pile up undispatched for the whole movie. The 15-byte peek
block at 0x56B570 (the loop head) becomes `call helper` + NOPs. The helper
removes and dispatches up to 64 messages per tick. WM_QUIT is re-posted to the
thread instead of dispatched, so WinMain still sees it after the movie (the
game's own sub_570380 pump dispatches the same way, without TranslateMessage).
Registers the loop relies on (ebx=Sleep, edi=PeekMessageA, esi=this) are preserved.
"""
import struct

from swtfu.patching import ShortCode

PEEK_MESSAGE = 0xF1D80C
DISPATCH_MESSAGE = 0xF1D818
POST_THREAD_MESSAGE = 0xF1D814
GET_CURRENT_THREAD_ID = 0xF1D21C
STRIDE = 128

SITES = (
    (0x56B570, bytes.fromhex("6a006a006a006a008d4c241c51ffd7"), "movie_wait_pump"),
)
SITE_NAMES = ("movie_wait_pump",)


def iat_call(address):
    return "ff15" + struct.pack("<I", address).hex()


def helper_code():
    code = ShortCode()
    code.emit("83ec20")                           # sub esp,20h: MSG at [esp], budget at [esp+1Ch]
    code.emit("c744241c40000000")                 # mov dword [esp+1Ch],64
    code.label("next")
    code.emit("6a016a006a006a00" "8d442410" "50")  # PM_REMOVE,0,0,0; lea eax,[esp+10h]; push eax
    code.emit(iat_call(PEEK_MESSAGE))
    code.emit("85c0")
    code.jump("74", "done")                       # queue empty
    code.emit("837c240412")                       # message == WM_QUIT?
    code.jump("74", "quit")
    code.emit("54" + iat_call(DISPATCH_MESSAGE))   # push esp (lpMsg); DispatchMessageA
    code.emit("ff4c241c")                         # dec dword [esp+1Ch]
    code.jump("75", "next")
    code.jump("eb", "done")
    code.label("quit")                            # re-post WM_QUIT for WinMain's pump
    code.emit(iat_call(GET_CURRENT_THREAD_ID))
    code.emit("ff74240c" "ff74240c" "6a12" "50")  # lParam, wParam, WM_QUIT, thread id
    code.emit(iat_call(POST_THREAD_MESSAGE))
    code.label("done")
    code.emit("83c420c3")                         # add esp,20h; ret
    return code.finish(STRIDE)


CODE = helper_code()
