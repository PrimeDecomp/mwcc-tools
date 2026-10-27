//! How much of a short compile is instantiation: `cargo run --release -p mwcc-wasm --example instantiate_bench --
//! <mwcc.wasm> [runs] [-- compiler arguments]`. Without arguments, compiles a one-line C file. Prints the mean
//! instantiation and run times.
use mwcc_wasm::Request;
use std::time::Duration;

fn main() {
    let args: Vec<String> = std::env::args().skip(1).collect();
    let module = args.first().expect("the module");
    let runs: usize = args.get(1).and_then(|n| n.parse().ok()).unwrap_or(200);
    let work = tempfile::tempdir().unwrap();
    std::fs::write(
        work.path().join("small.c"),
        "int f(int a) { return a * 3; }\n",
    )
    .unwrap();
    let compiler_args: Vec<String> = match args.iter().position(|a| a == "--") {
        Some(i) => args[i + 1..].to_vec(),
        None => [
            "-nodefaults",
            "-proc",
            "gekko",
            "-O4,p",
            "-c",
            "small.c",
            "-o",
            "out.o",
        ]
        .iter()
        .map(|a| a.to_string())
        .collect(),
    };
    let directory = match args.iter().position(|a| a == "--") {
        Some(_) => std::env::current_dir().unwrap(),
        None => work.path().to_path_buf(),
    };
    let loaded = mwcc_wasm::load(std::path::Path::new(module)).unwrap();
    let program = directory.join("mwcceppc.exe").display().to_string();
    let environment: Vec<(String, String)> = std::env::vars().collect();
    let (mut instantiate, mut run) = (Duration::ZERO, Duration::ZERO);
    for i in 0..runs + 10 {
        let outcome = loaded
            .run(
                &Request {
                    args: &compiler_args,
                    directory: &directory,
                    environment: &environment,
                    program: &program,
                    version_info: &loaded.version_info,
                    timeout: Some(Duration::from_secs(60)),
                    cancel: None,
                    trace: false,
                    overlay: None,
                    transcode_sjis: false,
                    record_reads: false,
                    command_line: None,
                    shared_files: None,
                },
                &|| false,
            )
            .unwrap();
        assert_eq!(
            outcome.exit_code,
            0,
            "{}",
            String::from_utf8_lossy(&outcome.console)
        );
        // (the first ten warm up)
        if i >= 10 {
            instantiate += outcome.instantiate;
            run += outcome.run;
        }
    }
    let ms = |d: Duration| d.as_secs_f64() * 1000.0 / runs as f64;
    println!(
        "{runs} runs: instantiate {:.2} ms, run {:.2} ms",
        ms(instantiate),
        ms(run)
    );
}
