//! Time a compile in process with a given module: `module_bench MODULE TRACE REPEAT -- COMPILER-ARGS...`, run in the
//! compile's directory (TRACE: 0 or 1). Prints each run's time, the trace's size and the object's SHA-1.
use anyhow::{Context, Result};
use mwcc_oracle_core::{in_process, version::Version};
use sha1::{Digest, Sha1};
use std::{path::Path, time::Instant};

fn main() -> Result<()> {
    let args: Vec<String> = std::env::args().skip(1).collect();
    let split = args.iter().position(|a| a == "--").context("no --")?;
    let module = Path::new(&args[0]);
    let trace = args[1] == "1";
    let repeat: usize = args[2].parse()?;
    let compiler_args = &args[split + 1..];
    let root = Path::new(env!("CARGO_MANIFEST_DIR")).join("../..");
    // (BENCH_VERSION: another version than GC_2_7)
    let version =
        Version::parse(&std::env::var("BENCH_VERSION").unwrap_or_else(|_| "GC_2_7".into()))?;
    let program = root.join(format!(
        "build/compilers/GC/{}/mwcceppc.exe",
        version.compiler_directory()
    ));
    let output = compiler_args
        .iter()
        .position(|a| a == "-o")
        .map(|i| compiler_args[i + 1].clone());
    let environment: Vec<(String, String)> = std::env::vars()
        .filter(|(k, _)| k != "MWCC_ORACLE_TRACE_FILE")
        .collect();
    let loading = Instant::now();
    in_process::preload(module)?;
    eprintln!("load {:.0} ms", loading.elapsed().as_secs_f64() * 1e3);
    for _ in 0..repeat {
        let began = Instant::now();
        let outcome = in_process::run(
            &in_process::Run {
                root: &root,
                version,
                module,
                program: &program,
                args: compiler_args,
                directory: &std::env::current_dir()?,
                environment: &environment,
                timeout: None,
                trace,
                overlay: None,
                transcode_sjis: false,
            },
            &|| false,
        )?;
        let elapsed = began.elapsed();
        if let Ok(path) = std::env::var("BENCH_STDERR") {
            std::fs::write(path, &outcome.stderr)?;
        }
        let object = output
            .as_ref()
            .and_then(|o| std::fs::read(o).ok())
            .map(|data| format!("{:x}", Sha1::digest(data)))
            .unwrap_or_default();
        println!(
            "exit {} {:.0} ms, stderr {} bytes, object {object}",
            outcome.exit_code,
            elapsed.as_secs_f64() * 1e3,
            outcome.stderr.len()
        );
    }
    Ok(())
}
