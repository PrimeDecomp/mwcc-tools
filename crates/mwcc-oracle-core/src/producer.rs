//! Build and validate the standalone 32-bit Rust producer object.
use crate::{coff, process_tree};
use anyhow::{Context, Result, ensure};
use std::{
    collections::BTreeSet,
    fs,
    path::Path,
    process::{Command, Stdio},
    thread,
    time::{Duration, Instant},
};
pub const ARGUMENT_HOOKS: [&str; 4] = [
    "_FunctionCalls_GenerateCall",
    "_assign_argument_locations",
    "_load_argument_registers",
    "_store_argument_on_stack",
];
pub const ARENA_HOOKS: [&str; 8] = [
    "_CompilerTools_ResetPool",
    "_freeaheap",
    "_freelheap",
    "_CompilerTools_ResetGallocAndPoolMemory",
    "_releasegheap",
    "_releaseheaps",
    "_initheaps",
    "_CompilerTools_InitHeaps",
];
pub const SOURCE_HOOKS: [&str; 3] = ["_CFunc_ParseFuncDef", "_fn_00513040", "_fn_00513240"];
pub const HOOKS: [&str; 41] = [
    "_CFunc_ParseFuncDef",
    "_fn_00513040",
    "_fn_00513240",
    "_CompilerTools_ResetPool",
    "_freeaheap",
    "_freelheap",
    "_CompilerTools_ResetGallocAndPoolMemory",
    "_releasegheap",
    "_releaseheaps",
    "_initheaps",
    "_CompilerTools_InitHeaps",
    "_fn_00460c90",
    "_fn_00507070",
    "_fn_00506f50",
    "_fn_004f7fb0",
    "_schedule_block",
    "_select_ready_coloring_node",
    "_fn_00436390",
    "_COptimizer_Optimize",
    "_Peephole_MergeAdjacentBlocks",
    "_fn_00454030",
    "_create_temp_object",
    "_fn_004601f0",
    "_fn_00460780",
    "_IRO_CommonSubs",
    "_move_common_sub",
    "_IroDump_Print",
    "_fn_00506cd0",
    "_generate_inline_statements",
    "_inline_statement",
    "_CInline_AddFunctionPrecNode",
    "_CInline_AddSpecialization",
    "_CTemplateNew_InstantiateInlineTemplateObject",
    "_CTemplateNew_InstantiateFunction",
    "_fn_0050f240",
    "_generate_inline_code",
    "_setup_inline_locals_and_arguments",
    "_FunctionCalls_GenerateCall",
    "_assign_argument_locations",
    "_load_argument_registers",
    "_store_argument_on_stack",
];
pub const SUPPORT: [&str; 19] = [
    "_OracleRust_SourceProbeV1",
    "_OracleRust_ArenaProbeV1",
    "_OracleRust_ExpressionProbeV1",
    "_OracleRust_ArgumentForceGpr",
    "_OracleRust_CounterProbeV6",
    "_OracleRust_BackendProbe",
    "_OracleRust_LinkName",
    "_OracleRust_PrintInstruction",
    "_OracleRust_Snapshot",
    "_OracleRust_DescribeExpression",
    "_cse_vregs",
    "_OracleRust_OriginFind",
    "_OracleRust_OriginWalk",
    "_OracleRust_PrintChain",
    "_OracleRust_InlineLine",
    // (the probes in gc2's sources, under ORACLE_PROBES: include/compiler/OracleProbes.h)
    "_OracleProbe_ArenaRequest",
    "_OracleProbe_ArenaCommit",
    "_OracleProbe_ArenaCallback",
    "_OracleProbe_SourceEmission",
];
pub const SOURCES: [&str; 19] = [
    "source_nodes.rs",
    "source_arenas.rs",
    "lifecycle.rs",
    "call_arguments.rs",
    "backend.rs",
    "lib.rs",
    "abi.rs",
    "local_call.rs",
    "graph.rs",
    "frame.rs",
    "pcode.rs",
    "schedule.rs",
    "blocks.rs",
    "expression.rs",
    "scalar.rs",
    "cse.rs",
    "origins.rs",
    "instantiation.rs",
    "bindings.rs",
];
pub const TRACE_SOURCES: [&str; 2] = ["lib.rs", "trace_output.rs"];
pub const TRACE_EXPORTS: [&str; 2] = ["_OracleTrace_Enabled", "_OracleTrace_Printf"];
pub const TRACE_IMPORTS: [&str; 5] = ["_getenv", "_fopen", "_vfprintf", "_fflush", "___files"];
pub const FRONTEND_SOURCES: [&str; 2] = ["lib.rs", "frontend.rs"];
pub fn frontend_hooks(version: &str) -> Result<[&'static str; 5]> {
    let names = match version {
        "GC_1_3" => [
            "_can_inline",
            "_inline_call_expression",
            "_CInline_DispatchNextDeferredNode",
        ],
        "GC_1_3_2" => ["_fn_0055ed70", "_fn_0055f350", "_fn_0055a7f0"],
        "GC_2_6" | "GC_2_7" => ["_fn_00560c80", "_fn_00561260", "_fn_0055c730"],
        _ => anyhow::bail!("unsupported Rust frontend version {version}"),
    };
    Ok([names[0], names[1], names[2], "_fn_0055e540", "_fn_0055e600"])
}
pub fn frontend_imports(version: &str) -> Result<Vec<String>> {
    let mut imports: Vec<String> = [
        "_OracleTrace_Printf",
        "_sprintf",
        "_getenv",
        "_OracleRust_LinkName",
        "_OracleRust_OriginFind",
        "_OracleRust_OriginWalk",
        "_OracleRust_PrintChain",
        "_OracleRust_InlineLine",
        "_copts",
        "_data_00582468",
        "_data_005dfb66",
        "_data_005dfb68",
        "_data_005dfb62",
        "_pending_prec_nodes",
        "_anyerrors",
    ]
    .into_iter()
    .map(str::to_owned)
    .collect();
    imports.extend(frontend_hooks(version)?.map(|name| format!("{name}__retail")));
    Ok(imports)
}
pub fn validate_frontend(data: &[u8], version: &str) -> Result<()> {
    let imports = frontend_imports(version)?;
    validate_surface(
        data,
        &frontend_hooks(version)?,
        &imports.iter().map(String::as_str).collect::<Vec<_>>(),
    )
}
pub const IMPORTS: [&str; 74] = [
    "_CFunc_ParseFuncDef__retail",
    "_fn_00513040__retail",
    "_fn_00513240__retail",
    "_evalMode",
    "_alloc_state",
    "_OracleRust_ArenaGlobals",
    "_CompilerTools_ResetPool__retail",
    "_freeaheap__retail",
    "_freelheap__retail",
    "_CompilerTools_ResetGallocAndPoolMemory__retail",
    "_releasegheap__retail",
    "_releaseheaps__retail",
    "_initheaps__retail",
    "_CompilerTools_InitHeaps__retail",
    "_abort",
    "_FunctionCalls_GenerateCall__retail",
    "_assign_argument_locations__retail",
    "_load_argument_registers__retail",
    "_store_argument_on_stack__retail",
    "_OracleRust_ArgumentForceGprRetail",
    "_fn_00460c90__retail",
    "_fn_00507070__retail",
    "_fn_00506f50__retail",
    "_fn_004fcb70",
    "_fn_004ceda0",
    "_fn_004fcb20",
    "_fn_004f7fb0__retail",
    "_locals",
    "_temporary_objects",
    "_getenv",
    "_OracleTrace_Printf",
    "_CMangler_GetLinkName",
    "_sprintf",
    "_gPCodeBlocks",
    "_gVirtualRegistersActive",
    "_schedule_block__retail",
    "_select_ready_coloring_node__retail",
    "_fn_00436390__retail",
    "_COptimizer_Optimize__retail",
    "_Peephole_MergeAdjacentBlocks__retail",
    "_gCurrentStatement",
    "_gCurrentBlock",
    "_fn_00454030__retail",
    "_create_temp_object__retail",
    "_fn_004601f0__retail",
    "_fn_00460780__retail",
    "_IRO_CommonSubs__retail",
    "_move_common_sub__retail",
    "_iro_dump_output",
    "_INT_005882b8",
    "_class_data_parts",
    "_iro_pointer_analysis",
    "_data_005875b8",
    "_linear_head",
    "_strncmp",
    "_fopen",
    "_vfprintf",
    "_fn_00506cd0__retail",
    "_generate_inline_statements__retail",
    "_inline_statement__retail",
    "_memset",
    "_pendingInlineWork",
    "_pending_prec_nodes",
    "_deferredInlineNodes",
    "_cscope_currentfunc",
    "_data_005dfb62",
    "_CInline_AddFunctionPrecNode__retail",
    "_CInline_AddSpecialization__retail",
    "_CTemplateNew_InstantiateInlineTemplateObject__retail",
    "_CTemplateNew_InstantiateFunction__retail",
    "_fn_0050f240__retail",
    "_generate_inline_code__retail",
    "_setup_inline_locals_and_arguments__retail",
    "_data_0058245a",
];
pub fn validate(data: &[u8]) -> Result<()> {
    validate_surface(
        data,
        &HOOKS.into_iter().chain(SUPPORT).collect::<Vec<_>>(),
        &IMPORTS,
    )
}
pub fn validate_trace(data: &[u8]) -> Result<()> {
    validate_surface(data, &TRACE_EXPORTS, &TRACE_IMPORTS)
}
fn validate_surface(data: &[u8], exports: &[&str], imports: &[&str]) -> Result<()> {
    let object = coff::Object::parse(data)?;
    let defined: BTreeSet<_> = object
        .symbols
        .values()
        .filter(|s| s.section > 0 && s.storage == 2)
        .map(|s| s.name.as_str())
        .collect();
    ensure!(
        defined == exports.iter().copied().collect(),
        "Rust producer exports do not match the migrated hooks: {defined:?}"
    );
    let undefined: BTreeSet<_> = object
        .symbols
        .values()
        .filter(|s| s.section == 0 && s.storage == 2)
        .map(|s| s.name.as_str())
        .collect();
    ensure!(
        undefined == imports.iter().copied().collect(),
        "Rust producer imports unexpected runtime helpers: {undefined:?}"
    );
    for section in object.sections {
        ensure!(
            section
                .relocations
                .iter()
                .all(|r| [0, 6, 0x14].contains(&r.kind)),
            "unsupported Rust producer relocation in {}",
            section.name
        );
    }
    Ok(())
}
pub fn build(root: &Path, output: &Path, rustc: &Path, timeout: Duration) -> Result<()> {
    build_object(root, output, rustc, timeout, false, None, false)
}
pub fn build_trace(root: &Path, output: &Path, rustc: &Path, timeout: Duration) -> Result<()> {
    build_object(root, output, rustc, timeout, true, None, false)
}
/// The hooks (or VERSION's frontend hooks) for the WebAssembly oracle (crate::wasm_oracle): wasm32 LLVM IR, whose
/// names the oracle's link takes to gc2's.
pub fn build_wasm(
    root: &Path,
    output: &Path,
    rustc: &Path,
    timeout: Duration,
    frontend: Option<&str>,
) -> Result<()> {
    if let Some(version) = frontend {
        frontend_hooks(version)?;
    }
    build_object(root, output, rustc, timeout, false, frontend, true)
}
pub fn build_frontend(
    root: &Path,
    output: &Path,
    rustc: &Path,
    timeout: Duration,
    version: &str,
) -> Result<()> {
    frontend_hooks(version)?;
    build_object(root, output, rustc, timeout, false, Some(version), false)
}
fn build_object(
    root: &Path,
    output: &Path,
    rustc: &Path,
    timeout: Duration,
    trace: bool,
    frontend: Option<&str>,
    wasm: bool,
) -> Result<()> {
    ensure!(!timeout.is_zero(), "Rust producer timeout must be positive");
    let root = root.canonicalize()?;
    let source = root.join("crates/mwcc-oracle-hooks/src/lib.rs");
    ensure!(
        source.is_file(),
        "Rust producer source missing: {}",
        source.display()
    );
    let output = root.join(output);
    let parent = output
        .parent()
        .context("Rust producer output has no parent")?;
    fs::create_dir_all(parent)?;
    let temporary = tempfile::NamedTempFile::new_in(parent)?;
    let log = crate::temporary::diagnostic_log()?;
    let stream = log.reopen()?;
    let mut command = Command::new(rustc);
    command.current_dir(&root).args([
        "--edition",
        "2024",
        "--target",
        if wasm {
            "wasm32-unknown-unknown"
        } else {
            "i686-pc-windows-msvc"
        },
        "--crate-name",
        "mwcc_oracle_hooks",
        "--crate-type",
        "lib",
        if wasm { "--emit=llvm-ir" } else { "--emit=obj" },
        "-D",
        "warnings",
        "-C",
        "panic=abort",
        "-C",
        "opt-level=2",
        "-C",
        "debuginfo=0",
        "-C",
        "codegen-units=1",
    ]);
    if trace {
        command.args(["--cfg", "feature=\"trace-output\""]);
    }
    if let Some(version) = frontend {
        let feature = match version {
            "GC_1_3" => "frontend-gc13",
            "GC_1_3_2" => "frontend-gc132",
            "GC_2_6" => "frontend-gc26",
            "GC_2_7" => "frontend-gc27",
            _ => unreachable!(),
        };
        command.args([
            "--cfg",
            "feature=\"frontend\"",
            "--cfg",
            &format!("feature=\"{feature}\""),
        ]);
    }
    command
        .arg(source)
        .arg("-o")
        .arg(temporary.path())
        .stdout(Stdio::from(stream.try_clone()?))
        .stderr(Stdio::from(stream));
    process_tree::prepare(&mut command);
    let target = if wasm {
        "wasm32-unknown-unknown"
    } else {
        "i686-pc-windows-msvc"
    };
    let mut child = command.spawn().with_context(|| {
        format!("launch rustc for {target} (install the target with rustup target add {target})")
    })?;
    let began = Instant::now();
    let status = loop {
        if let Some(status) = child.try_wait()? {
            break status;
        }
        if began.elapsed() >= timeout {
            process_tree::terminate(&mut child)?;
            anyhow::bail!("Rust producer compilation timed out");
        }
        thread::sleep(Duration::from_millis(10));
    };
    ensure!(
        status.success(),
        "Rust producer compilation failed:\n{}",
        String::from_utf8_lossy(&fs::read(log.path())?)
    );
    let data = fs::read(temporary.path())?;
    if wasm {
        // (its names are checked where the oracle is linked)
    } else if let Some(version) = frontend {
        validate_frontend(&data, version)?;
    } else if trace {
        validate_trace(&data)?;
    } else {
        validate(&data)?;
    }
    temporary.persist(output)?;
    Ok(())
}
