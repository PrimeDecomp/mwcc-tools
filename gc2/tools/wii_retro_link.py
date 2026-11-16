#!/usr/bin/env python3
"""Build the native Wii_Retro compiler by splitting and relinking Wii 1.0RC1."""

import argparse
import hashlib
import json
import os
import subprocess
import sys
import tempfile
from collections import defaultdict
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import wii_patch_pretest as pretest
import wii_patch_struct_copy as struct_copy
import wii_patch_template_vtables as vtables
import wii_retro_pe
from mw import coff, formats, link, project
from mw.pe import PEFile

ROOT = project.ROOT.parent
VERSION = "Wii_Retro"
ALIGNMENT_FUNCTION = "_CMach_GetObjectAlignment"
ALIGNMENT_OPTIONS = {
    "RC1_structalignment": 0x74F4C0,
    "RC1_oldalignment": 0x74F444,
    "RC1_GetQUALalign": 0x4F4DC0,
    "RC1_GetQualifiedTypeAlign": 0x4F2760,
    "RC1_AllocationAlignment": 0x4F4BB0,
    "RC1_Registers_GetVarInfo": 0x590780,
}
INLINE_SYMBOLS = {
    "RC1_unique_name_counter": 0x70F808,
    "RC1_expanding_function": 0x719050,
    "RC1_GraphInlining": 0x558900,
    "RC1_InlineSetup": 0x55A740,
    "RC1_AppendLocal": 0x510BB0,
}
INLINE_CALLS = (
    (0x47CE46, "fn_0047ce20", "_CInline_CallGraphInlining", "_retro_CallGraphInlining"),
    (0x5593EA, "fn_005592e0", "fn_0055a740", "_retro_InlineSetup"),
    (0x55A24E, "fn_00559f50", "fn_0055a740", "_retro_InlineSetup"),
    (0x55A9AE, "fn_0055a740", "_CFunc_AppendLocal", "_retro_AppendInlineLocal"),
    (0x55AB4F, "fn_0055a740", "_CFunc_AppendLocal", "_retro_AppendInlineFormal"),
)
REFERENCE_SYMBOLS = {
    "RC1_ipa_kind": 0x76C0B4,
    "RC1_first_linear": 0x7503DC,
    "RC1_locals": 0x758120,
    "RC1_Linearize": 0x5D9830,
    "RC1_CopyAndConstantPropagation": 0x60AF30,
    "RC1_Delinearize": 0x5D8940,
    "RC1_AssignLocals": 0x46D690,
    "RC1_ReassignLocals": 0x46D8B0,
}
REFERENCE_CALLS = (
    (0x58D5B8, "_IRO_Optimizer", "_IRO_Linearize", "_retro_Linearize"),
    (0x58DEB0, "_IRO_Optimizer", "_IRO_CopyAndConstantPropagation", "_retro_CopyAndConstantPropagation"),
    (0x58E110, "_IRO_Optimizer", "_IRO_CopyAndConstantPropagation", "_retro_CopyAndConstantPropagation"),
    (0x58D98F, "_IRO_Optimizer", "_IRO_Delinearize", "_retro_Delinearize"),
    (0x46A7AB, "_CodeGen_Generator", "fn_0046d690", "_retro_AssignLocals"),
    (0x46A78C, "_CodeGen_Generator", "fn_0046d8b0", "_retro_ReassignLocals"),
)
SCHEDULER_SYMBOLS = {
    "RC1_ScheduleBlock": 0x5B6AD0,
}
SCHEDULER_CALLS = (
    (0x46B3E4, "_CodeGen_Generator", "_scheduleinstructions", "_retro_scheduleinstructions"),
    (0x46B335, "_CodeGen_Generator", "_scheduleinstructions", "_retro_scheduleinstructions"),
    (0x5B6AB1, "_scheduleinstructions", "fn_005b6ad0", "_retro_schedule_block"),
    (0x5B7026, "fn_005b6e50", "_may_alias", "_retro_aggregate_may_alias"),
    (0x5B70D6, "fn_005b70a0", "_may_alias", "_retro_aggregate_may_alias"),
    (0x5B7126, "fn_005b70a0", "_may_alias", "_retro_aggregate_may_alias"),
)
TEMP_CREATORS = (
    (0x62EE38, "fn_0062ed70", "_retro_CreateCommonSubexpression"),
    (0x630581, "fn_00630350", "_retro_CreateScalarizedMember"),
    (0x6346DD, "_IRO_MoveInvariantExpressions", "_retro_CreateLoopInvariant"),
)
BANNER_POINTER = 0x691988
BANNER_STRING = 0x691934
BANNER_SYMBOL = "_WiiRetroCompilerName"


def read_config(version):
    return json.loads((ROOT / "config" / version / "config.json").read_text(encoding="utf-8"))


def split_layout(pe, layout, symbols):
    ranges = defaultdict(list)
    for unit in formats.read_splits(layout / "splits.txt").units:
        for item in unit.ranges:
            if item.start < item.end:
                ranges[item.section].append((item.start, item.end, unit.name))

    scattered = json.loads((layout / "units.json").read_text(encoding="utf-8"))
    modules = {item["module"]: item["unit"] for item in scattered["modules"]}
    for item in scattered["scattered"]:
        ranges[item["section"]].append(
            (int(item["start"], 16), int(item["end"], 16), modules.get(item["module"], item["module"]))
        )

    alignment = next(symbol for symbol in symbols if symbol.name == ALIGNMENT_FUNCTION)
    removed_start, removed_end = alignment.address, alignment.address + alignment.size
    sections, units = {}, []
    for section in pe.sections:
        name = section.name
        if name == ".reloc":
            continue
        kind = "code" if section.code else "bss" if section.bss else "rodata" if name in (".rdata", ".exc") else "data"
        sections[name] = [("type", kind), ("align", "1")]
        # The linker rebuilds the resource and import tables. Preserve the order of the CRT and exception tables.
        if name in (".rsrc", ".idata"):
            continue
        if name in (".CRT", ".exc"):
            units.append(formats.Unit(name[1:], [formats.Range(name, section.virtual_address, section.end)]))
            continue
        start = section.virtual_address
        for first, last, owner in sorted(ranges[name]):
            if first < start or last > section.end:
                raise ValueError(f"{name}: overlapping or invalid CodeView range at 0x{first:x} ({owner})")
            if start < first:
                units.append(formats.Unit(f"{name[1:]}/{start:08x}-gap", [formats.Range(name, start, first)]))
            pieces = [(first, last)]
            if name == ".text" and first <= removed_start < last:
                if removed_end > last:
                    raise ValueError(f"{ALIGNMENT_FUNCTION} crosses a CodeView range")
                pieces = [(first, removed_start), (removed_end, last)]
            for begin, end in pieces:
                if begin < end:
                    label = f"{name[1:]}/{begin:08x}-{Path(owner).stem}"
                    units.append(formats.Unit(label, [formats.Range(name, begin, end)]))
            start = last
        if start < section.end:
            units.append(formats.Unit(f"{name[1:]}/{start:08x}-tail", [formats.Range(name, start, section.end)]))
    return formats.Splits(sections, units)


def link_symbols(pe, layout):
    symbols = formats.read_symbols(layout / "symbols.txt")
    names, addresses = set(), set()
    for symbol in symbols:
        if symbol.name in names:
            symbol.name += f"_{symbol.address:08x}"
        names.add(symbol.name)
        addresses.add(symbol.address)
        # Cross-unit references can target a procedure with no public CodeView name.
        if symbol.scope == "local":
            symbol.options = [(key, value) for key, value in symbol.options if key != "scope"]
            symbol.options.append(("scope", "global"))

    # Name the exact destination of each absolute fixup, including private data and function interiors.
    for fixup in pe.base_relocations:
        address = pe.word(fixup)
        if address not in addresses:
            symbols.append(formats.Symbol(f"_va_{address:08x}", pe.section_for_address(address).name, address))
            addresses.add(address)

    patches = (
        pretest.SYMBOLS | vtables.SYMBOLS | ALIGNMENT_OPTIONS |
        INLINE_SYMBOLS | REFERENCE_SYMBOLS | SCHEDULER_SYMBOLS
    )
    for name, address in patches.items():
        symbols.append(formats.Symbol(f"_{name}", pe.section_for_address(address).name, address))
    return symbols


def import_libraries(pe, symbols, work):
    by_slot = {symbol.address: symbol.name for symbol in symbols if symbol.name.startswith("__imp_")}
    entries = defaultdict(list)
    for item in pe.imports:
        entries[item.dll].append(
            link.Import(
                item.dll, by_slot[item.slot].removeprefix("__imp_"), item.number, item.name is not None, item.slot
            )
        )
    paths = []
    for dll, imports in entries.items():
        path = work / f"{Path(dll).stem}.lib"
        path.write_bytes(link.import_library(dll, imports))
        paths.append(path)
    return paths


def redirect_call(objects, splits, symbols, call, caller, callee, replacement):
    owner = next(symbol for symbol in symbols if symbol.name == caller)
    unit = next(unit for unit in splits.units if any(r.start <= call < r.end for r in unit.ranges))
    path = objects / f"{unit.name}.obj"
    obj = coff.Object.read(path)
    (caller_symbol,) = [symbol for symbol in obj.named(caller) if symbol.defined]
    section = obj.section(caller_symbol.section)
    offset = caller_symbol.value + call - owner.address
    if section.data[offset : offset + 5] != b"\xe8\0\0\0\0":
        raise ValueError(f"{caller}: expected a direct call at 0x{call:x}")
    (reloc,) = [reloc for reloc in section.relocations if reloc.offset == offset + 1]
    if reloc.kind != coff.REL32 or obj.symbol(reloc.symbol).name != callee:
        raise ValueError(f"{caller}: expected a relocation to {callee} at 0x{call:x}")
    reloc.symbol = obj.external(replacement)
    obj.write(path)


def redirect_temp_creators(objects, splits, symbols):
    callee = "_create_temp_object"
    for address, caller, creator in TEMP_CREATORS:
        redirect_call(objects, splits, symbols, address, caller, callee, creator)
    for unit in splits.units:
        if not any(region.section == ".text" for region in unit.ranges):
            continue
        path = objects / f"{unit.name}.obj"
        obj = coff.Object.read(path)
        indices = {symbol.index for symbol in obj.named(callee)}
        changed = False
        for section in obj.sections:
            for reloc in section.relocations:
                if reloc.symbol not in indices:
                    continue
                offset = reloc.offset
                if reloc.kind != coff.REL32 or offset < 1 or section.data[offset - 1 : offset + 4] != b"\xe8\0\0\0\0":
                    raise ValueError(f"{unit.name}: expected a direct call to {callee}")
                reloc.symbol = obj.external("_retro_CreateTemp")
                changed = True
        if changed:
            obj.write(path)


def identity_object(objects, splits, symbols, banner, work):
    section_name = ".data"
    unit = next(
        unit
        for unit in splits.units
        if any(r.section == section_name and r.start <= BANNER_POINTER < r.end for r in unit.ranges)
    )
    region = unit.range(section_name)
    path = objects / f"{unit.name}.obj"
    obj = coff.Object.read(path)
    section = next(section for section in obj.sections if section.name == section_name)
    offset = BANNER_POINTER - region.start
    (reloc,) = [reloc for reloc in section.relocations if reloc.offset == offset]
    old_names = {symbol.name for symbol in symbols if symbol.address == BANNER_STRING}
    if (
        reloc.kind != coff.DIR32
        or obj.symbol(reloc.symbol).name not in old_names
        or section.data[offset : offset + 4] != bytes(4)
    ):
        raise ValueError("expected an absolute relocation for the RC1 compiler banner")
    reloc.symbol = obj.external(BANNER_SYMBOL)
    obj.write(path)

    identity = coff.Object()
    storage = identity.add_section(".rdata", 0x40100040, banner.encode("ascii") + b"\0")
    identity.add_symbol(BANNER_SYMBOL, 0, storage.number, 0, coff.EXTERNAL)
    path = work / "identity.obj"
    identity.write(path)
    return path


def build(stock, output, tools, clang, linker):
    settings = read_config(VERSION)
    base = read_config(settings["derived_from"])
    raw = stock.read_bytes()
    if hashlib.sha1(raw).hexdigest() != base["sha1"]:
        raise ValueError(f"{stock}: expected the Wii 1.0RC1 compiler with SHA-1 {base['sha1']}")
    runtime = stock.with_name("lmgr8c.dll").read_bytes()
    if output.resolve() == stock.resolve().parent:
        raise ValueError("Wii_Retro needs an output directory separate from Wii 1.0RC1")
    output.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix=".wii-retro-", dir=output) as directory:
        work = Path(directory).resolve()
        # The compiler's shared instruction-selection routines keep the existing width-selection edits.
        code = bytearray(raw)
        struct_copy.patch(code)
        source_image = work / "rc1.exe"
        source_image.write_bytes(code)
        pe = PEFile(source_image)
        layout = ROOT / base["codeview_layout"]
        symbols = link_symbols(pe, layout)
        splits = split_layout(pe, layout, symbols)
        config = work / "config" / VERSION
        config.mkdir(parents=True)
        (config / "config.json").write_text(
            json.dumps({"original": source_image.name, "sha1": hashlib.sha1(code).hexdigest()}), encoding="utf-8"
        )
        (config / "symbols.txt").write_text(formats.format_symbols(symbols), encoding="utf-8")
        (config / "splits.txt").write_text(formats.format_splits(splits), encoding="utf-8")

        objects = work / "obj"
        subprocess.run([tools, "split", VERSION, "--root", str(work), "--output", str(objects)], check=True)
        redirect_call(
            objects,
            splits,
            symbols,
            pretest.CALL,
            "_assemblefunction",
            "_optimizefinalbranches",
            "_retro_optimizefinalbranches",
        )
        redirect_call(
            objects,
            splits,
            symbols,
            vtables.CALL,
            "_CParser_ParseDeclaration",
            "_CMid_GlobalDeclarationActions",
            "_retro_GlobalDeclarationActions",
        )
        for call in (*INLINE_CALLS, *REFERENCE_CALLS, *SCHEDULER_CALLS):
            redirect_call(objects, splits, symbols, *call)
        redirect_temp_creators(objects, splits, symbols)
        identity = identity_object(objects, splits, symbols, settings["banner"], work)

        sources = Path(__file__).resolve().with_name("wii_retro")
        compiled = []
        for name in (
            "struct_copy", "drop_pretest", "template_vtables",
            "inline_locals", "reference_order", "aggregate_schedule",
        ):
            obj = work / f"{name}.obj"
            flags = wii_retro_pe.BUILD.split()[1:]
            subprocess.run([clang, *flags, str(sources / f"{name}.c"), "-o", str(obj)], check=True)
            compiled.append(obj)

        # Keep command length independent of the number of CodeView units.
        inputs = [objects / f"{unit.name}.obj" for unit in splits.units]
        inputs += [*compiled, identity, *import_libraries(pe, symbols, work), objects / "resources.res"]
        rsp = work / "link.rsp"
        rsp.write_text("".join(f'"{path.as_posix()}"\n' for path in inputs), encoding="utf-8")
        exe = work / "mwcceppc.exe"
        subprocess.run(
            [
                linker,
                "/nologo",
                "/nodefaultlib",
                "/safeseh:no",
                "/opt:noref,noicf",
                "/subsystem:console",
                "/machine:x86",
                "/entry:mainCRTStartup",
                "/timestamp:0",
                f"/out:{exe}",
                f"@{rsp}",
            ],
            check=True,
        )
        data = exe.read_bytes()
        for name, contents in (("lmgr8c.dll", runtime), ("mwcceppc.exe", data)):
            destination = output / name
            if not destination.exists() or destination.read_bytes() != contents:
                project.write_atomic(destination, contents)
        print(f"{output / 'mwcceppc.exe'}: SHA-1 {hashlib.sha1(data).hexdigest()}")


def main():
    config = read_config(VERSION)
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--stock", type=Path, default=ROOT / read_config(config["derived_from"])["original"])
    parser.add_argument("--out-dir", type=Path, default=(ROOT / config["executable"]).parent)
    parser.add_argument("--tools", default=os.environ.get("MWCC_TOOLS", "mwcc-tools"))
    parser.add_argument("--clang", default=os.environ.get("CLANG", "clang"))
    parser.add_argument("--linker", default=os.environ.get("LLD_LINK", "lld-link"))
    args = parser.parse_args()
    try:
        build(args.stock, args.out_dir, args.tools, args.clang, args.linker)
    except (ValueError, OSError, project.Error, subprocess.CalledProcessError) as error:
        parser.exit(1, f"{VERSION}: {error}\n")


if __name__ == "__main__":
    main()
