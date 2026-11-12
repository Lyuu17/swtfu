"""Supported TFU1 build: executable name, hash, code signature and gApp."""

EXE = "swtfu.exe"
APP_GLOBAL = 0x1240440  # gApp pointer
EXPECTED_HASH = "1c156705548fce9e33fa3423e75b5cbe36248319b96f4b4850bf2e8125692032"
CODE_SIGNATURE = bytes.fromhex("6a006a00e80702000083c408c21000cc")
