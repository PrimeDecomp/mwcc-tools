//! `MWCC_WASM_MEMORY_MB`: a run that needs more memory than it allows stops with an error, and one within it compiles.
//! Uses this checkout's GC 2.7 module (skipped when it is not built).
use mwcc_wasm::Request;
use std::{fs, path::PathBuf, time::Duration};

#[test]
fn a_run_past_the_limit_is_an_error() {
    let module =
        PathBuf::from(env!("CARGO_MANIFEST_DIR")).join("../../build/GC_2_7/wasm/mwcc.wasm");
    if !module.is_file() {
        eprintln!("(skipped: the GC_2_7 module is not built)");
        return;
    }
    // (before the first run reads it; this file's only test, so its process is this test's)
    unsafe { std::env::set_var(mwcc_wasm::MEMORY_MB, "64") };
    let work = tempfile::tempdir().unwrap();
    fs::write(
        work.path().join("small.c"),
        "int f(int a) { return a * 3; }\n",
    )
    .unwrap();
    // (one function of many statements: the compiler keeps all of them, and their code, at once)
    let mut large = String::from("int g(int* p) {\n    int s = 0;\n");
    for i in 0..60_000 {
        large.push_str(&format!("    s += p[{}] * {i};\n", i % 97));
    }
    large.push_str("    return s;\n}\n");
    fs::write(work.path().join("large.c"), large).unwrap();
    let program = work.path().join("mwcceppc.exe").display().to_string();
    let loaded = mwcc_wasm::load(&module).unwrap();
    let run = |source: &str| {
        let args: Vec<String> = [
            "-nodefaults",
            "-proc",
            "gekko",
            "-O4,p",
            "-c",
            source,
            "-o",
            "out.o",
        ]
        .iter()
        .map(|a| a.to_string())
        .collect();
        loaded.run(
            &Request {
                args: &args,
                directory: work.path(),
                environment: &[],
                program: &program,
                version_info: &loaded.version_info,
                timeout: Some(Duration::from_secs(300)),
                cancel: None,
                trace: false,
                overlay: None,
                transcode_sjis: false,
                record_reads: false,
            },
            &|| false,
        )
    };
    let small = run("small.c").unwrap();
    assert_eq!(
        small.exit_code,
        0,
        "{}",
        String::from_utf8_lossy(&small.console)
    );
    let error = format!(
        "{:?}",
        run("large.c").err().expect("the large compile stops")
    );
    assert!(
        error.contains("out of memory: more than 64 MiB (MWCC_WASM_MEMORY_MB)"),
        "{error}"
    );
}
