"""Supported-build windowed resize and background playback fixes.

Keep genuine focus notifications intact. Continue the native game loop when
unfocused, avoid focus-driven audio suspension, and balance native ShowCursor
calls so background updates cannot inflate its per-thread display counter.
Ignore minimized/duplicate client-size notifications that needlessly reset
movie textures. Genuine positive-size changes keep the original resize path.
"""
from RendererResetFix import apply_code_patch

RESIZE_ADDRESS = 0xB984A0
RESIZE_ORIGINAL = bytes.fromhex(
    "8b54240485d27f05ba010000008b44240885c07f05b801000000"
    "8941348981dc1e00008951308991d81e0000c681a401000001b001c20800"
    "cccccccccccccccc"
)
CURSOR_ADDRESS = 0x69DA00
CURSOR_ORIGINAL = bytes.fromhex(
    "568bf18b462c85c0745183f801744cff15404df300394618741d"
    "80be150d00000075386a01ff15444df300c705a0d00e01010000005ec3"
    "833da0d00e0100741b80be150d00000075126a00ff15444df300"
    "c705a0d00e01000000005ec3cccccc"
)
BACKGROUND_ADDRESS = 0x401704
BACKGROUND_ORIGINAL = bytes.fromhex(
    "e837a92700eb18bf020000008bce83ef01e81652020085ff75f2"
    "e8dda82700889ef8000000bf0401000080bef8000000007409"
)


class ShortCode:
    def __init__(self):
        self.data = bytearray()
        self.labels = {}
        self.jumps = []

    def emit(self, text):
        self.data.extend(bytes.fromhex(text))

    def label(self, name):
        self.labels[name] = len(self.data)

    def jump(self, opcode, label):
        self.emit(opcode + "00")
        self.jumps.append((len(self.data) - 1, label))

    def finish(self, size):
        for position, label in self.jumps:
            offset = self.labels[label] - position - 1
            if not -128 <= offset <= 127:
                raise RuntimeError("Runtime patch branch exceeds its encoding")
            self.data[position] = offset & 255
        if len(self.data) > size:
            raise RuntimeError("Runtime patch exceeds the verified function boundary")
        return bytes(self.data).ljust(size, b"\x90")


def resize_code():
    code = ShortCode()
    code.emit("8b54240483fa01")       # edx = width; width <= 1 means minimized/invalid
    code.jump("7e", "done")
    code.emit("8b44240883f801")       # eax = height
    code.jump("7e", "done")
    code.emit("395130")               # current width == width?
    code.jump("75", "resize")
    code.emit("394134")               # current height == height?
    code.jump("74", "done")
    code.label("resize")
    code.emit("8941348981dc1e00008951308991d81e0000c681a401000001")
    code.label("done")
    code.emit("b001c20800")
    return code.finish(len(RESIZE_ORIGINAL))


def cursor_code_v4():
    code = ShortCode()
    code.emit("568bf18b462c85c0")
    code.jump("74", "done")
    code.emit("83f801")
    code.jump("74", "done")
    code.emit("ff15404df300394618")    # real GetForegroundWindow, not a spoofed focus flag
    code.jump("74", "foreground")
    code.emit("80be150d000000")
    code.jump("75", "done")
    code.emit("833da0d00e0100")        # already shown? Never increment every background frame
    code.jump("75", "done")
    code.emit("6a01ff15444df300c605a0d00e0101")
    code.jump("eb", "done")
    code.label("foreground")
    code.emit("833da0d00e0100")
    code.jump("74", "done")
    code.emit("80be150d000000")
    code.jump("75", "done")
    code.emit("6a00ff15444df300c605a0d00e0100")
    code.label("done")
    code.emit("5ec3")
    return code.finish(len(CURSOR_ORIGINAL))


def cursor_code():
    code = ShortCode()
    code.emit("568bf1837e2c01")       # preserve native cursor modes 0/1
    code.jump("76", "done")
    code.emit("80be150d000000")
    code.jump("75", "done")
    code.emit("ff15404df300394618")
    code.jump("75", "background")
    # ShowCursor's counter belongs to the calling GUI thread. A shared native
    # flag cannot describe both the main message thread and movie worker.
    code.emit("6a00ff15444df30083f8ff")
    code.jump("7d", "hidden")
    code.emit("6a01ff15444df300")     # undo decrement if already hidden
    code.label("hidden")
    code.emit("c605a0d00e0100")
    code.jump("eb", "done")
    code.label("background")
    code.emit("6a01ff15444df30085c0")
    code.jump("7e", "shown")
    code.emit("6a00ff15444df300")     # undo increment if already visible
    code.label("shown")
    code.emit("c605a0d00e0101")
    code.label("done")
    code.emit("5ec3")
    return code.finish(len(CURSOR_ORIGINAL))


RESIZE_PATCHED = resize_code()
CURSOR_PATCHED = cursor_code()
CURSOR_V4 = cursor_code_v4()
background = bytearray(BACKGROUND_ORIGINAL)
background[0:5] = b"\x90" * 5                     # focus-gain audio resume, paired with loss below
background[0x40171E - BACKGROUND_ADDRESS:0x401723 - BACKGROUND_ADDRESS] = b"\x90" * 5
background[0x401735 - BACKGROUND_ADDRESS:0x401737 - BACKGROUND_ADDRESS] = b"\x90" * 2
BACKGROUND_PATCHED = bytes(background)

PATCHES = [
    ("resize_notifications", RESIZE_ADDRESS, RESIZE_ORIGINAL, RESIZE_PATCHED),
    ("balanced_cursor", CURSOR_ADDRESS, CURSOR_ORIGINAL, CURSOR_PATCHED),
    ("background_updates", BACKGROUND_ADDRESS, BACKGROUND_ORIGINAL, BACKGROUND_PATCHED),
]


def apply_playback_fixes(game):
    from MovieMessageFix import apply_movie_message_fix
    renderer = game.u32(0x13BB5D4)
    if not renderer or game.u32(renderer + 80) != 1:
        raise RuntimeError("Background playback fixes require native windowed mode")
    if game.u32(0x10ED0A0) not in (0, 1):
        raise RuntimeError("Unexpected native cursor state; no playback patches applied")
    for name, address, original, patched in PATCHES:
        allowed = (original, patched, CURSOR_V4) if address == CURSOR_ADDRESS else (original, patched)
        if game.read(address, len(original)) not in allowed:
            raise RuntimeError(f"Unsupported {name} code; no playback patches applied")
    # Enable the background loop last, after its cursor-count guard exists.
    result = {"movie_window_messages": apply_movie_message_fix(game)}
    result.update({name: apply_code_patch(game, address,
                   CURSOR_V4 if address == CURSOR_ADDRESS and game.read(address, len(original)) == CURSOR_V4 else original, patched)
                   for name, address, original, patched in PATCHES})
    return result
