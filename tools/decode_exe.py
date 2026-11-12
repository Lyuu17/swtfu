"""Decrypt a SteamStub-wrapped SWTFU/SWTFU2 executable into an image for IDA.

Never modifies the input. Both games use the same wrapper; the stub's header
address and XOR key are read from the entry-point code, so one decoder serves
both. The output is an analysis artifact, never a game patch.

Usage: python tools/decode_exe.py <SWTFU.exe|SWTFU2.exe> <output.exe>
Requires pefile and pycryptodome.
"""
import hashlib
import struct
import sys
from pathlib import Path

import pefile
from Crypto.Cipher import AES

MASK = 0xFFFFFFFF
# Entry stub: `mov dword ptr [ebp-88h], <header VA>` ... `mov dword ptr [ebp-42Ch], <xor key>`.
STUB = bytes.fromhex("5351525657558bec81ec00100000c78578ffffff")
KEY_OPCODE = bytes.fromhex("c785d4fbffff")


def xor_chain(data, key, preserve_first=False):
    result = bytearray(data[:4] if preserve_first else b"")
    for (word,) in struct.iter_unpack("<I", data[4:] if preserve_first else data):
        result.extend(struct.pack("<I", word ^ key))
        key = word
    return result


def xtea_cbc_decrypt(encrypted, key):
    previous = (0x55555555, 0x55555555)
    decoded = bytearray()
    for cipher in struct.iter_unpack("<2I", encrypted):
        left, right = cipher
        total = (32 * 0x9E3779B9) & MASK
        for _ in range(32):
            right = (right - (((((left << 4) ^ (left >> 5)) + left) & MASK)
                              ^ ((total + key[(total >> 11) & 3]) & MASK))) & MASK
            total = (total - 0x9E3779B9) & MASK
            left = (left - (((((right << 4) ^ (right >> 5)) + right) & MASK)
                            ^ ((total + key[total & 3]) & MASK))) & MASK
        decoded.extend(struct.pack("<2I", left ^ previous[0], right ^ previous[1]))
        previous = cipher
    return decoded


def loader_fields(loader):
    """Payload offsets the loader's `start` export copies into its globals, in order:
    flags, ?, original entry, code VA, code size, then `lea` AES key, `add` stolen prefix."""
    import capstone
    lp = pefile.PE(data=loader)
    start = next(e.address for e in lp.DIRECTORY_ENTRY_EXPORT.symbols if e.name == b"start")
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    md.detail = True
    found = []
    for insn in md.disasm(lp.get_memory_mapped_image()[start:start + 0x400], start):
        if insn.mnemonic in ("mov", "lea") and len(insn.operands) == 2 and insn.operands[1].type == capstone.x86.X86_OP_MEM                 and insn.reg_name(insn.operands[1].mem.base) == "eax" and insn.operands[1].mem.disp >= 0x100:
            found.append(insn.operands[1].mem.disp)
        elif insn.mnemonic == "add" and insn.op_str.startswith("eax, 0x") and found:
            found.append(insn.operands[1].imm)
            break
    if len(found) != 7:
        raise SystemExit(f"Unrecognized loader start code (fields {found}); no output written")
    return found[2], found[3], found[4], found[5], found[6]


def decode(source):
    pe = pefile.PE(data=source)
    base = pe.OPTIONAL_HEADER.ImageBase
    entry = pe.get_data(pe.OPTIONAL_HEADER.AddressOfEntryPoint, 0x60)
    if not entry.startswith(STUB):
        raise SystemExit("Entry point is not the known SteamStub wrapper; no output written")
    header_va = struct.unpack_from("<I", entry, len(STUB))[0]
    key_at = entry.index(KEY_OPCODE) + len(KEY_OPCODE)
    xor_key = struct.unpack_from("<I", entry, key_at)[0]
    word = lambda data, offset: struct.unpack_from("<I", data, offset)[0]
    header = xor_chain(pe.get_data(header_va - base, 0x364), xor_key)
    payload = xor_chain(pe.get_data(word(header, 0x24) - base, word(header, 0x28)), word(header, 0x20), True)
    # Header words 0x3C/0x40/0x44: payload offsets of the loader VA, its size and its XTEA key.
    loader = xtea_cbc_decrypt(pe.get_data(word(payload, word(header, 0x3C)) - base, word(payload, word(header, 0x40))),
                              struct.unpack_from("<4I", payload, word(header, 0x44)))
    if loader[:2] != b"MZ":
        raise SystemExit("Decoded loader is not a PE; no output written")
    entry_field, code_va_field, size_field, key_field, prefix_field = loader_fields(bytes(loader))
    # An ECB-encrypted IV, one stolen ciphertext block, then the on-disk code ciphertext.
    code_size, code_va = word(payload, size_field), word(payload, code_va_field)
    aes_key = bytes(payload[key_field:key_field + 32])
    stream = bytes(payload[prefix_field:prefix_field + 32]) + pe.get_data(code_va - base, code_size)
    iv = AES.new(aes_key, AES.MODE_ECB).decrypt(stream[:16])
    padded = AES.new(aes_key, AES.MODE_CBC, iv).decrypt(stream[16:])
    if padded[-16:] != bytes([16]) * 16:
        raise SystemExit("Code decryption padding check failed; no output written")
    plain = padded[:-16]
    assert len(plain) == code_size
    image = bytearray(source)
    offset = pe.get_offset_from_rva(code_va - base)
    image[offset:offset + code_size] = plain
    struct.pack_into("<I", image, pe.OPTIONAL_HEADER.get_field_absolute_offset("AddressOfEntryPoint"),
                     word(payload, entry_field) - base)  # original entry, for IDA
    return bytes(image), code_va, code_size, word(payload, entry_field)


def main():
    source_path, output_path = Path(sys.argv[1]), Path(sys.argv[2])
    source = source_path.read_bytes()
    image, code_va, code_size, oep = decode(source)
    output_path.write_bytes(image)
    print(f"source sha256 {hashlib.sha256(source).hexdigest()} (unchanged)")
    print(f"decoded code {code_va:#x}+{code_size:#x}, original entry {oep:#x} -> {output_path}")


if __name__ == "__main__":
    main()
