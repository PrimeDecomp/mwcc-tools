#!/usr/bin/env python3
"""python tools/rsrc2res.py EXE OUTPUT: the resources of an executable (its .rsrc section) as a 32-bit .res file, which
the linker takes as an input and writes the section from again."""
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from pe import PEFile


def resources(pe, section):
    """[(type, name, language, code page, data)] in the order the data lies; type and name are an int or a str."""
    base = section.virtual_address
    data = pe.data[section.file_offset:section.file_offset + section.file_size]

    def label(value):
        if value & 0x80000000:
            at = value & 0x7FFFFFFF
            length = struct.unpack_from("<H", data, at)[0]
            return data[at + 2:at + 2 + 2 * length].decode("utf-16-le")
        return value

    def walk(offset, path):
        named, ids = struct.unpack_from("<HH", data, offset + 12)
        for i in range(named + ids):
            name, target = struct.unpack_from("<II", data, offset + 16 + 8 * i)
            if target & 0x80000000:
                yield from walk(target & 0x7FFFFFFF, path + [label(name)])
            else:
                rva, size, page, _ = struct.unpack_from("<IIII", data, target)
                start = rva - (base - pe.image_base)
                yield (start, (*path, name, page, data[start:start + size]))

    # (the data lies in the order of the .res file the linker was given, the directory sorted by name)
    return [entry for _, entry in sorted(walk(0, []), key=lambda e: e[0])]


def encode(value):
    if isinstance(value, int):
        return struct.pack("<HH", 0xFFFF, value)
    return value.encode("utf-16-le") + b"\0\0"


def res_file(entries):
    out = bytearray()

    def add(kind, name, language, page, payload):
        head = encode(kind) + encode(name)
        head += bytes(-len(head) & 3)
        header_size = 8 + len(head) + 16
        out.extend(struct.pack("<II", len(payload), header_size))
        out.extend(head)
        out.extend(struct.pack("<IHHII", 0, 0x30, language, 0, 0))
        out.extend(payload)
        out.extend(bytes(-len(payload) & 3))

    add(0, 0, 0, 0, b"")
    for kind, name, language, page, payload in entries:
        add(kind, name, language, page, payload)
    return bytes(out)


def main():
    pe = PEFile(Path(sys.argv[1]))
    section = next(s for s in pe.sections if s.name == ".rsrc")
    Path(sys.argv[2]).write_bytes(res_file(resources(pe, section)))


if __name__ == "__main__":
    main()
