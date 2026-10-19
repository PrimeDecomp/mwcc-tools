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
         unsigned char flag(unsigned int x) { return (x >> 3) & 1; }\n",
    )
    .unwrap();
    let selection = Selection {
        inline: true,
        coloring: true,
        pcode: vec!["initial".into(), "presched".into(), "precolor".into()],
        sched: true,
        copyprop: true,
        peephole: true,
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
    let compiled = runner::compile(&CompileRequest {
        root: root.clone(),
        working_directory: work.path().to_path_buf(),
        source: source.display().to_string(),
        flags,
        version: Version::parse("GC_2_7").unwrap(),
        options: selection.trace_options(&work.path().join("iro.log")),
        overrides: vec![],
        check: false,
        original_fallback: false,
        cut_limit: None,
        merge_limit: None,
        keep_object: None,
        timeout: Duration::from_secs(120),
    })
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
    assert!(
        trace
            .pcode
            .iter()
            .flat_map(|d| &d.lines)
            .all(|l| !l.contains("op") || !l.contains("??")),
        "an opcode without its name"
    );
}
