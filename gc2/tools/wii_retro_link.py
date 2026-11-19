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
from mw import coff, formats, link, project
from mw.pe import PEFile
from wii_retro_abi import CLANG_FLAGS, VTABLE_CALL, VTABLE_SYMBOLS

ROOT = project.ROOT.parent
VERSION = "Wii_Retro"
ALIGNMENT_FUNCTION = "_CMach_GetObjectAlignment"
REPLACEMENT_NAMES = {
    "fn_0057f9d0": "_retro_ClearIntrinsic",
    "fn_0057fad0": "_retro_CopyIntrinsic",
    "fn_0046d690": "_RC1_AssignLocals",
    "fn_0046d8b0": "_RC1_ReassignLocals",
    "fn_005be950": "_RC1_MayAliasPair",
    "fn_005bec90": "_RC1_RefreshAliasMembership",
    "fn_005bee90": "_RC1_AddAliasAncestors",
    "fn_006657e0": "_RC1_GatherInstructionAlias",
    "fn_00665940": "_RC1_ReachingOperandAlias",
    "fn_005c1aa0": "_RC1_InstantiateStoredDefinition",
    "fn_005c1e40": "_RC1_InstantiateStoredStatement",
    "fn_005c22d0": "_RC1_InstantiateMemberInitializers",
    "fn_005c2380": "_RC1_InstantiateStaticDeclaration",
    "fn_005c24c0": "_RC1_InstantiateLocalDeclaration",
    "fn_005c2740": "_RC1_UpdateVLACleanups",
    "fn_005c2860": "_RC1_InstantiateStoredInitializer",
    "fn_005c28d0": "_RC1_InstantiateInitializerTree",
    "fn_005c2a70": "_RC1_InstantiateCleanups",
    "fn_005c2b10": "_RC1_ResolveDependentCleanup",
    "fn_005b6ad0": "_RC1_ScheduleBlock",
    "fn_005b6cb0": "_RC1_SelectInstruction",
    "fn_005b6e50": "_RC1_FindSuccessors",
    "fn_005b70a0": "_RC1_SerializeStore",
    "fn_005b7490": "_RC1_SerializeRegister",
    "fn_005b7580": "_RC1_AddSuccessor",
    "fn_005b7670": "_RC1_InitDependencies",
    "fn_0065fb30": "_RC1_Initialize7400Timing",
}
ASSIGNMENT_FUNCTION = "_gen_ASS"
ASSIGNMENT_JUMP_TABLE = (0x675BB0, 0x675BBC)
CONDITIONAL_FUNCTION = "_gen_COND"
CONDITIONAL_JUMP_TABLES = (0x675B10, 0x675B74)
REGISTER_FUNCTIONS = (
    "_Registers_ClassForType", "_Registers_GetVarInfo", "_assign_register_by_type", "_obtain_nonvolatile_register",
    "_retain_register", "_assign_register_to_variable", "_assign_GPR_pair",
)
REGISTER_CLASS_AND_INFO_TABLES = (0x67B878, 0x67B8BC)
REGISTER_PAIR_TABLES = (0x67B8BC, 0x67B8DC)
REGISTER_ASSIGNMENT_TABLES = (0x67B8DC, 0x67B8FC)
REGISTER_SYMBOLS = {"RC1_RegisterInfo_source_file": 0x6DB82E, "RC1_Registers_source_file": 0x6DC02C}
SCHEDULER_FUNCTIONS = (
    "_scheduleinstructions", "_may_alias", "_may_alias_object", "_may_alias_worst_case",
    "_instr_may_alias_alias", "_uniquely_aliases", "_instr_must_alias_alias",
    "_gather_alias_info", "_make_distinct_virtual_alias", "_propagatealiasinfo",
    "_is_const_alias_runtime_call", "_canidate_reaches_use", "_candidatealias",
)
ALIAS_JUMP_TABLE = (0x67DF2C, 0x67DF50)
SCHEDULER_OPERAND_JUMP_TABLE = (0x67D94C, 0x67D960)
PCODE_EXPANSION_FUNCTION = "_pcode_expand_pseudo_ops"
MEMORY_WIDTH_FUNCTION = "_nbytes_loaded_or_stored_by"
MEMBER_INSTANTIATION_FUNCTION = "_CTempl_InstantiateMember"
MEMBER_INSTANTIATION_JUMP_TABLE = (0x67E114, 0x67E128)
STORED_STATEMENT_JUMP_TABLE = (0x67E128, 0x67E168)
STORED_INITIALIZER_JUMP_TABLE = (0x67E168, 0x67E174)
STORED_CLEANUP_JUMP_TABLE = (0x67E174, 0x67E1CC)
PCODE_EXPANSION_JUMP_TABLE = (0x67D0A4, 0x67D0B0)
PCODE_FORMAT_JUMP_TABLE = (0x67D1CC, 0x67D324)
PCODE_SYMBOLS = {"RC1_PCodeInfo_source_file": 0x6DBD96, "RC1_spe_register_pairs": 0x74F269}
CONDITIONAL_SYMBOLS = {
    "RC1_next_gpr": 0x758288,
    "RC1_next_fpr": 0x758284,
    "RC1_next_vr": 0x758280,
    "RC1_FloatBinaryOperator": 0x51FB20,
    "RC1_TryConditionalCompare": 0x523740,
    "RC1_MatchConditionalAbsolute": 0x523AD0,
    "RC1_ConditionalConstantKind": 0x523A00,
    "RC1_LogicalExpressionNoBranch": 0x51ED30,
}
ASSIGNMENT_SYMBOLS = {
    "RC1_processor": 0x74F270,
    "RC1_soft_float": 0x74F28F,
    "RC1_use_paired_moves_for_double": 0x74F2C0,
    "RC1_gprfloatcopy": 0x74F2D6,
    "RC1_postincrement_object": 0x718340,
    "RC1_postincrement_end": 0x718344,
    "RC1_postincrement_start": 0x718348,
    "RC1_InstrSelection_source_file": 0x6C4E78,
    "RC1_CanMoveAssignmentBeforePostincrement": 0x525E70,
}
ALIGNMENT_OPTIONS = {
    "RC1_structalignment": 0x74F4C0,
    "RC1_oldalignment": 0x74F444,
    "RC1_GetQUALalign": 0x4F4DC0,
    "RC1_GetQualifiedTypeAlign": 0x4F2760,
    "RC1_AllocationAlignment": 0x4F4BB0,
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
}
REFERENCE_CALLS = (
    (0x58D5B8, "_IRO_Optimizer", "_IRO_Linearize", "_retro_Linearize"),
    (0x58DEB0, "_IRO_Optimizer", "_IRO_CopyAndConstantPropagation", "_retro_CopyAndConstantPropagation"),
    (0x58E110, "_IRO_Optimizer", "_IRO_CopyAndConstantPropagation", "_retro_CopyAndConstantPropagation"),
    (0x58D98F, "_IRO_Optimizer", "_IRO_Delinearize", "_retro_Delinearize"),
    (0x46A7AB, "_CodeGen_Generator", "_RC1_AssignLocals", "_retro_AssignLocals"),
    (0x46A78C, "_CodeGen_Generator", "_RC1_ReassignLocals", "_retro_ReassignLocals"),
)
DEFERRED_EXPORT_SYMBOLS = {
    "RC1_IsCopyConstructor": 0x555740,
    "RC1_defer_codegen": 0x74F447,
    "RC1_deferred_actions": 0x70FF46,
    "RC1_deferred_exports": 0x70FF42,
    "RC1_pending_dummy_functions": 0x70FF3A,
    "RC1_RewriteDefinitionExpression": 0x47EFC0,
    "RC1_RewriteGenerationExpression": 0x47EAB0,
    "RC1_ProcessGenerationStatements": 0x47EBE0,
    "RC1_generation_needs_statement_pass": 0x70FF35,
    "RC1_nosyminline": 0x74F4D1,
    "RC1_filesyminfo": 0x74F52F,
    "RC1_process_deferred_after_errors": 0x74F484,
    "RC1_CMiddleLayer_source_file": 0x6A82E0,
    "RC1_CCallGraph_source_file": 0x6DC1D4,
    "RC1_call_graph_tail": 0x752F74,
    "RC1_call_graph_next_index": 0x752F78,
    "RC1_allow_weak_graph_roots": 0x74F481,
    "RC1_CTemplateFuncInst_source_file": 0x6DC1F0,
    "RC1_CTemplateTools_source_file": 0x6C519C,
    "RC1_CTemplateClass_source_file": 0x6E3548,
    "RC1_CDecl_source_file": 0x6C4E98,
    "RC1_vla_registry": 0x71839A,
    "RC1_pending_array_initializers": 0x722B18,
    "RC1_warn_no_side_effect": 0x74F4A8,
}
DEFERRED_EXPORT_CALLS = (
    (0x4589AA, "_CParser_Setup", "_CMid_Setup", "_retro_MidSetup"),
    (0x47BE90, "_CMid_Finish", "fn_0047bf90", "_retro_RunEndOfFileActions"),
    (0x47BEDF, "_CMid_Finish", "fn_0047bf90", "_retro_RunEndOfFileActions"),
    (0x47BEE4, "_CMid_Finish", "fn_0047d400", "_retro_EmitInitializers"),
    (0x47BEE9, "_CMid_Finish", "fn_0047bf90", "_retro_RunEndOfFileActions"),
    (0x47CE80, "fn_0047ce20", "_CExcept_CallGraphExceptionOptimizer", "_retro_OrderIPAFunctions"),
    (0x47CEF0, "fn_0047ce20", "_CMid_GenerateCGNode", "_retro_GenerateCGNode"),
    (0x47EAD1, "fn_0047eab0", "_CMid_ObjectAddrRef", "_retro_RecordFirstReference"),
    (0x4BFAB1, "_Assembler", "_CMid_DefineFunction", "_retro_DefineFunction"),
    (0x506450, "_CFunc_GenerateDummyCtorFunc", "_CMid_DefineFunction", "_retro_DefineFunction"),
    (0x50663A, "_CFunc_GenerateSingleExprFunc", "_CMid_DefineFunction", "_retro_DefineFunction"),
    (0x508027, "_CFunc_Gen", "_CMid_DefineFunction", "_retro_DefineFunction"),
    (0x5C1012, "_CTempl_Instantiate", "_CTempl_GenClassInstance", "_retro_GenClassInstance"),
    (0x5C1052, "_CTempl_Instantiate", "_CTempl_GenClassInstance", "_retro_GenClassInstance"),
    (0x552F4C, "fn_00552e50", "_CInit_DeclareData", "_retro_DeclareVTable"),
)
IR_STREAM_SYMBOLS = {
    "RC1_ir_local_count": 0x718364,
    "RC1_ir_label_count": 0x718370,
    "RC1_ir_local_references": 0x71836C,
    "RC1_ir_local_objects": 0x718368,
    "RC1_ir_unpack_code": 0x751ABC,
    "RC1_ir_pack_flag": 0x76C036,
    "RC1_CIRStream_source_file": 0x6C4E8C,
    "RC1_PackFileReference": 0x52F180,
    "RC1_PackObjectList": 0x52AF20,
    "RC1_PackStatements": 0x52B520,
}
SCHEDULER_SYMBOLS = {
    "RC1_scheduling_cpu": 0x74F272,
    "RC1_altivec_model": 0x74F27E,
    "RC1_scheduler_model": 0x71E198,
    "RC1_critical_path": 0x71E19C,
    "RC1_default_successor": 0x71E19E,
    "RC1_volatile_refs": 0x71E1A2,
    "RC1_side_effects": 0x71E1A6,
    "RC1_memory_defs": 0x71E1AA,
    "RC1_memory_uses": 0x71E1AE,
    "RC1_register_defs": 0x71E1B2,
    "RC1_register_uses": 0x71E1C6,
    "RC1_check_disjoint_memory": 0x74F2F8,
    "RC1_optimize_size": 0x74F4DA,
    "RC1_DisjointMemory": 0x5B7190,
    "RC1_7400_timing_records": 0x6EDEDE,
    "RC1_7400_timing_initialized": 0x736110,
    "RC1_7400_instruction_timing": 0x736111,
    "RC1_alias_count": 0x722AF4,
    "RC1_gathered_aliases": 0x722AF0,
    "RC1_alias_scratch": 0x722B00,
    "RC1_alias_scratch_aux": 0x722AFC,
    "RC1_Scheduler_source_file": 0x6DC06C,
    "RC1_Alias_source_file": 0x6DC110,
    "RC1_optimization_level": 0x74F4D9,
    "RC1_alias_propagation_settings": 0x6F3D00,
    "RC1_c99": 0x74F456,
    "RC1_cplusplus": 0x74F434,
    "RC1_indirect_aliasing": 0x74F457,
}
SCHEDULER_CALLS = (
    (0x46B3E4, "_CodeGen_Generator", "_scheduleinstructions", "_retro_scheduleinstructions"),
    (0x46B335, "_CodeGen_Generator", "_scheduleinstructions", "_retro_scheduleinstructions"),
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

    removed_names = {
        ALIGNMENT_FUNCTION, ASSIGNMENT_FUNCTION, CONDITIONAL_FUNCTION, PCODE_EXPANSION_FUNCTION, MEMORY_WIDTH_FUNCTION,
        "_pcsetsideeffects", "_vformatpcode",
        MEMBER_INSTANTIATION_FUNCTION, "_CTemplTool_InstantiateExprList", "_CTemplTool_FixTypedefObject",
        "_CTemplTool_InstantiateMemberVar", "_CTemplTool_GetTemplClassInst",
        "_CTemplTool_Template2TemplClass", "_CTemplClass_GetMasterTemplate",
        "_CTemplTool_InstClass2Template", "_CTemplTool_Class2TemplClassInst", "_CTemplTool_TemplInst2TemplClassInst",
        "_CTemplClass_NewInstance", "_CTemplClass_GetInstance",
        "_CError_LockErrorSourceRef", "_CError_UnlockErrorSourceRef",
        "_CDecl_RemoveVLAInfo", "_CDecl_FindVLAInfo", "_CDecl_RegisterVLAInfo", "_CDecl_DiscardVLAInfos",
        *REPLACEMENT_NAMES.values(), *REGISTER_FUNCTIONS, *SCHEDULER_FUNCTIONS,
    }
    removed = sorted((symbol.address, symbol.address + symbol.size, symbol.name)
                     for symbol in symbols if symbol.name in removed_names)
    if len(removed) != len(removed_names):
        raise ValueError("missing replaced function from the CodeView symbols")
    removed_by_section = {
        ".text": removed,
        # These stock jump tables point into the functions being replaced.
        ".rdata": [
            (*CONDITIONAL_JUMP_TABLES, "gen_COND jump tables"),
            (*ASSIGNMENT_JUMP_TABLE, "gen_ASS jump table"),
            (*REGISTER_CLASS_AND_INFO_TABLES, "register class and variable info jump tables"),
            (*REGISTER_PAIR_TABLES, "assign_GPR_pair jump tables"),
            (*REGISTER_ASSIGNMENT_TABLES, "assign_register_by_type jump tables"),
            (*PCODE_EXPANSION_JUMP_TABLE, "pcode_expand_pseudo_ops jump table"),
            (*PCODE_FORMAT_JUMP_TABLE, "vformatpcode jump table"),
            (*SCHEDULER_OPERAND_JUMP_TABLE, "scheduler operand jump table"),
            (*ALIAS_JUMP_TABLE, "alias query jump table"),
            (*MEMBER_INSTANTIATION_JUMP_TABLE, "member instantiation jump table"),
            (*STORED_STATEMENT_JUMP_TABLE, "stored statement jump table"),
            (*STORED_INITIALIZER_JUMP_TABLE, "stored initializer jump table"),
            (*STORED_CLEANUP_JUMP_TABLE, "stored cleanup jump table"),
        ],
    }
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
            pieces = []
            begin = first
            for removed_start, removed_end, symbol in removed_by_section.get(name, ()):
                if first <= removed_start < last:
                    if removed_end > last:
                        raise ValueError(f"{symbol} crosses a CodeView range")
                    pieces.append((begin, removed_start))
                    begin = removed_end
            pieces.append((begin, last))
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
        symbol.name = REPLACEMENT_NAMES.get(symbol.name, symbol.name)
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
        VTABLE_SYMBOLS | ALIGNMENT_OPTIONS | ASSIGNMENT_SYMBOLS | CONDITIONAL_SYMBOLS | REGISTER_SYMBOLS |
        PCODE_SYMBOLS |
        INLINE_SYMBOLS | REFERENCE_SYMBOLS | DEFERRED_EXPORT_SYMBOLS | IR_STREAM_SYMBOLS | SCHEDULER_SYMBOLS
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
    redirect_direct_calls(objects, splits, callee, "_retro_CreateTemp")


def redirect_direct_calls(objects, splits, callee, replacement):
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
                reloc.symbol = obj.external(replacement)
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
        source_image = work / "rc1.exe"
        source_image.write_bytes(raw)
        pe = PEFile(source_image)
        layout = ROOT / base["codeview_layout"]
        symbols = link_symbols(pe, layout)
        splits = split_layout(pe, layout, symbols)
        config = work / "config" / VERSION
        config.mkdir(parents=True)
        (config / "config.json").write_text(
            json.dumps({"original": source_image.name, "sha1": base["sha1"]}), encoding="utf-8"
        )
        (config / "symbols.txt").write_text(formats.format_symbols(symbols), encoding="utf-8")
        (config / "splits.txt").write_text(formats.format_splits(splits), encoding="utf-8")

        objects = work / "obj"
        subprocess.run([tools, "split", VERSION, "--root", str(work), "--output", str(objects)], check=True)
        redirect_call(
            objects,
            splits,
            symbols,
            0x46B1A5,
            "_CodeGen_Generator",
            PCODE_EXPANSION_FUNCTION,
            "_retro_expand_pseudo_ops",
        )
        redirect_call(
            objects,
            splits,
            symbols,
            VTABLE_CALL,
            "_CParser_ParseDeclaration",
            "_CMid_GlobalDeclarationActions",
            "_retro_GlobalDeclarationActions",
        )
        for call in (*INLINE_CALLS, *REFERENCE_CALLS, *DEFERRED_EXPORT_CALLS, *SCHEDULER_CALLS):
            redirect_call(objects, splits, symbols, *call)
        redirect_direct_calls(objects, splits, "_CMach_ArgumentAlignment", "_retro_ArgumentAlignment")
        redirect_direct_calls(objects, splits, "_CMid_ObjectAddrRef", "_retro_ObjectAddrRef")
        redirect_direct_calls(objects, splits, "_CMid_GetFuncData", "_retro_GetFuncData")
        redirect_direct_calls(objects, splits, "_CMid_SetFuncData", "_retro_SetFuncData")
        redirect_direct_calls(objects, splits, "_CMid_FreeFuncData", "_retro_FreeFuncData")
        redirect_direct_calls(objects, splits, "_CCallGraph_NewNode", "_retro_NewGraphNode")
        redirect_direct_calls(objects, splits, "_CCallGraph_NewInitCodeNode", "_retro_NewInitCodeNode")
        redirect_direct_calls(objects, splits, "_CCallGraph_Renumber", "_retro_RenumberGraph")
        redirect_direct_calls(objects, splits, "_CCallGraph_RemoveNode", "_retro_RemoveGraphNode")
        redirect_direct_calls(objects, splits, "_CCallGraph_RemoveDeadNodes", "_retro_RemoveDeadGraphNodes")
        redirect_direct_calls(objects, splits, "_CCallGraph_Compute", "_retro_ComputeGraph")
        redirect_direct_calls(objects, splits, "_CCallGraph_ColorUses", "_retro_ColorGraphUses")
        redirect_direct_calls(objects, splits, "_CCallGraph_Unmark", "_retro_UnmarkGraph")
        redirect_direct_calls(objects, splits, "_CCallGraph_UnmarkAndUnflag", "_retro_UnmarkAndUnflagGraph")
        redirect_direct_calls(objects, splits, "_CCallGraph_IsDefined", "_retro_IsGraphNodeDefined")
        redirect_direct_calls(objects, splits, "_CCallGraph_GetIR", "_retro_GetGraphIR")
        redirect_direct_calls(objects, splits, "_CCallGraph_SetIR", "_retro_SetGraphIR")
        redirect_direct_calls(objects, splits, "fn_0052ed70", "_retro_PackObjectReference")
        redirect_direct_calls(objects, splits, "fn_0052ec90", "_retro_UnpackObjectReference")
        redirect_direct_calls(objects, splits, "_CIRStream_PackCode", "_retro_PackCode")
        redirect_direct_calls(objects, splits, "_CTempl_GenClassInstance", "_retro_GenerateClassMembers")
        redirect_direct_calls(objects, splits, "_CTempl_GenMemberInstance", "_retro_GenerateMemberInstance")
        redirect_direct_calls(objects, splits, "_CTempl_Instantiate", "_retro_InstantiateTemplateDefinitions")
        redirect_direct_calls(objects, splits, "_CTempl_GenFuncInstance", "_retro_GenerateFunctionInstance")
        redirect_temp_creators(objects, splits, symbols)
        identity = identity_object(objects, splits, symbols, settings["banner"], work)

        compiled = []
        for relative in settings["sources"]:
            source = ROOT / relative
            obj = work / f"{source.stem}.obj"
            defines = ["-DWII_RETRO_RELINK"] if source.name == "template_vtables.c" else []
            subprocess.run([clang, *CLANG_FLAGS, *defines, str(source), "-o", str(obj)], check=True)
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
