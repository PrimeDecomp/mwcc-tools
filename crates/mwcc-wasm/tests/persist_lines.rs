//! Line numbers in a persistent compile: a candidate's lines are numbered as in a normal compile of the context followed
//! by the candidate, so `-sym on` and `-g` objects and the messages are the same. Uses this checkout's GC 2.7 and 2.6 modules (each
//! skipped when it is not built).
use mwcc_wasm::{Overlay, Request};
use std::{fs, path::PathBuf, time::Duration};

fn module(version: &str) -> Option<PathBuf> {
    let module = PathBuf::from(env!("CARGO_MANIFEST_DIR"))
        .join(format!("../../build/{version}/wasm/mwcc.wasm"));
    if module.is_file() {
        Some(module)
    } else {
        eprintln!("(skipped: the {version} module is not built)");
        None
    }
}

const CONTEXT: &str = "#include \"v.h\"\n\
typedef struct S { int a; float b; } S;\n\
\n\
static int g(int x) {\n\
    return x * V;\n\
}\n\
extern S table[4];\n";

/// Twenty candidates of different shapes and lengths.
fn candidates() -> Vec<String> {
    (0..20)
        .map(|i| {
            let mut text = String::new();
            for _ in 0..(i % 4) {
                text.push_str("\n// a comment line\n");
            }
            text.push_str(&format!("int f{i}(int n) {{\n    int s = 0;\n"));
            for k in 0..(i % 5) {
                text.push_str(&format!(
                    "    s += g(n + {k});\n    if (s > {i}) s -= table[{}].a;\n",
                    k % 4
                ));
            }
            text.push_str("    return s;\n}\n");
            if i % 3 == 0 {
                text.push_str(&format!("float h{i}(S* p) {{ return p->b * {i}.0f; }}\n"));
            }
            text
        })
        .collect()
}

/// Each candidate compiled persistently after CONTEXT, and normally after it, by VERSION's module with EXTRA flags.
fn lines_match(version: &str, context: &str, extra: &[&str]) {
    let Some(module) = module(version) else {
        return;
    };
    let work = tempfile::tempdir().unwrap();
    fs::create_dir(work.path().join("inc")).unwrap();
    fs::write(work.path().join("inc/v.h"), "#define V 3\n").unwrap();
    let capacity = 8192;
    fs::write(
        work.path().join("a.c"),
        mwcc_wasm::persistent_source(context, capacity),
    )
    .unwrap();
    let args: Vec<String> = [
        "-nodefaults",
        "-proc",
        "gekko",
        "-O4,p",
        "-i",
        "inc",
        "-c",
        "a.c",
        "-o",
        "a.o",
    ]
    .iter()
    .chain(extra)
    .map(|a| a.to_string())
    .collect();
    let program = work.path().join("mwcceppc.exe").display().to_string();
    let loaded = mwcc_wasm::load(&module).unwrap();
    let request = |overlay: Option<&'static Overlay>| Request {
        args: &args,
        directory: work.path(),
        environment: &[],
        program: &program,
        version_info: &loaded.version_info,
        timeout: Some(Duration::from_secs(60)),
        cancel: None,
        trace: false,
        overlay,
        transcode_sjis: false,
        record_reads: false,
        command_line: None,
        shared_files: None,
    };
    let mut persistent = loaded
        .compiler
        .persistent(&request(None), capacity)
        .unwrap();
    // (and two that fail, on their third line and in a function after the first: the messages name the same lines)
    let mut candidates = candidates();
    candidates.push("int e0(void) {\n    int x = 1;\n    return x +;\n}\n".into());
    candidates.push(
        "int e1(void) { return 0; }\n\nint e2(int n) {\n    return n * undeclared;\n}\n".into(),
    );
    for (i, candidate) in candidates.iter().enumerate() {
        let fast = persistent
            .compile(candidate.as_bytes(), Some(Duration::from_secs(60)))
            .unwrap();
        // (the normal compile: the context and a line end, then the candidate, as the same file)
        let mut overlay = Overlay::new();
        let normal_source = format!("{}\n{candidate}", context.trim_end_matches('\n'));
        overlay.insert(work.path().join("a.c"), normal_source.into_bytes());
        let overlay: &'static Overlay = Box::leak(Box::new(overlay));
        let _ = fs::remove_file(work.path().join("a.o"));
        let outcome = loaded.run(&request(Some(overlay)), &|| false).unwrap();
        let console = String::from_utf8_lossy(&outcome.console);
        assert_eq!(
            (fast.exit_code, String::from_utf8_lossy(&fast.console)),
            (outcome.exit_code, console.clone()),
            "{version}: candidate {i}'s status and messages differ with {extra:?}"
        );
        assert_eq!(
            outcome.exit_code == 0,
            i < 20,
            "{version}: candidate {i}: {console}"
        );
        let normal = fs::read(work.path().join("a.o")).ok();
        assert!(
            fast.object == normal,
            "{version}: candidate {i} differs with {extra:?}"
        );
    }
}

#[test]
fn sym_on_objects_equal_a_normal_compile() {
    for version in ["GC_2_7", "GC_2_6"] {
        lines_match(version, CONTEXT, &["-sym", "on"]);
    }
}

#[test]
fn g_objects_equal_a_normal_compile() {
    for version in ["GC_2_7", "GC_2_6"] {
        lines_match(version, CONTEXT, &["-g"]);
    }
}

#[test]
fn a_one_line_context() {
    lines_match(
        "GC_2_7",
        "#include \"v.h\"
extern struct S { int a; float b; } table[4];
static int g(int x) { return x * V; }
typedef struct S S;",
        &["-g"],
    );
    lines_match(
        "GC_2_7",
        "typedef struct S { int a; float b; } S; extern S table[4]; static int g(int x) { return x; }",
        &["-g"],
    );
}
