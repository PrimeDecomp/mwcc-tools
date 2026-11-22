#![cfg(any(windows, feature = "wasm"))]

use mwcc_oracle_core::{birth_origin::ResultPart, source_arenas, source_nodes, value_history};
#[cfg(feature = "wasm")]
use mwcc_oracle_core::{in_process, version::Version};
use std::{fs, path::Path, time::Duration};

const SOURCE: &str = r#"
extern int external(int, int);
static inline int scaled(int n) { return n * 3 + 1; }
int example(int n) { external(n, 0); return external(scaled(n), n + 2); }

extern double external_real(int n);
extern long long external_wide(int n);
extern int* external_pointer(int n);
extern __vec2x32float__ external_paired(int n);
struct One { int a; };
struct Two { int a; int b; };
extern struct One external_aggregate(int n);
extern struct Two external_aggregate_pair(int n);
double result_float(int n) { return external_real(n) + 2.5; }
long long result_wide(int n) { return external_wide(n) + (long long)n; }
double load_indexed_float(const double* p, int i, double v) { return v + p[i]; }
long long load_indexed_wide(const long long* p, int i) { return p[i] + 1LL; }
double convert_several(int a, unsigned b, int c, unsigned d) {
    return (double)a + (double)b + (double)c + (double)d;
}
double convert_one(int n, double v) { return v + (double)n; }
extern volatile int global_words[64];
void copy_words(volatile int* dst) {
    dst[0] = global_words[0];
    dst[1] = global_words[0];
    dst[2] = global_words[0];
}
volatile int* word_address(void) { return global_words + 7; }
#pragma optimize_for_size on
int count_loop(volatile int* p) {
    int i;
    for (i = -7; i < 32767; ++i) *p += i;
    return *p;
}
int count_split_loop(volatile int* p) {
    int i;
    for (i = 0; i < 70001; ++i) *p ^= i;
    return *p;
}
int stride_loop(const volatile int* p, int n) {
    int i, sum = 0;
    for (i = 5; i < n; i += 2) sum ^= i * 16385 + p[i];
    return sum;
}
#pragma optimize_for_size reset
int* result_pointer(int n) { return external_pointer(n) + 1; }
__vec2x32float__ result_paired(int n) { return external_paired(n); }
int result_aggregate(int n) { return external_aggregate(n).a + n; }
int result_aggregate_pair(int n) {
    struct Two pair = external_aggregate_pair(n);
    return pair.a - pair.b;
}
"#;

const SOFT_SOURCE: &str = r#"
extern float borrow_float(int n);
extern double borrow_double(int n);
float result_soft_float(int n) { return borrow_float(n); }
double result_soft_double(int n) { return borrow_double(n); }
float add_soft_constant(float x) { return x + 1.23456f; }
"#;

const FLAGS: &[&str] = &[
    "-nodefaults",
    "-proc",
    "gekko",
    "-O4,p",
    "-inline",
    "auto",
    "-ipa",
    "file",
];

#[cfg(windows)]
#[test]
fn wii_rc1_windows_oracle_traces_while_preserving_the_object() {
    use mwcc_oracle_core::runner::{Invocation, TraceOptions, execute_process};

    let root = Path::new(env!("CARGO_MANIFEST_DIR")).join("../..");
    let build = root.join("gc2/build/Wii_1_0RC1");
    if !build.join("oracle/mwcceppc.exe").is_file() {
        eprintln!("(skipped: the Wii 1.0RC1 Windows oracle is not built)");
        return;
    }
    let work = tempfile::tempdir().unwrap();
    let input = work.path().join("fixture.c");
    let output = work.path().join("fixture.o");
    let compile =
        |executable: &Path, flags: &[String], options: &TraceOptions, trace_file: &Path| {
            let result = execute_process(Invocation {
                root: &root,
                executable,
                flags,
                source: "fixture.c",
                directory: work.path(),
                output: &output,
                trace_file,
                options,
                timeout: Duration::from_secs(60),
            })
            .unwrap();
            assert_eq!(result.exit_code, Some(0), "{}", result.diagnostics);
            (fs::read(&output).unwrap(), result.trace)
        };
    let plain = build.join("clang/mwcceppc.exe");
    let oracle = root.join("build/Wii_1_0RC1/oracle/mwcceppc.exe");
    let options = TraceOptions {
        backend: true,
        source_arenas: true,
        source_nodes: true,
        stages: true,
        names: true,
        ..TraceOptions::default()
    };
    let plain_trace = work.path().join("plain.trace");
    let defaults = TraceOptions::default();
    for (source, fp, check) in [
        (SOURCE, "hard", check_trace as fn(&str)),
        (SOFT_SOURCE, "soft", check_soft_trace),
    ] {
        fs::write(&input, source).unwrap();
        let flags: Vec<_> = FLAGS
            .iter()
            .copied()
            .chain(["-fp", fp])
            .map(String::from)
            .collect();
        let trace = work.path().join(format!("native-{fp}.trace"));
        let (plain, _) = compile(&plain, &flags, &defaults, &plain_trace);
        let (instrumented, trace) = compile(&oracle, &flags, &options, &trace);
        assert_eq!(plain, instrumented);
        check(&String::from_utf8(trace).unwrap());
    }
}

#[cfg(feature = "wasm")]
#[test]
fn wii_rc1_source_oracle_traces_while_preserving_the_object() {
    let root = Path::new(env!("CARGO_MANIFEST_DIR")).join("../..");
    let version = Version::Wii10Rc1;
    let wasm = root.join("build/Wii_1_0RC1/wasm");
    let instrumented = wasm.join("mwcc.wasm");
    if !instrumented.is_file() {
        eprintln!("(skipped: the Wii 1.0RC1 oracle module is not built)");
        return;
    }

    let work = tempfile::tempdir().unwrap();
    let input = work.path().join("fixture.c");
    let options = [
        ("MWCC_ORACLE_TRACE_FILE".into(), "rc1.trace".into()),
        ("MWCC_ORACLE_BACKEND".into(), "1".into()),
        ("MWCC_ORACLE_STAGES".into(), "1".into()),
        ("MWCC_ORACLE_SCHEDULE".into(), "1".into()),
        ("MWCC_ORACLE_NAMES".into(), "1".into()),
        ("MWCC_ORACLE_INLINE_COST".into(), "1".into()),
        ("MWCC_ORACLE_SOURCE_ARENAS".into(), "1".into()),
        ("MWCC_ORACLE_SOURCE_NODES".into(), "1".into()),
    ];
    let program = root.join("build/Wii_1_0RC1/oracle/mwcceppc.exe");
    let object = work.path().join("fixture.o");
    let plain = wasm.join("mwcceppc.wasm");
    let oracle = wasm.join("oracle.wasm");
    let compile = |source: &str, fp: &str, modules: &[(&Path, bool)], check: fn(&str)| {
        fs::write(&input, source).unwrap();
        let args: Vec<_> = FLAGS
            .iter()
            .copied()
            .chain(["-fp", fp, "-c", "fixture.c", "-o", "fixture.o"])
            .map(String::from)
            .collect();
        let mut baseline = None;
        for &(module, trace) in modules {
            let result = in_process::run(
                &in_process::Run {
                    root: &root,
                    version,
                    module,
                    program: &program,
                    args: &args,
                    directory: work.path(),
                    environment: &options,
                    timeout: Some(Duration::from_secs(60)),
                    trace,
                    overlay: None,
                    transcode_sjis: false,
                    command_line: None,
                    files: None,
                },
                &|| false,
            )
            .unwrap();
            assert_eq!(
                result.exit_code,
                0,
                "{}",
                String::from_utf8_lossy(&result.console)
            );
            let bytes = fs::read(&object).unwrap();
            if let Some(expected) = &baseline {
                assert_eq!(&bytes, expected);
            } else {
                baseline = Some(bytes);
            }
            fs::remove_file(&object).unwrap();
            if trace {
                let path = work.path().join("rc1.trace");
                check(&fs::read_to_string(&path).unwrap());
                fs::remove_file(path).unwrap();
            }
        }
    };
    compile(
        SOURCE,
        "hard",
        &[
            (&plain, false),
            (&instrumented, false),
            (&instrumented, true),
            (&oracle, true),
        ],
        check_trace,
    );
    compile(
        SOFT_SOURCE,
        "soft",
        &[(&plain, false), (&oracle, true)],
        check_soft_trace,
    );
}

fn check_trace(trace: &str) {
    assert!(
        trace.lines().any(|line| line.starts_with("inl.expand ")),
        "{trace}"
    );
    assert!(trace.lines().any(|line| line.starts_with("st.")), "{trace}");
    let values = value_history::parse(trace).unwrap();
    let function = values
        .functions
        .iter()
        .find(|f| f.link == "example")
        .unwrap();
    assert!(function.ended);
    assert!(
        function
            .changes
            .iter()
            .any(|change| change.site == 0x5b17d0)
    );
    assert!(
        function
            .changes
            .iter()
            .any(|change| change.site == 0x5b3f6f)
    );
    assert!(
        function
            .births
            .iter()
            .any(|birth| birth.site == 0x5b16fa && birth.object.is_some())
    );
    assert!(
        function
            .births
            .iter()
            .any(|birth| birth.site == 0x5fd244 && birth.call_result.is_some())
    );
    use ResultPart::{FirstWord, SecondWord, Single};
    check_call_results(
        &values,
        &[
            ("result_float", &[(0x5fcd3e, Single, 2, 8)]),
            ("result_paired", &[(0x5fcd3e, Single, 2, 8)]),
            ("result_pointer", &[(0x5fd244, Single, 11, 4)]),
            (
                "result_wide",
                &[(0x5fce22, FirstWord, 1, 8), (0x5fce37, SecondWord, 1, 8)],
            ),
            ("result_aggregate", &[(0x5fd314, Single, 4, 4)]),
            (
                "result_aggregate_pair",
                &[(0x5fd2a2, FirstWord, 4, 8), (0x5fd2b7, SecondWord, 4, 8)],
            ),
        ],
    );
    let wide = values
        .functions
        .iter()
        .find(|f| f.link == "result_wide")
        .unwrap();
    for site in [0x51a776, 0x51a783, 0x51ad4b] {
        assert!(
            wide.births.iter().any(|birth| birth.site == site),
            "{site:#x}"
        );
    }
    for (name, sites) in [
        ("result_float", &[0x5adde6][..]),
        ("load_indexed_float", &[0x5addf9]),
        ("load_indexed_wide", &[0x5ae12e, 0x5ae44c, 0x5ae466]),
        ("convert_several", &[0x5ad063, 0x5ad0ef]),
        ("copy_words", &[0x5af543]),
        ("word_address", &[0x5af543, 0x5af4c9]),
        ("count_loop", &[0x6507b1, 0x5afd07]),
        ("count_split_loop", &[0x6507b1, 0x5afd07]),
        ("stride_loop", &[0x5afdc7, 0x5afc4c]),
        (
            "convert_one",
            &[0x5ace66, 0x5ace7c, 0x5acf4f, 0x5aceda, 0x5acf96],
        ),
    ] {
        let function = values.functions.iter().find(|f| f.link == name).unwrap();
        let actual: Vec<_> = function
            .births
            .iter()
            .filter_map(|birth| sites.contains(&birth.site).then_some(birth.site))
            .collect();
        assert_eq!(actual, sites, "{name}");
    }
    assert!(
        values
            .functions
            .iter()
            .flat_map(|f| &f.births)
            .any(|birth| birth.site == 0x5ae82d)
    );
    let arenas = source_arenas::parse(trace).unwrap();
    assert!(matches!(
        arenas.events.first().map(|e| &e.observation),
        Some(source_arenas::EventKind::Begin { version: 6, .. })
    ));
    assert!(
        arenas.events.iter().any(|event| {
            matches!(&event.observation, source_arenas::EventKind::Allocation(a) if a.complete)
        }),
        "{:?}",
        arenas.observations
    );
    let nodes = source_nodes::parse(trace).unwrap();
    assert_eq!(nodes.version, Some(6));
    assert!(
        nodes
            .emissions
            .iter()
            .any(|emission| { emission.site == 0x509cac && emission.end_position.is_some() }),
        "{:?}",
        nodes.observations
    );
}

fn check_soft_trace(trace: &str) {
    use ResultPart::{FirstWord, SecondWord, Single};
    let values = value_history::parse(trace).unwrap();
    check_call_results(
        &values,
        &[
            ("result_soft_float", &[(0x5fd244, Single, 2, 4)]),
            (
                "result_soft_double",
                &[(0x5fce22, FirstWord, 2, 8), (0x5fce37, SecondWord, 2, 8)],
            ),
        ],
    );
    let function = values
        .functions
        .iter()
        .find(|f| f.link == "add_soft_constant")
        .unwrap();
    assert!(function.births.iter().any(|birth| birth.site == 0x5afbba));
}

type ExpectedCallResults<'a> = (&'a str, &'a [(u32, ResultPart, i32, i32)]);

fn check_call_results(values: &value_history::History, results: &[ExpectedCallResults<'_>]) {
    for &(name, expected) in results {
        let function = values.functions.iter().find(|f| f.link == name).unwrap();
        let actual: Vec<_> = function
            .births
            .iter()
            .filter_map(|birth| {
                let result = birth.call_result.as_ref()?;
                Some((birth.site, result.part, result.type_kind, result.type_size))
            })
            .collect();
        assert_eq!(actual, expected, "{name}");
    }
}

#[cfg(feature = "wasm")]
#[test]
fn wii_rc1_persistent_oracle_can_toggle_candidate_tracing() {
    let root = Path::new(env!("CARGO_MANIFEST_DIR")).join("../..");
    let module = root.join("build/Wii_1_0RC1/wasm/mwcc.wasm");
    if !module.is_file() {
        eprintln!("(skipped: the Wii 1.0RC1 oracle module is not built)");
        return;
    }
    let work = tempfile::tempdir().unwrap();
    let capacity = 1024;
    fs::write(
        work.path().join("a.c"),
        mwcc_wasm::persistent_source("static int half(int x) { return x / 2; }", capacity),
    )
    .unwrap();
    let loaded = mwcc_wasm::load(&module).unwrap();
    let args = [
        "-nodefaults",
        "-proc",
        "gekko",
        "-O4,p",
        "-ipa",
        "file",
        "-c",
        "a.c",
        "-o",
        "a.o",
    ]
    .map(String::from);
    let timeout = Some(Duration::from_secs(60));
    let program = work.path().join("mwcceppc.exe");
    let mut compiler = loaded
        .compiler
        .persistent(
            &mwcc_wasm::Run {
                args: &args,
                directory: work.path(),
                environment: &[("MWCC_ORACLE_NAMES".into(), "1".into())],
                program: program.to_str().unwrap(),
                version_info: &loaded.version_info,
                timeout,
                cancel: None,
                trace: false,
                overlay: None,
                transcode_sjis: false,
                record_reads: false,
                command_line: None,
                shared_files: None,
            },
            capacity,
        )
        .unwrap();
    let candidate = b"int f(int x) { return half(x) + 1; }\n";
    let off = compiler.compile_traced(candidate, timeout, false).unwrap();
    let on = compiler.compile_traced(candidate, timeout, true).unwrap();
    assert_eq!(
        off.exit_code,
        0,
        "{}",
        String::from_utf8_lossy(&off.console)
    );
    assert_eq!(on.exit_code, 0, "{}", String::from_utf8_lossy(&on.console));
    assert!(off.object.is_some());
    assert_eq!(off.object, on.object);
    assert!(off.trace.is_empty());
    assert!(String::from_utf8_lossy(&on.trace).contains("inl.expand "));
}
