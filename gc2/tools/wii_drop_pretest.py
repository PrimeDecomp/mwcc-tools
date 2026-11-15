#!/usr/bin/env python3
"""Delete the loop pretests the Prime games' Wii compiler leaves out, from compiled PowerPC objects.

A stand-in for the compiler change, to measure it (see ../../docs/wii-retro-compiler.md, "The destroy pretest").
A guard is deleted when it repeats the loop's own test and the loop body makes a call. In one executable section:
    i:    cmplw crN, rA, rB
    i+4:  beq   crN, X
    i+8:  b     T
          ...body: contains bl, bctrl or blrl...
    T:    cmplw crN, rA, rB     (the same instruction as at i)
    T+4:  bne   crN, body
    X = T+8
The pair at i and i+4 is deleted. Branch displacements, relocation offsets and addends, and symbol values and sizes
are fixed, and the section shrinks in place (the file layout is unchanged). Objects are rewritten in place.

usage: wii_drop_pretest.py [--nocall] OBJ...   (--nocall: also when the body makes no call)
"""

import struct
import sys

NOCALL = "--nocall" in sys.argv
files = [a for a in sys.argv[1:] if not a.startswith("--")]


def u32(b, o):
    return struct.unpack_from(">I", b, o)[0]


def sext(v, bits):
    return v - (1 << bits) if v & (1 << (bits - 1)) else v


def process(path):
    data = bytearray(open(path, "rb").read())
    if data[:4] != b"\x7fELF" or data[5] != 2:
        return 0
    shoff, = struct.unpack_from(">I", data, 0x20)
    shentsize, shnum, shstrndx = struct.unpack_from(">HHH", data, 0x2E)
    secs = []
    for k in range(shnum):
        name, typ, flags, addr, off, size, link, info, align, entsize = struct.unpack_from(">10I", data, shoff + k * shentsize)
        secs.append(dict(k=k, name=name, type=typ, flags=flags, off=off, size=size, link=link, info=info, entsize=entsize))
    symtab = next(s for s in secs if s["type"] == 2)

    def syms():
        for j in range(symtab["size"] // 16):
            o = symtab["off"] + j * 16
            name, value, size, info, other, shndx = struct.unpack_from(">IIIBBH", data, o)
            yield o, value, size, shndx

    rela = [s for s in secs if s["type"] == 4]
    total = 0
    for sec in secs:
        if not (sec["type"] == 1 and sec["flags"] & 4) or sec["size"] == 0:
            continue
        base, n = sec["off"], sec["size"]
        relocated = set()
        for r in rela:
            if r["info"] == sec["k"]:
                for j in range(r["size"] // 12):
                    relocated.add(u32(data, r["off"] + j * 12))
        ins = [u32(data, base + o) for o in range(0, n, 4)]

        def is_cmplw(w):
            return w >> 26 == 31 and (w >> 1) & 0x3FF == 32 and not (w >> 21) & 3 and not w & 1

        def bc(w):  # (BO without hint, BI, target index offset) or None
            if w >> 26 != 16 or w & 3:
                return None
            return (w >> 21) & 0x1E, (w >> 16) & 0x1F, sext(w & 0xFFFC, 16) // 4

        def is_call(idx, w):
            if w >> 26 == 18 and w & 1:
                return True
            return w in (0x4E800421, 0x4E800021)

        dele = []
        for i in range(len(ins) - 2):
            w = ins[i]
            if not is_cmplw(w):
                continue
            crf = (w >> 23) & 7
            b1 = bc(ins[i + 1])
            if not b1 or b1[0] != 12 or b1[1] != crf * 4 + 2:
                continue
            w2 = ins[i + 2]
            if w2 >> 26 != 18 or w2 & 3 or (i + 2) * 4 in relocated:
                continue
            t = i + 2 + sext(w2 & 0x3FFFFFC, 26) // 4
            if not (i + 3 <= t < len(ins) - 1) or ins[t] != w:
                continue
            b2 = bc(ins[t + 1])
            if not b2 or b2[0] != 4 or b2[1] != crf * 4 + 2 or (t + 1) * 4 in relocated:
                continue
            if i + 1 + b1[2] != t + 2 or (i + 1) * 4 in relocated:
                continue
            if not NOCALL and not any(is_call(j, ins[j]) for j in range(i + 3, t)):
                continue
            dele += [i, i + 1]
        if not dele:
            continue
        gone = set(dele)
        newidx, c = [], 0
        for j in range(len(ins) + 1):
            newidx.append(j - c)
            if j in gone:
                c += 1

        def mapoff(o):
            return newidx[o // 4] * 4 + (o % 4) if o < n else o - 4 * len(gone)

        out = []
        for j, w in enumerate(ins):
            if j in gone:
                continue
            if j * 4 not in relocated:
                if w >> 26 == 18 and not w & 2:
                    tgt = j + sext(w & 0x3FFFFFC, 26) // 4
                    d = (newidx[tgt] - newidx[j]) * 4
                    w = (w & ~0x3FFFFFC) | (d & 0x3FFFFFC)
                elif w >> 26 == 16 and not w & 2:
                    tgt = j + sext(w & 0xFFFC, 16) // 4
                    d = (newidx[tgt] - newidx[j]) * 4
                    assert -0x8000 <= d < 0x8000
                    w = (w & ~0xFFFC) | (d & 0xFFFC)
            out.append(w)
        new = b"".join(struct.pack(">I", w) for w in out)
        data[base : base + n] = new + bytes(n - len(new))
        struct.pack_into(">I", data, shoff + sec["k"] * shentsize + 0x14, len(new))
        old = list(syms())  # values before this section's change
        for o, value, size, shndx in old:
            if shndx == sec["k"]:
                nv = mapoff(value)
                ns = mapoff(value + size) - nv if size else 0
                struct.pack_into(">II", data, o + 4, nv, ns)
        # relocations against this section's code, and addends of relocations to symbols in it
        for r in rela:
            for j in range(r["size"] // 12):
                o = r["off"] + j * 12
                roff, rinfo, radd = struct.unpack_from(">IIi", data, o)
                if r["info"] == sec["k"]:
                    roff = mapoff(roff)
                _, value, _, shndx = old[rinfo >> 8]
                if shndx == sec["k"] and radd:
                    radd = mapoff(value + radd) - mapoff(value)
                struct.pack_into(">IIi", data, o, roff, rinfo, radd)
        total += len(gone) // 2
    if total:
        open(path, "wb").write(data)
    return total


grand = 0
for f in files:
    k = process(f)
    if k:
        print(f"{f}: {k}")
    grand += k
print(f"{grand} pretests deleted")
