#!/usr/bin/env python3
"""python tools/data_diff.py [options] SOURCE: how a translation unit's data is laid out, against the target. SOURCE (a
source, or an objdiff.json unit name or its end: Enemies/CIngSpaceJumpGuardian) is compiled with the oracle; each data section of the object (its alignment, then each object in order: offset, size,
value, name and the first function that references it) is compared with the same section of the target asm. Objects
are paired by their contents (local names, @N and lbl_..., are not compared; a string pool is split into its strings),
so the report says which objects are missing, extra or in another order, and the functions that first use them:
literals are laid out in the order the functions using them are compiled.

  --from-ninja FILE  the build.ninja that builds SOURCE (its flags and version; else flags after --)
  --version V        the compiler version (default: the rule's, else GC_1_3_2)
  --override P=R     as tools/oracle.py's; repeatable
  --target [DIR]     the target asm: DIR/**/asm/<source without src/>.s (by default the build/<id> directory
                     --from-ninja's build.ninja names most)
  --section NAME     only that section (.sdata2, .rodata...)
  --all              every section, not only those that differ
  --json             the layouts and the differences as JSON

Without the oracle of the version (GC 1.2.5n...), the original compiler compiles it: the layout is the same.

The target's padding at the end of a section (the next TU's alignment) is not compared, and its object boundaries
come from the project's split configuration: they can be wrong."""
import argparse
import codecs
import difflib
import json
import re
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import inline_diff  # noqa: E402
import oracle  # noqa: E402

IGNORED = (".text", ".init", "extab", "extabindex", ".comment", ".debug", ".line", ".mwcats", ".note", ".rela",
           ".symtab", ".strtab", ".shstrtab")
SHOWN = 40


def anonymous(name):
    """A compiler-made name (@123, @stringBase0, ...data.0, lbl_..., .L_...): not compared."""
    return name.startswith(("@", "lbl_", ".L_", "..."))


def strings_of(data):
    """The NUL-terminated strings DATA is made of, or None if it is not such text."""
    if not data or data[-1] != 0:
        return None
    data = data.rstrip(b"\0") + b"\0"  # (the NULs after the last one: padding)
    parts = data[:-1].split(b"\0")
    if not any(parts) or not all(p and all(32 <= c < 127 or c in (9, 10, 13) for c in p) for p in parts):
        return None
    return [p.decode("latin-1") for p in parts]


def render(section, data, relocations):
    """A data object's value as text: a string, a float or double (in the small data sections), or bytes with the
    symbols they point to."""
    if not relocations and len(data) in (4, 8) and section.startswith((".sdata2", ".sbss2", ".rodata")):
        value = struct.unpack(">f" if len(data) == 4 else ">d", data)[0]
        return f"{'float' if len(data) == 4 else 'double'} {value:.9g}"
    out, at = [], 0
    for offset, symbol in sorted(relocations.items()):
        if offset > at:
            out.append(data[at:offset].hex())
        out.append(f"&{symbol}")
        at = offset + 4
    if at < len(data):
        out.append(data[at:].hex() if any(data[at:]) else f"zero*{len(data) - at}")
    text = " ".join(out)
    return text if len(text) <= 72 else text[:69] + "..."


def item(section, offset, name, data, relocations, users):
    """One object (or one string of a pool, or padding) of a section, with `key`, what is compared."""
    symbols = tuple((o - offset, "*" if anonymous(s) or "+" in s else s) for o, s in sorted(relocations.items()))
    return {"section": section, "offset": offset, "size": len(data), "name": name,
            "value": render(section, data, {o - offset: s for o, s in relocations.items()}),
            "key": data.hex() + repr(symbols), "bytes": data.hex(), "symbols": repr(symbols), "users": users}


def split(section, offset, name, data, relocations, users, padding=False):
    """ITEM, or one item per string of a string pool."""
    if padding:
        return [{"section": section, "offset": offset, "size": len(data), "name": "(padding)",
                 "value": f"pad {len(data)}", "key": f"pad {len(data)}", "bytes": data.hex(), "symbols": "()",
                 "users": []}]
    # (a float or a double can read as text: 4 and 8 bytes are text in the pool, or when they end with one NUL after
    # 3 characters or more; the target's split cuts its pool where it is referenced)
    text = not relocations and (len(data) not in (4, 8) or name.startswith("@stringBase")
                                or (data[-1] == 0 and data[-2] != 0 and all(data[:3])))
    strings = strings_of(data) if text else None
    if not strings:
        return [item(section, offset, name, data, relocations, users)]
    out, at = [], offset
    for text in strings:
        size = len(text) + 1
        out.append({"section": section, "offset": at, "size": size, "name": name, "value": json.dumps(text),
                    "key": "str " + text, "bytes": text.encode("latin-1").hex() + "00", "symbols": "()",
                    "users": users})
        at += size
    return out


def trim(items):
    """ITEMS without the padding at the end (the alignment of what comes next)."""
    while items and items[-1]["name"] == "(padding)":
        items.pop()
    return items


def object_layout(data):
    """{section: {"align": n, "items": [...]}} of an ELF32 big-endian object's data sections."""
    shoff, = struct.unpack_from(">I", data, 0x20)
    shentsize, shnum, shstrndx = struct.unpack_from(">HHH", data, 0x2E)
    headers = [struct.unpack_from(">IIIIIIIIII", data, shoff + i * shentsize) for i in range(shnum)]
    names_at = headers[shstrndx][4]
    names = [data[names_at + h[0]:data.index(b"\0", names_at + h[0])].decode("latin-1") for h in headers]
    symbols = []
    for _, kind, _, _, offset, size, link, _, _, entsize in headers:
        if kind == 2:  # (SHT_SYMTAB)
            strings = headers[link][4]
            for at in range(offset, offset + size, entsize or 16):
                name_offset, value, symbol_size, info, _, index = struct.unpack_from(">IIIBBH", data, at)
                name = data[strings + name_offset:data.index(b"\0", strings + name_offset)].decode("latin-1")
                symbols.append((name, value, symbol_size, info & 0xf, index, info >> 4))
    functions = sorted((s[4], s[1], s[1] + s[2], s[0]) for s in symbols if s[3] == 2)

    def holder(index, offset, kinds=(1, 0)):
        for name, value, size, kind, at, _ in symbols:
            if at == index and kind in kinds and name and value <= offset < value + max(size, 1):
                return name, offset - value
        return None, offset

    def target(symbol, addend):
        name, _, _, kind, index, _ = symbols[symbol]
        if kind == 3:  # (STT_SECTION: the object at the addend)
            name, addend = holder(index, addend)
            name = name or names[index]
        return name if not addend else f"{name}+{addend:#x}"

    relocations, users = {}, {}
    for _, kind, _, _, offset, size, link, info, _, entsize in headers:
        if kind != 4:  # (SHT_RELA)
            continue
        executable = headers[info][2] & 4
        for at in range(offset, offset + size, entsize or 12):
            where, rinfo, addend = struct.unpack_from(">IIi", data, at)
            name = target(rinfo >> 8, addend)
            if executable:
                for index, start, end, function in functions:
                    if index == info and start <= where < end:
                        users.setdefault(name.split("+")[0], []).append((start, function))
                        break
            else:
                relocations.setdefault(info, {})[where] = name
                user, _ = holder(info, where)
                if user:
                    users.setdefault(name.split("+")[0], []).append((1 << 32, f"(data) {user}"))
    layout = {}
    for index, (_, kind, flags, _, offset, size, _, _, align, _) in enumerate(headers):
        name = names[index]
        if not flags & 2 or flags & 4 or name.startswith(IGNORED):  # (only allocated data)
            continue
        content = data[offset:offset + size] if kind != 8 else bytes(size)
        objects = sorted((s[1], s[2], s[0], s[5]) for s in symbols if s[4] == index and s[3] in (0, 1) and s[2])
        items, at = [], 0
        for value, symbol_size, symbol, binding in objects:
            if value > at:
                items += split(name, at, "", content[at:value], {}, [], padding=True)
            mine = {o: s for o, s in relocations.get(index, {}).items() if value <= o < value + symbol_size}
            used = [f for _, f in sorted(set(users.get(symbol, [])))]
            items += split(name, value, symbol, content[value:value + symbol_size], mine, used)
            if binding == 2:
                items[-1]["weak"] = True
            elif binding == 0:
                items[-1]["local"] = True
            at = max(at, value + symbol_size)
        # (code can reach a section's objects through an anchor, ...bss.0, and add the offset itself: who uses the
        # anchor may use any of them)
        anchors = [f for s in symbols if s[4] == index and s[0].startswith("...") for _, f in sorted(set(users.get(s[0], [])))]
        for i in items:
            if not i["users"] and anchors and i["name"] != "(padding)":
                i["users"], i["via"] = list(dict.fromkeys(anchors)), "an anchor of the section"
        layout[name] = {"align": align, "size": size, "items": trim(items)}
    common = [s for s in symbols if s[4] == 0xfff2]
    if common:
        layout["(common)"] = {"align": 0, "size": 0, "items": [
            item("(common)", 0, s[0], bytes(s[2]), {}, [f for _, f in sorted(set(users.get(s[0], [])))])
            for s in common]}
    return layout


def unquote(name):
    return name[1:-1] if name.startswith('"') else name


def asm_layout(path):
    """object_layout's layout of a target asm file (the .obj blocks of its data sections)."""
    layout, users = {}, {}
    section = function = None
    current = None  # [name, offset, bytearray, {offset: symbol}]
    loose = bytearray()

    def section_of(name):
        return layout.setdefault(name, {"align": 0, "size": 0, "items": [], "at": 0})

    def flush_loose():
        if loose and section:
            s = section_of(section)
            s["items"] += split(section, s["at"], "", bytes(loose), {}, [], padding=True)
            s["at"] += len(loose)
        loose.clear()

    for line in path.read_text(errors="replace").splitlines():
        text = line.split("*/", 1)[-1].strip() if line.lstrip().startswith("/*") else line.strip()
        if not text or text.startswith("#"):
            continue
        m = re.match(r"\.(fn|endfn)\s+(\"[^\"]*\"|[^\s,]+)", text)
        if m:
            function = unquote(m.group(2)) if m.group(1) == "fn" else None
            continue
        if function:
            for name in re.findall(r"(\"[^\"]+\"|[\w.$<>@,]+?)@(?:ha|h|l|sda21)\b", text):
                users.setdefault(unquote(name.split()[-1].split(",")[-1]), []).append(function)
            continue
        m = re.match(r"\.(text|data|rodata|bss|sdata|sbss|sdata2|sbss2)$|\.section\s+([^\s,]+)", text)
        if m:
            flush_loose()
            section = "." + m.group(1) if m.group(1) else m.group(2)
            section_of(section)
            continue
        m = re.match(r"\.balign\s+(\d+)", text)
        if m and section and current is None:
            s = section_of(section)
            if not s["items"] and not loose:
                s["align"] = max(s["align"], int(m.group(1)))
            continue
        m = re.match(r"\.comm\s+(\"[^\"]*\"|[^\s,]+),\s*(\w+)", text)
        if m:
            s = section_of("(common)")
            s["items"].append(item("(common)", 0, unquote(m.group(1)), bytes(int(m.group(2), 0)), {}, []))
            continue
        m = re.match(r"\.obj\s+(\"[^\"]*\"|[^\s,]+)", text)
        if m and section:
            flush_loose()
            current = [unquote(m.group(1)), section_of(section)["at"], bytearray(), {}]
            continue
        if text.startswith(".endobj") and current:
            name, offset, content, relocations = current
            s = section_of(section)
            s["items"] += split(section, offset, name, bytes(content), relocations, [],
                                padding=name.startswith("gap_"))
            s["at"] = offset + len(content)
            current = None
            continue
        out = current[2] if current else loose
        m = re.match(r"\.(4byte|2byte|byte|float|double|skip|string|rel)\s+(.*)$", text)
        if not m or section is None or section == ".text":
            continue
        kind, argument = m.groups()
        if kind == "string":
            out += codecs.decode(argument.strip()[1:-1].encode("latin-1"), "unicode_escape").encode("latin-1") + b"\0"
        elif kind == "float":
            out += struct.pack(">f", float(argument))
        elif kind == "double":
            out += struct.pack(">d", float(argument))
        elif kind == "skip":
            out += bytes(int(argument, 0))
        elif kind == "rel":
            if current:
                current[3][current[1] + len(out)] = unquote(argument.split(",")[0].strip()) + "+?"
            out += bytes(4)
        else:
            size = {"4byte": 4, "2byte": 2, "byte": 1}[kind]
            for value in argument.split(","):
                value = value.strip()
                if re.match(r"-?(0x[0-9A-Fa-f]+|\d+)$", value):
                    out += (int(value, 0) & ((1 << 8 * size) - 1)).to_bytes(size, "big")
                else:
                    if current:
                        current[3][current[1] + len(out)] = unquote(value)
                    out += bytes(size)
    flush_loose()
    for s in layout.values():
        for i in s["items"]:
            for symbol in re.findall(r"'([^']+)'\)", i.get("symbols", "")):
                users.setdefault(symbol, []).append(f"(data) {i['name']}")
    for name, s in layout.items():
        s["size"] = s.pop("at")
        for i in s["items"]:
            i["users"] = list(dict.fromkeys(users.get(i["name"], [])))
        s["items"] = trim(s["items"])
    return {name: s for name, s in layout.items() if name != ".text" and not name.startswith(IGNORED)}


def absorb(mine, theirs):
    """The target's last object of a section can hold the section's end padding (the split does not know where the
    object ends): cut it to the oracle's object it starts like."""
    if not theirs or theirs[-1]["key"] in {i["key"] for i in mine}:
        return
    last = theirs[-1]
    for i in mine:
        rest = last["bytes"][len(i["bytes"]):]
        if (i["symbols"] == last.get("symbols") and last["bytes"].startswith(i["bytes"]) and rest
                and not rest.strip("0") and len(rest) < 16):
            last.update(size=i["size"], value=i["value"], key=i["key"], bytes=i["bytes"])
            return


def made_early(layout, functions, compiled=()):
    """Mark each float literal of the target laid out before the literals of a function compiled before its first
    user (`made`: the first users before and after it): literals are laid out as they are made, when the first
    function using them is compiled, so one of the functions compiled between those made it, and it is not in the
    target's text (stripped, or another TU's copy); `candidates`: those of COMPILED (the oracle's functions, in
    order) between the two that the target's text does not have."""
    order = {f: n for n, f in enumerate(functions)}
    for name, s in layout.items():
        for i in s["items"]:  # (the users in the order compiled)
            i["users"].sort(key=lambda u: order.get(u, len(order)))
        literals = [(i, order[i["users"][0]]) for i in s["items"]
                    if i["users"] and i["users"][0] in order and i["value"].startswith(("float", "double"))]
        for n, (i, at) in enumerate(literals):
            later = [j for j, a in literals[n + 1:] if a < at]
            if later:
                after, before = literals[n - 1][0]["users"][0] if n else None, later[0]["users"][0]
                i["made"] = (after, before)
                if before in compiled:
                    start = compiled.index(after) + 1 if after in compiled else 0
                    i["candidates"] = [f for f in compiled[start:compiled.index(before)] if f not in order]


def named(i):
    return i["name"] and i["name"] != "(padding)" and not anonymous(i["name"])


def short(name):
    return name if len(name) <= 60 else name[:57] + "..."


def module_objects(root, own, cache={}):
    """{object name: the asm file (under ROOT, a module's asm directory) of another TU that has it}."""
    if root not in cache:
        index = {}
        for path in sorted(root.rglob("*.s")):
            for m in re.finditer(r'^\.obj\s+("[^"]*"|[^\s,]+)', path.read_text(errors="replace"), re.M):
                index.setdefault(unquote(m.group(1)), path)
        cache[root] = index
    return {name: path for name, path in cache[root].items() if path != own}


def annotate(mine, theirs):
    """Mark each named object found in another section on the other side (`elsewhere`)."""
    for one, other in ((mine, theirs), (theirs, mine)):
        where = {i["name"]: n for n, s in other.items() for i in s["items"] if named(i)}
        for n, s in one.items():
            for i in s["items"]:
                if named(i) and where.get(i["name"], n) != n:
                    i["elsewhere"] = where[i["name"]]


def linked(items):
    """ITEMS as linked: without the objects the linker drops (`dropped`) and the padding (which depends on them)."""
    return [i for i in items if "dropped" not in i and i["name"] != "(padding)"]


def compare(mine, theirs):
    """The differences between two sections' items (object_layout's), as difflib opcodes over their keys, with each
    item missing in one found elsewhere in the other (`moved`)."""
    a, b = [i["key"] for i in theirs], [i["key"] for i in mine]
    out = []
    for tag, i1, i2, j1, j2 in difflib.SequenceMatcher(None, a, b, autojunk=False).get_opcodes():
        out.append({"tag": tag, "target": theirs[i1:i2], "oracle": mine[j1:j2]})
    for d in out:
        if d["tag"] == "equal":
            continue
        for side, other in (("target", "oracle"), ("oracle", "target")):
            for i in d[side]:
                where = [j for j in (mine if other == "oracle" else theirs) if j["key"] == i["key"]]
                if len(where) == 1 and i["key"] not in [j["key"] for j in d[other]]:
                    i["moved"] = where[0]["offset"]
    return out


def line(mark, i):
    users = i["users"]
    used = f"  first used in {short(users[0])}" + (f" (+{len(users) - 1})" if len(users) > 1 else "") if users else ""
    if "made" in i:
        after, before = i["made"]
        used += (f"; made earlier, before {short(before)}'s literals"
                 + (f" (after {short(after)}'s)" if after else "")
                 + ": by a function compiled there, not in the target or whose use of it was optimized away")
        if i.get("candidates"):
            used += (f" (the oracle compiles {len(i['candidates'])} there: "
                     + ", ".join(short(c) for c in i["candidates"][:3]) + (", ..." if len(i["candidates"]) > 3 else "")
                     + ")")
        elif "candidates" in i:
            used += " (the oracle compiles no function there the target lacks)"
    if "via" in i:
        used = f"  used through {i['via']} in {short(users[0])}" + (f" (+{len(users) - 1})" if len(users) > 1 else "")
    moved = f"  [at {i['moved']:#x} in the other]" if "moved" in i else ""
    if "elsewhere" in i:
        moved += f"  [in {i['elsewhere']} in the other]"
    if mark == "+" and not users and i["name"] != "(padding)" and "dropped" not in i:
        moved += "  (nothing references it)"
    name = f"  {i['name']}" if i["name"] and i["name"] != "(padding)" else ""
    return f"   {mark} {i['offset']:#6x} {i['size']:#6x}  {i['value']}{name}{used}{moved}"


def dropped_text(items):
    """What the linker drops of ITEMS, in a line."""
    dropped = [i for i in items if "dropped" in i]
    if not dropped:
        return []
    unreferenced = [i["name"] for i in dropped if i["dropped"] == "unreferenced"]
    users = [f"{i['name']} ({i['dropped'][5:]})" for i in dropped if i["dropped"].startswith("used ")]
    kept = [f"{i['name']} ({i['dropped']})" for i in dropped
            if i["dropped"] != "unreferenced" and not i["dropped"].startswith("used ")]
    parts = ([f"{len(unreferenced)} unreferenced ({', '.join(unreferenced[:8])}{', ...' if len(unreferenced) > 8 else ''})"]
             if unreferenced else []) + ([f"{len(kept)} weak, kept by another TU: {', '.join(kept[:4])}"
                                          + (", ..." if len(kept) > 4 else "")] if kept else []) + (
        [f"{len(users)} {'; '.join(users[:3])}" + ("; ..." if len(users) > 3 else "")] if users else [])
    return [f"     dropped when linked: {'; '.join(parts)}"]


def report(name, mine, theirs, everything):
    out = []
    if mine is None or theirs is None:
        which = "the oracle's object" if mine is None else "the target"
        size = (mine or theirs)["size"]
        items = (mine or theirs)["items"]
        if mine is not None and (not linked(items) or name.startswith((".ctors", ".dtors"))) and not everything:
            return []
        out = [f"== {name}: not in {which} ({size:#x} bytes, {len(items)} objects)"]
        if mine is not None:
            out += dropped_text(items)
            items = linked(items)
        return out + [line("+" if theirs is None else "-", i) for i in items[:SHOWN]]
    differences = compare(linked(mine["items"]), linked(theirs["items"]))
    same = all(d["tag"] == "equal" for d in differences)
    if same and not everything:
        return []
    # (the target's alignment is what the address the split saw allows: not compared)
    out.append(f"== {name}: {'same once linked' if same else 'differs'}: oracle {mine['size']:#x} bytes, align "
               f"{mine['align']}; target {theirs['size']:#x}, align {theirs['align']}  (- target, + oracle)")
    out += dropped_text(mine["items"])
    shown = 0
    for d in differences:
        if d["tag"] == "equal":
            out.append(f"     ({len(d['target'])} the same, from {d['target'][0]['offset']:#x} / "
                       f"{d['oracle'][0]['offset']:#x}: {d['target'][0]['value'][:40]} ...)")
            continue
        for i in d["target"]:
            out.append(line("-", i))
        for i in d["oracle"]:
            out.append(line("+", i))
        shown += len(d["target"]) + len(d["oracle"])
        if shown > SHOWN:
            out.append("     ... (--json for all)")
            break
    return out


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("source")
    parser.add_argument("--from-ninja")
    parser.add_argument("--version")
    parser.add_argument("--override", action="append", default=[])
    parser.add_argument("--target", nargs="?", const="")
    parser.add_argument("--section")
    parser.add_argument("--all", action="store_true")
    parser.add_argument("--json", action="store_true")
    argv = sys.argv[1:]
    flags = []
    if "--" in argv:
        flags = argv[argv.index("--") + 1:]
        argv = argv[:argv.index("--")]
    arguments = parser.parse_args(argv)
    arguments.source = inline_diff.unit_source(arguments.from_ninja, arguments.source)
    compiled = oracle.compile_trace(arguments.source, flags, arguments.from_ninja, arguments.version,
                                    arguments.override, check=False, original_fallback=True)
    mine = object_layout(compiled["object"])
    if not arguments.target and not arguments.from_ninja:
        raise oracle.OracleError("without --target DIR, --from-ninja names the target directory")
    directory = Path(arguments.target) if arguments.target else inline_diff.target_directory(arguments.from_ninja)
    path = inline_diff.target_asm(directory, compiled["source"])
    if path is None:
        raise oracle.OracleError(f"no target asm for {compiled['source']} under {directory}")
    theirs = asm_layout(path)
    for n in set(mine) & set(theirs):
        absorb(mine[n]["items"], theirs[n]["items"])
    annotate(mine, theirs)
    # (the linker keeps one copy of a weak object per module, in one TU)
    module = Path(str(path)[:str(path).replace("\\", "/").rindex("/asm/") + 4])
    elsewhere = module_objects(module, path)
    # (the linker strips the local objects nothing references, and those only the functions not in the target use,
    # stripped or another TU's; it keeps the global ones)
    functions = inline_diff.asm_functions(path)
    text_order = [n for n, _ in sorted(oracle.object_symbols(compiled["object"]).items(),
                                       key=lambda x: (x[1][0], x[1][1]))]
    # (the order the functions were compiled in, from the trace; with deferred inlining, the text is in the reverse
    # order, the target's too)
    compiled_order = list(dict.fromkeys(m.group(2) or m.group(1) for line in compiled["lines"]
                                        for m in [re.match(r"ra\.before function=(\S*)(?: link=(.*))?$", line)] if m))
    compiled_order = [f for f in compiled_order if f in text_order] or text_order
    reverse = (len(compiled_order) > 1 and compiled_order is not text_order
               and text_order.index(compiled_order[0]) > text_order.index(compiled_order[-1]))
    made_early(theirs, list(functions)[::-1] if reverse else list(functions), compiled_order)
    position = {f: n for n, f in enumerate(compiled_order)}
    for s in mine.values():
        for i in s["items"]:
            i["users"].sort(key=lambda u: position.get(u, len(position)))
    for n, s in mine.items():
        if n.startswith((".ctors", ".dtors")):
            continue
        for i in s["items"]:
            users = [u for u in i["users"] if not u.startswith("(data)")]
            if i["name"] == "(padding)":
                continue
            if i.get("weak") and i["name"] in elsewhere:
                i["dropped"] = elsewhere[i["name"]].relative_to(module).as_posix()
            elif not i["users"] and i.get("local"):
                i["dropped"] = "unreferenced"
            elif (i.get("local") and users and len(users) == len(i["users"])
                  and not any(u in functions for u in users)):
                i["dropped"] = "used by functions not in the target: " + ", ".join(short(u) for u in users[:2])
    names = [n for n in list(mine) + [n for n in theirs if n not in mine]
             if not arguments.section or n == arguments.section]
    if arguments.json:
        out = {"schema_version": oracle.SCHEMA_VERSION, "source": compiled["source"], "target": str(path),
               "sections": {n: {"oracle": mine.get(n), "target": theirs.get(n),
                                "differences": compare(linked(mine[n]["items"]), linked(theirs[n]["items"]))
                                if n in mine and n in theirs else None} for n in names}}
        print(json.dumps(out, indent=1))
        return
    print(f"{compiled['source']} against {path}")
    lines = []
    for n in names:
        lines += report(n, mine.get(n), theirs.get(n), arguments.all)
    print("\n".join(lines) if lines else "the data is laid out as in the target")


if __name__ == "__main__":
    try:
        main()
    except oracle.OracleError as error:
        sys.exit(str(error))
