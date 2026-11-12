"""Identity of the supported TFU2 build (checked before any memory access)."""
EXE = "swtfu2.exe"
EXPECTED_HASH = "79874d834449c3ae82925d4ddec9f6cfc7f197ff02af456ebf942db3a2b65167"
# First 16 code bytes at 0x401000 once the SteamStub wrapper has decrypted them.
CODE_SIGNATURE = bytes.fromhex("6800004000e8f608000059c3cccccccc")
APP_GLOBAL = 0  # ponytail: unknown until the TFU2 frame loop is mapped; 0 = not read
