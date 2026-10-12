#!/usr/bin/env python3
"""python tools/link.py VERSION OUTPUT INPUT...: link the objects and libraries into the executable with the CodeWarrior
Windows/x86 linker the original was linked with, give it the original's time stamp (the linker writes the time it runs)
and check it against the original's SHA-1, writing build/VERSION/ok when it is the original."""
import hashlib
import json
import re
import shutil
import struct
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from pe import PEFile
from split import read_splits


def section_sizes(path):
    """{section name: bytes} of an object, the code and data of a unit (the name up to its $ group), without its debug
    information."""
    data = Path(path).read_bytes()
    sizes = {}
    for index in range(struct.unpack_from("<H", data, 2)[0]):
        header = 20 + 40 * index
        name = data[header:header + 8].rstrip(b"\0").decode().split("$")[0]
        if name != ".debug":
            sizes[name] = sizes.get(name, 0) + struct.unpack_from("<I", data, header + 16)[0]
    return {name: size for name, size in sizes.items() if size}


def usable(version, path):
    """Whether a unit's compiled object can stand in for its split one: the units of a version's link order are not
    always the sources' (a function may be in another source in this version), and then it holds other bytes than its
    range does. The sections' sizes are what is compared."""
    split_object = path.replace(f"build/{version}/compiled/", f"build/{version}/obj/", 1)
    return Path(split_object).exists() and section_sizes(path) == section_sizes(split_object)


def defined_symbols(path):
    """The external symbols an object defines."""
    data = Path(path).read_bytes()
    pointer, count = struct.unpack_from("<II", data, 8)
    strings = pointer + 18 * count
    names, index = set(), 0
    while index < count:
        entry = pointer + 18 * index
        raw = data[entry:entry + 8]
        section, _, storage, aux = struct.unpack_from("<hHBB", data, entry + 12)
        index += 1 + aux
        if section > 0 and storage == 2:
            if raw[:4] == bytes(4):
                offset = struct.unpack_from("<I", raw, 4)[0]
                names.add(data[strings + offset:data.index(bytes(1), strings + offset)].decode("latin-1"))
            else:
                names.add(raw.rstrip(bytes(1)).decode("latin-1"))
    return names


def undefine_symbols(source, target, names):
    """Write SOURCE, an object, to TARGET with the external symbols NAMES made undefined: the object keeps the bytes and
    the relocations that name them, and the linker takes the symbols from the object that defines them."""
    data = bytearray(Path(source).read_bytes())
    pointer, count = struct.unpack_from("<II", data, 8)
    strings = pointer + 18 * count
    index = 0
    while index < count:
        entry = pointer + 18 * index
        raw = bytes(data[entry:entry + 8])
        aux = data[entry + 17]
        if raw[:4] == bytes(4):
            offset = struct.unpack_from("<I", raw, 4)[0]
            name = bytes(data[strings + offset:data.index(bytes(1), strings + offset)]).decode("latin-1")
        else:
            name = raw.rstrip(bytes(1)).decode("latin-1")
        if name in names and data[entry + 16] == 2:
            struct.pack_into("<Ihh", data, entry + 8, 0, 0, struct.unpack_from("<H", data, entry + 14)[0])
        index += 1 + aux
    Path(target).parent.mkdir(parents=True, exist_ok=True)
    Path(target).write_bytes(data)


def hook_symbols(source, target, names):
    """Write SOURCE, an object, to TARGET with the functions NAMES hooked: the definition of NAME is named NAME__retail
    and an undefined NAME takes its place in every relocation, those of the object's own functions too, so that the
    calls to it reach the wrapper defined elsewhere, which calls NAME__retail."""
    data = bytearray(Path(source).read_bytes())
    sections = struct.unpack_from("<H", data, 2)[0]
    pointer, count = struct.unpack_from("<II", data, 8)
    strings = pointer + 18 * count
    size = struct.unpack_from("<I", data, strings)[0]
    table = bytearray(data[strings:strings + size])
    added, moved = bytearray(), {}
    index = 0
    while index < count:
        entry = pointer + 18 * index
        raw = bytes(data[entry:entry + 8])
        aux = data[entry + 17]
        if raw[:4] == bytes(4):
            offset = struct.unpack_from("<I", raw, 4)[0]
            name = bytes(table[offset:table.index(bytes(1), offset)]).decode("latin-1")
        else:
            name = raw.rstrip(bytes(1)).decode("latin-1")
        if name in names and struct.unpack_from("<h", data, entry + 12)[0] > 0:
            struct.pack_into("<II", data, entry, 0, len(table))
            table += (name + "__retail").encode("latin-1") + bytes(1)
            # (the undefined symbol: a name in the string table, no section, external)
            moved[index] = count + len(added) // 18
            added += struct.pack("<IIIhHBB", 0, len(table), 0, 0, 0x20, 2, 0)
            table += name.encode("latin-1") + bytes(1)
        index += 1 + aux
    if not moved:
        raise SystemExit(f"{source} defines none of {sorted(names)}")
    for k in range(sections):
        header = 20 + 40 * k
        raw = struct.unpack_from("<I", data, header + 20)[0]
        relocations, number = struct.unpack_from("<I", data, header + 24)[0], struct.unpack_from("<H", data, header + 32)[0]
        for r in range(number):
            at = relocations + 10 * r
            place, symbol, kind = struct.unpack_from("<IIH", data, at)
            if symbol not in moved:
                continue
            # (only the references to the function's entry: the jump tables of its switches are in the data, and name
            # the symbol plus an offset into its code: absolute with an addend, or a rel32 other than a plain call)
            field = struct.unpack_from("<i", data, raw + place)[0] if raw and place + 4 <= struct.unpack_from("<I", data, header + 16)[0] else None
            if kind in (6, 20) and field == 0:
                struct.pack_into("<I", data, at + 4, moved[symbol])
    struct.pack_into("<I", data, 12, count + len(added) // 18)
    struct.pack_into("<I", table, 0, len(table))
    Path(target).parent.mkdir(parents=True, exist_ok=True)
    Path(target).write_bytes(bytes(data[:strings]) + bytes(added) + bytes(table) + bytes(data[strings + size:]))


def revert_conflicts(version, inputs, text):
    """The compiled units the linker's errors blame: a compiled unit that needs a symbol nothing defines, a split one
    that needs a symbol its compiled counterpart is the only defining one of by another name (so that one), and the
    units that define a symbol twice."""
    compiled = {Path(p).name: p for p in inputs if p.startswith(f"build/{version}/compiled/")}
    reverted = set()
    counterpart = None
    for undefined, where in re.findall(r"Undefined symbol: (\S+) in\s+#\s+(\S+)", text):
        if where in compiled:
            reverted.add(compiled[where])
            continue
        # (a split unit needs a symbol: the compiled unit standing in for the one that defines it, in the split)
        if counterpart is None:
            counterpart = {path: defined_symbols(path.replace(f"build/{version}/compiled/", f"build/{version}/obj/", 1))
                           for path in compiled.values()}
        reverted.update(path for path, names in counterpart.items() if undefined in names)
    for first, rest in re.findall(r"Multiple definitions of symbol: (\S+) in\s+#\s+([^\n]+)", text):
        reverted.update(compiled[name.strip()] for name in rest.split(",") if name.strip() in compiled)
    return reverted


def main():
    oracle = sys.argv[1] == "--oracle"
    version, output, *inputs = sys.argv[2 if oracle else 1:]
    config = json.loads(Path(f"config/{version}/config.json").read_text())
    # (the oracle executable links its units from source whatever their ranges: a unit that does not link is an error,
    # and the result is not the original, so the SHA-1 is not checked)
    if config.get("validate_units") and not oracle:
        replaced = [path for path in inputs if path.startswith(f"build/{version}/compiled/") and not usable(version, path)]
        inputs = [path.replace(f"build/{version}/compiled/", f"build/{version}/obj/", 1) if path in replaced else path
                  for path in inputs]
        print(f"{len(replaced)} compiled units do not fit their ranges and are linked as split", file=sys.stderr)
    # (wibo runs the linker, a Windows executable, elsewhere)
    linker = str(Path("build/compilers/pro53/mwld.exe"))
    wibo = [] if sys.platform == "win32" else ["build/tools/wibo"]
    compiled_objects = [p for p in inputs if "/oracle/compiled/" in p.replace("\\", "/")]
    # (a hooked function keeps its code under the name NAME__retail, and the wrapper the oracle build defines under NAME
    # is what the rest of the original calls: the wrapper traces and calls the original's code)
    for symbol in config.get("oracle", {}).get("hooks", []) if oracle else []:
        held = [p for p in inputs if p.endswith(".obj") and "/oracle/compiled/" not in p.replace("\\", "/")
                and symbol in defined_symbols(p)]
        if len(held) != 1:
            print(f"hook {symbol}: defined by {len(held)} objects", file=sys.stderr)
            sys.exit(1)
        patched = f"build/{version}/oracle/hooked/{Path(held[0]).name}"
        hook_symbols(held[0], patched, {symbol})
        inputs = [patched if p == held[0] else p for p in inputs]
    while True:
        run = subprocess.run([*wibo, linker, "-nostdlib", "-subsystem", "console",
                              "-m", "_mainCRTStartup", *inputs, "-o", output, "-map", str(Path(output).with_suffix(".map"))],
                             stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        if run.returncode == 0:
            break
        if oracle:
            # (an oracle unit defines what another unit of the original does as well: the original's is used)
            duplicated = re.findall(r"Multiple definitions of symbol: (\S+) in\s+#\s+([^\n]+)", run.stdout)
            # (what the original's unit defines and the source does not, that the rest of the original needs: the split
            # object of the unit joins, without the symbols the compiled unit defines)
            wanted = set(re.findall(r"Undefined symbol: (\S+) in", run.stdout))
            joined = False
            if wanted:
                ours = set()
                for path in compiled_objects:
                    ours |= defined_symbols(path)
                for path in compiled_objects:
                    split_object = path.replace("/oracle/compiled/", "/obj/", 1)
                    patched = f"build/{version}/oracle/supplement/{Path(split_object).name}"
                    if patched in inputs or not Path(split_object).exists():
                        continue
                    if wanted & (defined_symbols(split_object) - ours):
                        undefine_symbols(split_object, patched, ours)
                        inputs.append(patched)
                        joined = True
            if joined:
                continue
            stripped = {}
            for symbol, where in duplicated:
                names = [n.strip() for n in where.split(",")]
                ours = [p for p in inputs if "/oracle/compiled/" in p.replace("\\", "/") and Path(p).name in names]
                if len(ours) == 1:
                    stripped.setdefault(ours[0], set()).add(symbol)
            if stripped:
                for path, names in stripped.items():
                    patched = str(Path(f"build/{version}/oracle/patched") / Path(path).name)
                    undefine_symbols(path, patched, names)
                    inputs = [patched if p == path else p for p in inputs]
                    print(f"{Path(path).name}: {len(names)} symbols taken from the original", file=sys.stderr)
                continue
        reverted = (revert_conflicts(version, inputs, run.stdout)
                    if config.get("validate_units") and not oracle else None)
        if not reverted:
            print(run.stdout, file=sys.stderr)
            sys.exit(run.returncode)
        print(f"{len(reverted)} compiled units are linked as split, for the symbols they define or need: "
              + ", ".join(Path(path).name for path in sorted(reverted)), file=sys.stderr)
        inputs = [path.replace(f"build/{version}/compiled/", f"build/{version}/obj/", 1) if path in reverted else path
                  for path in inputs]
    if oracle:
        # (the executable finds the licence manager stub beside itself, as the compilers do)
        stub = Path(__file__).parent / "lmgr_stub" / "lmgr326b.dll"
        # (also where wibo runs it: the directory is then copied to a Windows machine as it is)
        if stub.exists():
            shutil.copy(stub, Path(output).parent)
        return
    original = PEFile(Path(config["original"]))
    linked = bytearray(Path(output).read_bytes())
    stamp = struct.unpack_from("<I", original.data, 0x3C)[0] + 8
    linked[stamp:stamp + 4] = original.data[stamp:stamp + 4]
    Path(output).write_bytes(linked)
    if hashlib.sha1(linked).hexdigest() == config["sha1"]:
        units = len(read_splits(f"config/{version}/splits.txt")[1])
        built = sum(path.startswith(f"build/{version}/compiled/") for path in inputs)
        Path(f"build/{version}/ok").write_text(f"{version}: the original, {built} of {units} units linked from source\n")
        return
    # (where the link differs: the units whose ranges hold the first differing bytes)
    ranges = [(start, end, unit) for unit, sections in read_splits(f"config/{version}/splits.txt")[1]
              for _, start, end, _ in sections]
    print(f"{output}: SHA-1 is not {config['sha1']}", file=sys.stderr)
    if len(linked) != len(original.data):
        print(f"  {len(linked)} bytes, not {len(original.data)}", file=sys.stderr)
    shown = set()
    for offset in range(min(len(linked), len(original.data))):
        if linked[offset] == original.data[offset]:
            continue
        section = next((s for s in original.sections if s.file_offset <= offset < s.file_offset + s.file_size), None)
        address = section.virtual_address + offset - section.file_offset if section else None
        unit = next((u for start, end, u in ranges if address is not None and start <= address < end), "-")
        if unit not in shown:
            shown.add(unit)
            print(f"  differs at {f'0x{address:08x}' if address else f'offset 0x{offset:x}'} ({unit})", file=sys.stderr)
            if len(shown) == 10:
                break
    sys.exit(1)


if __name__ == "__main__":
    main()
