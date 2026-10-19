#!/usr/bin/env python3
"""python tools/equivalent.py VERSION [--debug]: link the equivalent compiler, build/VERSION/mwcceppc.exe: the sources'
objects, with the original's machine code standing in for every function that is not exact yet and the original's
data for what the sources do not define yet. It behaves as the original does (tools/equivalence.py tests that), and it
is made of the sources as far as they go. ninja runs it (ninja build/VERSION/mwcceppc.exe). With --debug, the objects
compiled with symbols on, and CodeView: build/VERSION/debug/mwcceppc.exe.

What is linked:
- each source's object (config/sources.json), with the functions whose rows are not exact (build/VERSION/compare.json)
  taken out: their symbols left undefined, for the lifted code to define;
- lifted code: the original's bytes of those functions (and of code no row names), each address in them a relocation
  against what is at that address in this link: a function, a variable a source defines, an import, or the image;
- the image: the original's .data, .rdata and .bss whole, each address in it relocated the same way. The variables the
  sources declare and do not define are defined in it at their addresses (config/VERSION/bindings.json), and so are
  those they declare COMMON at an address the bindings give, so the lifted code and the sources share them;
- the original's resources (tools/split.py's), the linker's runtime objects and import libraries with thunks, with the
  linker the original was linked with.

A name the sources refer to and nothing defines is an error, naming who refers to it, unless the reference is in code
the link never runs: a function whose row is lifted, or one the original does not have (its linker dropped it). Such a
function's first instruction becomes ud2, so that it traps if it runs after all.

python tools/equivalent.py VERSION --source-only: the compiler made of the sources alone,
build/VERSION/source-only/mwcceppc.exe: every function the sources' (an inexact one as it compiles), no lifted code
and no image, so the sources must define every variable the code that runs refers to (an error names each one they
do not); its resources are src/driver/mwcceppc.rc's (llvm-rc, build/VERSION/resources.res). What it lacks of the original's is what a build from source alone would lack: run tools/equivalence.py on it. A derived version
(config.json's derived_from, such as GC_2_0p2) is linked only this way, its rows and bindings its base's.

python tools/equivalent.py where VERSION ADDRESS...: the function of the equivalent compiler an address (a crash) is
in, from its link map, and that function in the original.
"""

from __future__ import annotations

import bisect
import collections
import re
import shutil
import struct
import sys
from dataclasses import dataclass, field
from pathlib import Path

import capstone

sys.path.insert(0, str(Path(__file__).parent))
from mw import build, coff, config, link, project, verdict
from mw.coff import DIR32, REL32, Object
from mw.names import c_name, is_mangled
from mw.pe import original

# (the image: the original's data sections, and the symbol each is reached through)
IMAGE = {".data": "__original_data", ".rdata": "__original_rdata", ".bss": "__original_bss"}
LICENSING = project.ROOT / "tools/lmgr_stub/lmgr326b.dll"
ALIGN_16 = 0x00500000
# (an object holds at most 65535 relocations a section: the lifted code is split well before)
RELOCATIONS_PER_OBJECT = 30000
UD2 = b"\x0f\x0b"
# (a gap between rows this long is code no row names, lifted whole; a shorter one is padding or an import thunk)
GAP = 16
SHORT_BRANCHES = {0xEB, *range(0x70, 0x80), *range(0xE0, 0xE4)}


def output_path(version, debug=False, source_only=False):
    folder = "debug/" if debug else "source-only/" if source_only else ""
    return project.version(version).build / f"{folder}mwcceppc.exe"


@dataclass
class Piece:
    """A run of the original's code linked as it is: a lifted row (or code no row names), with what it takes in."""

    start: int
    end: int
    symbol: str
    names: dict[str, int] = field(default_factory=dict)  # {symbol: address} it defines (its own first)
    fallthrough: int | None = None  # the address its last instruction falls into


class Errors(project.Error):
    pass


class Equivalent:
    def __init__(self, version, debug=False, source_only=False):
        self.version = v = project.version(version)
        if v.base and not source_only:
            raise project.Error(f"{v} is derived from {v.base}, with no original: it links only --source-only")
        # (what is read of an original: a derived version's base's)
        data = project.version(v.base) if v.base else v
        self.debug = debug
        self.source_only = source_only
        self.pe = original(data)
        self.fixups = self.pe.base_relocations
        self.rows = sorted(config.functions(data), key=lambda r: r.address)
        self.bindings = config.bindings(data)
        results = verdict.load(data) if not source_only else {}
        self.lifted_rows = [
            r for r in self.rows if not source_only and not (results.get(r.address) and results[r.address].exact)
        ]
        self.objects = {}
        for c in build.compilations(v):
            path = build.object_path(v, c.source, debug=True) if debug else None
            if path is None or not path.exists():
                path = build.object_path(v, c.source)
            self.objects[c.source] = Object.read(path)
        self.place_data({} if source_only else verdict.load_placements(v))
        self.imports = link.imports(data)
        self.slots = {item.slot: "__imp_" + item.symbol for item in self.imports}
        # (the functions imported: the source-only build's from src/imports, the others' from the original)
        self.thunks = link.defined_imports() if source_only else {item.symbol for item in self.imports}
        # (the linker names of __stdcall functions: _name@bytes, from the bindings and the objects)
        self.decorated = {}
        for name in [*self.bindings, *(s.name for obj in self.objects.values() for s in obj.symbols)]:
            if m := re.fullmatch(r"(?:__imp_)?(_[A-Za-z_]\w*)@(\d+)", name):
                self.decorated.setdefault(m[1], f"{m[1]}@{m[2]}")
        self.row_symbols = {r.address: self.symbol_of(r) for r in self.rows}
        self.row_starts = [r.address for r in self.rows]
        self.sections = {s.name: s for s in self.pe.sections}
        self.data_names = collections.defaultdict(list)
        for name, address in sorted(self.bindings.items()):
            if not name.startswith("__imp_"):
                self.data_names[address].append(name)
        self.bound = sorted(self.data_names)
        self.errors = []

    def place_data(self, placements):
        """The sources' data an exact function places in the original (the comparison's placements), each given a
        name (_at_ADDRESS) where it is: the lifted code and the image reach it there and not at a copy of their own.
        self.placed: [(address, end, name)], by address."""
        self.placed, seen = [], set()
        for source, places in sorted(placements.items()):
            obj = self.objects[source]
            normal = Object.read(build.object_path(self.version, source)) if self.debug else obj
            for number, offset, size, address in places:
                section = equivalent_section(normal, obj, number)
                if section is None or address in seen:
                    continue
                seen.add(address)
                name = f"_at_{address:08x}"
                obj.add_symbol(name, offset, section, 0, coff.EXTERNAL)
                self.placed.append((address, address + size, name))
        self.placed.sort()
        self.placed_starts = [p[0] for p in self.placed]

    # ------------------------------------------------------------------------------------------------ names

    def symbol_of(self, row):
        """A row's linker name."""
        if is_mangled(row.name):
            return row.name
        return self.decorated.get("_" + row.name, "_" + row.name)

    def section_of(self, address):
        try:
            return self.pe.section_for_address(address)
        except ValueError:
            return None

    def row_at(self, address):
        i = bisect.bisect_right(self.row_starts, address) - 1
        if i >= 0 and address < self.rows[i].end:
            return self.rows[i]
        return None

    def target(self, address):
        """(symbol, addend) of what is at ADDRESS in this link, or None."""
        if address in self.slots:
            return self.slots[address], 0
        section = self.section_of(address)
        if section is None:
            # (the address just past a section's end: a pointer to the end of an array)
            found = self.target(address - 1) if self.section_of(address - 1) else None
            return (found[0], found[1] + 1) if found else None
        if section.code:
            if address in self.row_symbols:
                return self.row_symbols[address], 0
            i = bisect.bisect_right(self.piece_starts, address) - 1
            if i >= 0 and address < self.pieces[i].end:
                return self.pieces[i].symbol, address - self.pieces[i].start
            row = self.row_at(address)
            if row is not None:
                return self.row_symbols[row.address], address - row.address
            # (an import thunk, jmp [slot])
            code = self.pe.read(address, 6)
            if code[:2] == b"\xff\x25" and struct.unpack_from("<I", code, 2)[0] in self.slots:
                return self.slots[struct.unpack_from("<I", code, 2)[0]][len("__imp_") :], 0
            return None
        # (data a source defines that an exact function places)
        i = bisect.bisect_right(self.placed_starts, address) - 1
        if i >= 0 and address < self.placed[i][1]:
            return self.placed[i][2], address - self.placed[i][0]
        # (a variable a source defines: by its name, or inside it, by its name and the offset)
        i = bisect.bisect_right(self.bound, address) - 1
        if i >= 0:
            start = self.bound[i]
            for name in self.data_names[start]:
                if name in self.extents and address - start < max(self.extents[name], 1):
                    return name, address - start
        if section.name in IMAGE:
            return IMAGE[section.name], address - section.virtual_address
        return None

    # ------------------------------------------------------------------------------------------------ objects

    def prepare(self):
        """The sources' objects, ready to link: the lifted rows' functions taken out, duplicates resolved, COMMONs
        the image places made references."""
        lifted = {self.symbol_of(r) for r in self.lifted_rows}
        row_names = set(self.row_symbols.values())
        source_of = {self.symbol_of(r): r.source for r in self.rows}
        image_sections = {self.sections[name] for name in IMAGE}
        # (a function or variable two objects define, outside a COMDAT: the copy of the row's source)
        definers = collections.defaultdict(list)
        for source, obj in self.objects.items():
            comdat = comdat_sections(obj)
            for s in obj.symbols:
                if s.external and s.defined and s.section not in comdat:
                    definers[s.name].append(source)
        keep = {}
        for name, sources in definers.items():
            if len(sources) > 1:
                if source_of.get(name) in sources:
                    keep[name] = source_of[name]
                else:
                    self.errors.append(f"{name} is defined by {', '.join(sources)}: which is the original's?")
        # {source: [(section, offset, name, dead)]}: each function's place, and whether the link never runs it
        self.functions = {}
        self.common_placed = {}
        self.extents = {}
        for source, obj in self.objects.items():
            self.functions[source] = sorted(
                (s.section, s.value, s.name, s.name in lifted or s.name not in row_names)
                for s in obj.symbols
                if s.defined and s.function and s.storage in (coff.EXTERNAL, coff.STATIC)
            )
            for s in obj.symbols:
                if s.external and s.defined and (s.name in lifted or keep.get(s.name, source) != source):
                    # (the lifted code defines it, or another object's copy is the original's)
                    s.section, s.value = 0, 0
                elif (
                    s.storage == coff.STATIC
                    and s.defined
                    and s.function
                    and s.name in lifted
                    and source_of.get(s.name) == source
                ):
                    # (a static function of a lifted row: its object's calls are the lifted code's)
                    s.section, s.value, s.storage = 0, 0, coff.EXTERNAL
                elif s.common and s.name in self.bindings and not self.source_only:
                    section = self.section_of(self.bindings[s.name])
                    if section in image_sections:
                        # (an uninitialized variable with a place in the original: the image's, at that place)
                        self.common_placed[s.name] = max(self.common_placed.get(s.name, 0), s.value)
                        s.value = 0
            self.extents.update(data_extents(obj, self.bindings))
            for number in comdat_sections(obj):
                mark_any(obj, number)

    def undefined(self, objects):
        defined, referenced = set(), collections.defaultdict(set)
        for where, obj in objects:
            for s in obj.symbols:
                if s.external and (s.defined or s.common):
                    defined.add(s.name)
                elif s.undefined:
                    referenced[s.name].add(where)
        return {name: where for name, where in referenced.items() if name not in defined}

    def export_statics(self, wanted):
        """The static functions something refers to by name (the lifted code, the image) made external: the row's
        source's, else the only object's with one of that name."""
        source_of = {self.symbol_of(r): r.source for r in self.rows}
        statics = collections.defaultdict(list)
        for source, obj in self.objects.items():
            for s in obj.symbols:
                if s.storage == coff.STATIC and s.defined and s.function and s.name in wanted:
                    statics[s.name].append((source, s))
        for name, found in statics.items():
            chosen = [s for source, s in found if source == source_of.get(name)] or [s for _, s in found]
            if len(chosen) > 1:
                self.errors.append(
                    f"{name}: {', '.join(source for source, _ in found)} each have a static function "
                    f"of the name, which the original's code calls"
                )
                continue
            chosen[0].storage = coff.EXTERNAL

    # ------------------------------------------------------------------------------------------------ lifted code

    def plan_pieces(self):
        """self.pieces: the runs of the original's code to link as they are, by address."""
        lifted = {r.address for r in self.lifted_rows}
        text = self.sections[".text"]
        # (the rows, and the gaps between them)
        spans, cursor = [], text.virtual_address
        for row in self.rows:
            if not text.virtual_address <= row.address < text.end:
                continue
            if row.address > cursor:
                spans.append((cursor, row.address, None))
            spans.append((row.address, row.end, row))
            cursor = max(cursor, row.end)
        if cursor < text.virtual_address + text.virtual_size:
            spans.append((cursor, text.virtual_address + text.virtual_size, None))
        pieces = []
        for i, (start, end, row) in enumerate(spans):
            following = spans[i + 1][0] if i + 1 < len(spans) and spans[i + 1][2] else None
            if row is None:
                if end - start < GAP:
                    continue
                previous = pieces[-1] if pieces else None
                if previous and previous.end == start:
                    # (code after a lifted function: its own, reached by its short jumps or its end)
                    previous.end = end
                    previous.fallthrough = following if following == end else None
                else:
                    pieces.append(
                        Piece(
                            start,
                            end,
                            f"_unk_{start:08x}",
                            {f"_unk_{start:08x}": start},
                            following if following == end else None,
                        )
                    )
            elif row.address in lifted:
                symbol = self.row_symbols[row.address]
                pieces.append(Piece(start, end, symbol, {symbol: start}))
        # (code that runs on past its end, or jumps past it with a short jump no relocation can move, takes in what
        # follows: a lifted row it takes in keeps its own name there, any other code is a private copy)
        absorbed = []
        for piece in pieces:
            if absorbed and piece.start < absorbed[-1].end:
                absorbed[-1].names.update(piece.names)
                absorbed[-1].end = max(absorbed[-1].end, piece.end)
                continue
            while piece.fallthrough is None and self.escapes(piece):
                row = self.row_at(piece.end) or next((r for r in self.rows if r.address >= piece.end), None)
                if row is None or row.address - piece.end >= GAP:
                    break
                piece.end = row.end
                if row.address in lifted:
                    piece.names[self.row_symbols[row.address]] = row.address
            absorbed.append(piece)
        self.pieces = absorbed
        self.piece_starts = [p.start for p in absorbed]

    def instructions(self, start, end):
        """(address, size, bytes) of each instruction from START to END, decoded in a linear sweep that steps over
        the data among the code: a fixup at an instruction's start is a table's entry, and undecodable bytes are
        skipped one at a time."""
        md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
        code = self.pe.read(start, end - start)
        fixups = set(self.fixups[bisect.bisect_left(self.fixups, start) : bisect.bisect_left(self.fixups, end)])
        offset = 0
        while offset < len(code):
            if start + offset in fixups:
                offset += 4
                continue
            decoded = False
            for address, size, _, _ in md.disasm_lite(code[offset:], start + offset):
                if address in fixups:
                    break
                decoded = True
                yield address, size, code[address - start : address - start + size]
                offset = address - start + size
            if not decoded:
                offset += 1

    def escapes(self, piece):
        """Whether PIECE's code leaves its end other than by a ret, a jmp or a relocated jump: it runs into what
        follows, or a short jump reaches past its end."""
        last = None
        for address, size, raw in self.instructions(piece.start, piece.end):
            last = (address, size, raw)
            if size == 2 and raw[0] in SHORT_BRANCHES:
                target = address + 2 + struct.unpack_from("<b", raw, 1)[0]
                if target >= piece.end:
                    return True
                if target < piece.start:
                    self.errors.append(
                        f"{piece.symbol} (0x{piece.start:08x}): a short jump at 0x{address:08x} reaches "
                        f"back to 0x{target:08x}, before its start: lift the function before it as well"
                    )
        if last is None or last[0] + last[1] < piece.end:
            return False  # (data after the code, a jump table, or padding)
        md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
        ins = next(md.disasm(last[2], last[0]), None)
        return ins is None or ins.mnemonic not in ("ret", "retn", "jmp", "int3")

    def lift(self, extra_names):
        """The lifted code, as objects: [Object]. EXTRA_NAMES {name: address} are names the sources refer to inside
        the lifted code, defined there too."""
        for name, address in extra_names.items():
            i = bisect.bisect_right(self.piece_starts, address) - 1
            self.pieces[i].names.setdefault(name, address)
        objects, obj, text, relocations = [], None, bytearray(), []

        def finish():
            if obj is not None:
                section = obj.add_section(
                    ".text",
                    coff.CODE | coff.EXECUTE | coff.READ | ALIGN_16,
                    bytes(text),
                    relocations=list(relocations),
                    section_symbol=False,
                )
                assert section.number == 1
                objects.append(obj)

        for piece in self.pieces:
            if obj is None or len(relocations) > RELOCATIONS_PER_OBJECT:
                finish()
                obj, text, relocations = Object(), bytearray(), []
            while len(text) % 16:
                text += b"\xcc"
            base = len(text)
            code = bytearray(self.pe.read(piece.start, piece.end - piece.start))
            for name, address in piece.names.items():
                obj.add_symbol(name, base + address - piece.start, 1, coff.FUNCTION_TYPE, coff.EXTERNAL)
            sites = {}
            for fixup in self.fixups[
                bisect.bisect_left(self.fixups, piece.start) : bisect.bisect_left(self.fixups, piece.end)
            ]:
                value = struct.unpack_from("<I", code, fixup - piece.start)[0]
                found = self.target(value)
                if found is None:
                    self.errors.append(
                        f"{piece.symbol}+0x{fixup - piece.start:x}: 0x{value:08x} is nothing this link has"
                    )
                    continue
                sites[fixup - piece.start] = (DIR32, *found)
            for address, size, raw in self.instructions(piece.start, piece.end):
                if size == 5 and raw[0] in (0xE8, 0xE9):
                    at = address + 1
                elif size == 6 and raw[0] == 0x0F and 0x80 <= raw[1] <= 0x8F:
                    at = address + 2
                else:
                    continue
                destination = address + size + struct.unpack_from("<i", raw, size - 4)[0]
                if piece.start <= destination < piece.end:
                    continue
                found = self.target(destination)
                if found is None:
                    self.errors.append(
                        f"{piece.symbol}+0x{address - piece.start:x}: calls or jumps to 0x{destination:08x}, "
                        "which is in no function"
                    )
                    continue
                # (the linker writes the target less the field's end, plus the field's addend)
                sites[at - piece.start] = (REL32, *found)
            if piece.fallthrough is not None:
                symbol, addend = self.target(piece.fallthrough)
                code += b"\xe9" + bytes(4)
                sites[len(code) - 4] = (REL32, symbol, addend)
            for offset, (kind, symbol, addend) in sorted(sites.items()):
                struct.pack_into("<I", code, offset, addend & 0xFFFFFFFF)
                relocations.append(coff.Relocation(base + offset, obj.external(symbol), kind))
            text += code
        finish()
        return objects

    # ------------------------------------------------------------------------------------------------ the image

    def image(self):
        """The original's data sections as one object (define_in_image() adds the names the sources want)."""
        obj = Object()
        for name in IMAGE:
            section = self.sections[name]
            size = section.virtual_size
            if section.bss:
                number = obj.add_section(
                    ".bss", coff.UNINITIALIZED | coff.READ | coff.WRITE | ALIGN_16, b"", size, section_symbol=False
                ).number
                obj.add_symbol(IMAGE[name], 0, number, 0, coff.EXTERNAL)
                continue
            data = bytearray(self.pe.read(section.virtual_address, size).ljust(size, b"\0"))
            relocations = []
            flags = coff.INITIALIZED | coff.READ | (coff.WRITE if name == ".data" else 0) | ALIGN_16
            section_obj = obj.add_section(name, flags, b"", section_symbol=False)
            obj.add_symbol(IMAGE[name], 0, section_obj.number, 0, coff.EXTERNAL)
            for fixup in self.fixups[
                bisect.bisect_left(self.fixups, section.virtual_address) : bisect.bisect_left(
                    self.fixups, section.virtual_address + size
                )
            ]:
                offset = fixup - section.virtual_address
                found = self.target(struct.unpack_from("<I", data, offset)[0])
                if found is None:
                    continue  # (an address this link has nothing at: left as it is, a value nothing can use)
                struct.pack_into("<I", data, offset, found[1] & 0xFFFFFFFF)
                relocations.append(coff.Relocation(offset, obj.external(found[0]), DIR32))
            section_obj.data, section_obj.size, section_obj.relocations = bytes(data), size, relocations
        return obj

    def define_in_image(self, image, names):
        """NAMES {name: address} defined in the image, at their places."""
        by_section = {section.name: section.number for section in image.sections}
        for name, address in sorted(names.items(), key=lambda item: item[1]):
            section = self.section_of(address)
            image.add_symbol(name, address - section.virtual_address, by_section[section.name], 0, coff.EXTERNAL)

    # ------------------------------------------------------------------------------------------------ the link

    def lift_and_image(self, inputs):
        """The lifted code and the image for what INPUTS (the sources' and the runtime's objects) refer to and do not
        define: the lifted code defines the functions, the image the data."""
        wanted = self.undefined(inputs)
        text = self.sections[".text"]
        extra = {}
        for name in wanted:
            address = self.bindings.get(name)
            if address is not None and text.virtual_address <= address < text.end and address not in self.row_symbols:
                i = bisect.bisect_right(self.piece_starts, address) - 1
                if i >= 0 and address < self.pieces[i].end:
                    extra[name] = address
        lifted = [("lifted code", o) for o in self.lift(extra)]
        image = self.image()
        wanted = self.undefined(inputs + lifted + [("the image", image)])
        self.export_statics(set(wanted))
        image_names = {name: self.bindings[name] for name in self.common_placed}
        for name in wanted:
            address = self.bindings.get(name)
            if address is not None and self.section_of(address) and self.section_of(address).name in IMAGE:
                image_names[name] = address
        self.define_in_image(image, image_names)
        for name, size in self.common_placed.items():
            following = self.bound[bisect.bisect_right(self.bound, self.bindings[name]) :]
            if (
                following
                and self.bindings[name] + size > following[0]
                and self.section_of(following[0]) is self.section_of(self.bindings[name])
            ):
                print(
                    f"warning: {name} is {size} bytes, more than the original has before "
                    f"{self.data_names[following[0]][0]}"
                )
        return lifted, [("the image", image)]

    def run(self):
        v = self.version
        output = output_path(v, self.debug, self.source_only)
        work = output.parent / "equivalent"
        self.prepare()
        self.pieces = []
        if not self.source_only:
            self.plan_pieces()
        runtime = [(path, Object.read(project.ROOT / path)) for path in link.RUNTIME_OBJECTS]
        sources = list(self.objects.items())
        if self.source_only:
            lifted, image = [], []
        else:
            lifted, image = self.lift_and_image(sources + runtime)
        everything = sources + runtime + lifted + image
        missing = self.undefined(everything)
        missing = {n: w for n, w in missing.items() if not n.startswith("__imp_") and n not in self.thunks}
        for name, referrers in sorted(missing.items()):
            for referrer in sorted(referrers & {"lifted code", "the image"}):
                self.errors.append(f"{referrer} refers to {name}, which nothing defines")
        # (references only dead code makes: dropped, and the code made to trap)
        for source, obj in self.objects.items():
            trap(obj, self.functions[source], missing, self.errors, source, v)
        if self.errors:
            raise Errors("\n".join(self.errors))
        work.mkdir(parents=True, exist_ok=True)
        for old in work.glob("*"):
            old.unlink()
        inputs = []
        for source, obj in sources:
            path = work / (re.sub(r"[^\w.-]", "_", source) + ".obj")
            obj.write(path)
            inputs.append(path)
        for i, (_, obj) in enumerate(lifted):
            obj.write(work / f"lifted{i}.obj")
            inputs.append(work / f"lifted{i}.obj")
        for _, obj in image:
            obj.write(work / "image.obj")
            inputs.append(work / "image.obj")
        if self.source_only:
            # (from src/imports' definitions; else made from the original's import table)
            inputs += [Path(link.source_import_library(name)) for name in link.IMPORT_LIBRARIES]
        else:
            for name, data in link.import_libraries_of(v, code=True).items():
                project.write_atomic(work / name, data)
                inputs.append(work / name)
        listing = work / "inputs.txt"
        project.write_atomic(listing, "".join(f"{project.relative(p)}\n" for p in inputs))
        output.parent.mkdir(parents=True, exist_ok=True)
        # (the source-only compiler's from src/driver/mwcceppc.rc, the same bytes; else split.py's of the original)
        resources = f"build/{v}/resources.res" if self.source_only else f"build/{v}/obj/resources.res"
        result = link.mwld(
            [*link.RUNTIME_OBJECTS, f"@{project.relative(listing)}", resources],
            project.relative(output),
            "-map",
            project.relative(output.with_suffix(".map")),
            *(["-sym", "on,codeview"] if self.debug else []),
        )
        if result.returncode:
            output.unlink(missing_ok=True)
            raise project.Error(f"the link fails:\n{(result.stdout + result.stderr)[-3000:]}")
        shutil.copy(LICENSING, output.parent / LICENSING.name)
        print(
            f"{v}: {project.relative(output)}: {len(self.rows) - len(self.lifted_rows)} functions of {len(self.rows)} "
            f"from the sources, {len(self.pieces)} pieces of the original's code"
        )


def equivalent_section(normal, obj, number):
    """The number of OBJ's section that is NORMAL's section NUMBER (OBJ compiled with symbols on has sections of
    its own between them): the same name, the same place among the sections of the name, the same size."""
    section = normal.section(number)
    nth = sum(1 for s in normal.sections[: number - 1] if s.name == section.name)
    same = [s for s in obj.sections if s.name == section.name]
    if nth < len(same) and same[nth].size == section.size:
        return same[nth].number
    return None


def comdat_sections(obj):
    """The numbers of OBJ's COMDAT sections (whose section symbol's auxiliary record has a selection)."""
    return {
        s.section
        for s in obj.symbols
        if s.storage == coff.STATIC
        and s.defined
        and s.aux
        and s.name == obj.section(s.section).name
        and obj.section(s.section).flags & 0x1000
        and s.aux[14]
    }


def mark_any(obj, number):
    """Let the linker take any one copy of COMDAT section NUMBER (the compiler marks some 'no duplicates')."""
    for s in obj.symbols:
        if s.storage == coff.STATIC and s.section == number and s.aux and s.aux[14] == 1:
            s.aux = s.aux[:14] + bytes([2]) + s.aux[15:]


def data_extents(obj, bindings):
    """{name: bytes} of the bound variables OBJ defines: each runs to the next symbol of its section."""
    found, by_section = {}, collections.defaultdict(list)
    for s in obj.symbols:
        if (
            s.defined
            and not obj.section(s.section).code
            and s.storage in (coff.EXTERNAL, coff.STATIC)
            and not (s.aux and s.storage == coff.STATIC)
        ):
            by_section[s.section].append(s)
    for number, symbols in by_section.items():
        values = sorted({s.value for s in symbols})
        for s in symbols:
            if s.external and s.name in bindings:
                i = bisect.bisect_right(values, s.value)
                found[s.name] = (values[i] if i < len(values) else obj.section(number).size) - s.value
    return found


def trap(obj, functions, missing, errors, source, version):
    """Drop OBJ's references to the MISSING names where they are in a function the link never runs (FUNCTIONS:
    [(section, offset, name, dead)], sorted), and make such a function trap; any other reference to them is an
    error."""
    trapped = set()
    for section in obj.sections:
        kept = []
        for r in section.relocations:
            symbol = obj.symbol(r.symbol)
            name = symbol.name
            if not symbol.undefined or name not in missing:
                kept.append(r)
                continue
            i = bisect.bisect_right(functions, (section.number, r.offset, "\U0010ffff", True)) - 1
            owner = functions[i] if i >= 0 and functions[i][0] == section.number else None
            if owner is not None and owner[3]:
                trapped.add(owner[:2])
                continue
            errors.append(
                f"{source}: {owner[2] if owner else section.name} refers to {name}, which nothing defines "
                f"and the original does not locate (bind it in config/{version}/bindings.json, or define it)"
            )
            kept.append(r)
        section.relocations = kept
    for number, value in trapped:
        section = obj.section(number)
        section.data = section.data[:value] + UD2 + section.data[value + 2 :]


# ------------------------------------------------------------------------------------------------------------ where


def where(version, addresses):
    v = project.version(version)
    path = output_path(v).with_suffix(".map")
    if not path.exists():
        raise project.Error(f"{project.relative(path)} does not exist: ninja {project.relative(output_path(v))}")
    # (the map's symbol table: address, object, symbol; the lifted code's functions are symbols of its objects)
    entries = []
    for line in path.read_text(errors="replace").splitlines():
        if m := re.fullmatch(r"([0-9a-fA-F]{8}) (\S+\.obj)\s+(\S+)", line.strip()):
            entries.append((int(m[1], 16), m[2], m[3]))
    entries.sort()
    rows = {r.name: r for r in config.functions(v)}
    for text in addresses:
        address = int(text, 16)
        i = bisect.bisect_right(entries, (address, "\U0010ffff")) - 1
        if i < 0:
            print(f"0x{address:08x}: before every symbol")
            continue
        start, module, symbol = entries[i]
        row = rows.get(c_name(symbol))
        place = f"; the original's 0x{row.address + address - start:08x}, {row.source or 'no source'}" if row else ""
        print(f"0x{address:08x}: {module} {symbol}+0x{address - start:x}{place}")


def main():
    if sys.argv[1:2] in (["-h"], ["--help"]):
        print(__doc__)
        return
    args = sys.argv[1:]
    try:
        if args[:1] == ["where"] and len(args) >= 3:
            where(args[1], args[2:])
            return
        debug, source_only = "--debug" in args, "--source-only" in args
        args = [a for a in args if a not in ("--debug", "--source-only")]
        if len(args) != 1 or (debug and source_only):
            sys.exit(__doc__)
        Equivalent(args[0], debug, source_only).run()
    except Errors as e:
        sys.exit(f"error: {args[0]}: the equivalent compiler cannot be linked:\n{e}")
    except project.Error as e:
        sys.exit(f"error: {e}")


if __name__ == "__main__":
    main()
