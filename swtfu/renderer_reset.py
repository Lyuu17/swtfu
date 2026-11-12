"""Runtime fix for SWTFU's healthy-device resize/reset loop.

The original reset routine accepts D3DERR_DEVICENOTRESET but rejects D3D_OK
even when WM_SIZE explicitly requested a swap-chain resize. The replacement
accepts both, and preserves rejection of DEVICELOST and other errors.
It keeps the original resource-release, Reset, and resource-rebuild calls.

Only the supported hash-checked running game is changed. Restart restores
the original code.
"""

from swtfu.patching import apply_code_patch

ADDRESS = 0xB5DD90
ORIGINAL = bytes.fromhex(
    "568bf18b462c85c0745b80bea40100000074528b088b510c50ffd2"
    "3d69087688740433c05ec380bea50100000075078bcee8aaf9ffff"
    "8b462c8b088d563052508b4140ffd03d6c08768874d685c075d2"
    "8bcee8c997ffff8bcee8f2f8ffffc686a401000000b8010000005ec3cccccccc"
)
replacement = bytearray(ORIGINAL)
replacement[0x1B:0x26] = bytes.fromhex("85c074073d690876887546")
replacement[0x4A:0x4C] = bytes.fromhex("7420")
replacement[0x4E:0x50] = bytes.fromhex("751c")
replacement[0x6C:0x70] = bytes.fromhex("33c05ec3")
PATCHED = bytes(replacement)

def apply_fix(game):
    return apply_code_patch(game, ADDRESS, ORIGINAL, PATCHED)
