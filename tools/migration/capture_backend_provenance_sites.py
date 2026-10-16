"""Audit retail coalescing probe sites; this does not instrument the compiler.

Independent development capture: run with capstone==5.0.9. The product tools
remain Rust. Only hashes, relocation positions and short probe guards are saved.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct

from capstone import Cs, CS_ARCH_X86, CS_MODE_32
from capstone.x86 import X86_INS_CALL

VERSIONS = ("GC_1_3", "GC_1_3_2", "GC_2_7")
SITES = {
    0x10F: "copy_flag_rejected",
    0x17B: "nonregister_destination_rejected",
    0x18B: "other_register_class_rejected",
    0x1D6: "roots_equal",
    0x244: "interference_rejected",
    0x271: "destination_below_window",
    0x280: "destination_above_window",
    0x289: "source_below_window",
    0x291: "source_above_window",
    0x2B9: "gpr_special_register_rejected",
    0x2C7: "merge_root_write",
}


def capture(root, version):
    config = json.loads((root / f"config/{version}/config.json").read_text())
    image = (root / config["original"]).read_bytes()
    assert hashlib.sha1(image).hexdigest() == config["sha1"]
    u16 = lambda at: struct.unpack_from("<H", image, at)[0]
    u32 = lambda at: struct.unpack_from("<I", image, at)[0]
    pe = u32(60)
    assert image[:2] == b"MZ" and image[pe:pe+4] == b"PE\0\0" and u16(pe+4) == 0x14C
    optional = pe + 24
    assert u16(optional) == 0x10B
    base = u32(optional + 28)
    section_at = optional + u16(pe+20)
    sections = [struct.unpack_from("<IIII", image, section_at + n*40 + 8) for n in range(u16(pe+6))]
    def offset(rva):
        for virtual_size, virtual_address, raw_size, raw_at in sections:
            if virtual_address <= rva < virtual_address + min(virtual_size, raw_size):
                return raw_at + rva - virtual_address
        raise ValueError(f"unbacked RVA {rva:x}")
    symbols = (root / f"config/{version}/symbols.txt").read_text()
    match = re.search(r"^_SpillCode_CoalesceCopies = \.text:0x([0-9A-Fa-f]+); // type:function size:0x([0-9A-Fa-f]+)", symbols, re.M)
    assert match
    address, size = (int(value, 16) for value in match.groups())
    assert size == 0x44A
    start = offset(address - base)
    code = image[start:start+size]
    normalized = bytearray(code)
    relocation_offsets = []
    rva, length = struct.unpack_from("<II", image, optional + 96 + 5*8)
    cursor = offset(rva)
    end = cursor + length
    while cursor < end:
        page, block_size = struct.unpack_from("<II", image, cursor)
        assert block_size >= 8 and block_size % 2 == 0 and cursor + block_size <= end
        for at in range(cursor+8, cursor+block_size, 2):
            entry = u16(at)
            relative = base + page + (entry & 0xFFF) - address
            if entry >> 12 == 3 and 0 <= relative < size:
                assert relative + 4 <= size
                normalized[relative:relative+4] = bytes(4)
                relocation_offsets.append(relative)
        cursor += block_size
    decoder = Cs(CS_ARCH_X86, CS_MODE_32)
    decoder.detail = True
    instructions = list(decoder.disasm(code, address))
    assert sum(instruction.size for instruction in instructions) == size
    sites = []
    external_calls = []
    for instruction in instructions:
        relative = instruction.address - address
        if instruction.id == X86_INS_CALL and instruction.bytes[0] == 0xE8:
            external_calls.append(dict(offset=relative, target=instruction.operands[0].imm))
            normalized[relative+1:relative+5] = bytes(4)
        if relative in SITES:
            sites.append(dict(role=SITES[relative], offset=relative, bytes=bytes(instruction.bytes).hex(),
                              instruction=instruction.mnemonic + " " + instruction.op_str))
    assert {site["offset"] for site in sites} == set(SITES)
    assert all(site["instruction"].startswith(("j", "mov")) for site in sites)
    return dict(version=version, compiler_sha1=config["sha1"], compiler_sha256=hashlib.sha256(image).hexdigest(),
                function="_SpillCode_CoalesceCopies", address=address, size=size,
                code_sha256=hashlib.sha256(code).hexdigest(), normalized_code_sha256=hashlib.sha256(normalized).hexdigest(),
                normalized_fields="PE HIGHLOW relocations and direct CALL displacements only",
                relocations=relocation_offsets, calls=external_calls, sites=sites, instrumented=False)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--oracle-root", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    rows = [capture(args.oracle_root, version) for version in VERSIONS]
    assert len({row["normalized_code_sha256"] for row in rows}) == 1, "retail control flow differs: audit separately"
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(dict(format_version=1, scope="coalescing-site audit; not coverage or instrumentation", versions=rows), indent=2)+"\n", encoding="utf-8")
    print("Three retail coalescers audited; normalized code matches; eleven candidate branch/write probe sites per version")


if __name__ == "__main__":
    main()
