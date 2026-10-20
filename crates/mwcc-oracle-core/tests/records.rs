//! mwdec's model of a trace (mwcc-trace's `records`), read from a compile in process with every stream it reads
//! (skipped when this checkout's GC 2.7 module is not built).
use mwcc_oracle_core::{
    runner::{self, CompileRequest},
    version::Version,
    wasm_build,
};
use mwcc_trace::records::{self, Selection};
use std::{fs, path::PathBuf, time::Duration};

#[test]
fn every_part_of_the_model_is_read_from_the_module() {
    let root = PathBuf::from(env!("CARGO_MANIFEST_DIR")).join("../..");
    if !root.join(wasm_build::module_output("GC_2_7")).is_file() {
        eprintln!("(skipped: the GC 2.7 module is not built)");
        return;
    }
    let work = tempfile::tempdir().unwrap();
    let source = work.path().join("model.cpp");
    fs::write(
        &source,
        "struct V { float x, y, z; };\n\
         inline float dot(const V& a, const V& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }\n\
         float length2(const V& a) { return dot(a, a); }\n\
         int sum(int* p, int n) { int s = 0; int t = n; for (int i = 0; i < n; i++) { s += p[i] * (i + 3); t = s; } return s + t; }\n\
         unsigned char flag(unsigned int x) { return (x >> 3) & 1; }\n\
         float reload(float* p, float* q) { float x = *p; *q = 0.0f; return x + *p; }\n",
    )
    .unwrap();
    let selection = Selection {
        inline: true,
        coloring: true,
        pcode: vec!["initial".into(), "presched".into(), "precolor".into()],
        sched: true,
        copyprop: true,
        peephole: true,
        alias: true,
        alias_queries: true,
        value_numbering: true,
        ..Selection::default()
    };
    let flags: Vec<String> = [
        "-nodefaults",
        "-proc",
        "gekko",
        "-fp",
        "hardware",
        "-O4,p",
        "-inline",
        "auto",
    ]
    .map(String::from)
    .into();
    let compile = |options| {
        runner::compile(&CompileRequest {
            root: root.clone(),
            working_directory: work.path().to_path_buf(),
            source: source.display().to_string(),
            flags: flags.clone(),
            version: Version::parse("GC_2_7").unwrap(),
            options,
            overrides: vec![],
            check: false,
            original_fallback: false,
            cut_limit: None,
            merge_limit: None,
            keep_object: None,
            timeout: Duration::from_secs(120),
        })
        .unwrap()
    };
    let compiled = compile(selection.trace_options(&work.path().join("iro.log")));
    // (the streams observe the compile: the object is the one a compile that traces nothing makes)
    assert_eq!(
        compiled.object_sha256,
        compile(Default::default()).object_sha256,
        "tracing changed the object"
    );
    std::fs::write(
        std::env::temp_dir().join("alias_trace_dump.txt"),
        &compiled.trace,
    )
    .unwrap();
    let trace = records::parse(&compiled.trace, &selection);
    assert!(!trace.inline.is_empty(), "no inlining decisions");
    assert!(
        trace.inline.iter().all(|d| d.reason != "unknown"),
        "an inlining decision without its reason"
    );
    assert!(
        trace.expand_order.iter().any(|f| f.contains("sum")),
        "{:?}",
        trace.expand_order
    );
    assert!(
        trace
            .rounds
            .iter()
            .any(|r| r.function.contains("sum") && r.class == "GPR"),
        "no colouring round for sum"
    );
    assert!(!trace.sched.is_empty(), "no scheduled blocks");
    for stage in ["initial", "presched", "precolor-gpr"] {
        assert!(
            trace
                .pcode
                .iter()
                .any(|d| d.stage == stage && !d.lines.is_empty()),
            "no PCode at {stage}"
        );
    }
    assert!(!trace.copyprop.is_empty(), "no copy propagation decisions");
    let table = trace
        .alias
        .iter()
        .rfind(|t| t.function.contains("sum"))
        .expect("no alias table for sum");
    assert!(
        table
            .aliases
            .iter()
            .any(|a| a.kind == records::AliasKind::Set && a.object.is_none()),
        "no alias set"
    );
    assert!(
        table
            .accesses
            .iter()
            .all(|a| table.aliases.iter().any(|b| b.index == a.alias)),
        "an access whose alias is not in the table"
    );
    assert!(
        table
            .accesses
            .iter()
            .any(|a| a.instruction.starts_with("lwz")),
        "no load among the accesses: {:?}",
        table.accesses
    );
    let numbering: Vec<_> = trace
        .value_numbering
        .iter()
        .filter(|p| p.function.contains("length2"))
        .collect();
    assert!(!numbering.is_empty(), "no value numbering of length2");
    let reload: Vec<_> = trace
        .value_numbering
        .iter()
        .filter(|p| p.function.contains("reload"))
        .collect();
    assert!(
        reload
            .iter()
            .flat_map(|p| &p.decisions)
            .any(|d| d.outcome == "kept"
                && d.reason
                    .as_deref()
                    .is_some_and(|r| r.contains("memory's value changed"))
                && d.killed_by
                    .as_deref()
                    .is_some_and(|k| k.starts_with("stfs"))),
        "the reload of *p is not kept for the store through q: {reload:?}"
    );
    assert!(
        numbering
            .iter()
            .flat_map(|p| &p.decisions)
            .any(|d| d.outcome == "kept" && d.instruction.starts_with("lfs")),
        "no load the value numbering kept: {numbering:?}"
    );
    assert!(
        trace
            .alias_queries
            .iter()
            .any(|q| q.kind == "may_alias" && !q.a.chars().all(|c| c.is_ascii_hexdigit())),
        "no may_alias query of a listed access"
    );
    assert!(
        trace
            .pcode
            .iter()
            .flat_map(|d| &d.lines)
            .all(|l| !l.contains("op") || !l.contains("??")),
        "an opcode without its name"
    );
}
