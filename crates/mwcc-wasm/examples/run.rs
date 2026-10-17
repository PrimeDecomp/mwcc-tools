//! `cargo run -p mwcc-wasm --example run -- MODULE ARGS...`: run the compiler's module in the current directory, with
//! this process's environment and gc2's version information.
use mwcc_oracle_core::source_only;
use std::{env, fs, path::Path};

fn main() -> wasmtime::Result<()> {
    let mut args: Vec<String> = env::args().skip(1).collect();
    let module = args.remove(0);
    let gc2 = Path::new(env!("CARGO_MANIFEST_DIR")).join("../../gc2");
    let script = fs::read_to_string(gc2.join("src/driver/mwcceppc.rc"))?;
    let header = fs::read_to_string(gc2.join("include/version.h"))?;
    let icon = fs::read(gc2.join("src/driver/mwcceppc.ico"))?;
    let resources = source_only::compile_resources(&script, &header, 5, &icon)
        .map_err(wasmtime::Error::from_anyhow)?;
    let started = std::time::Instant::now();
    let compiler = mwcc_wasm::Compiler::new(&fs::read(module)?)?;
    let compiled = started.elapsed();
    let directory = env::current_dir()?;
    let environment: Vec<_> = env::vars().collect();
    let program = directory
        .join("mwcceppc.exe")
        .to_string_lossy()
        .into_owned();
    let outcome = compiler.run(&mwcc_wasm::Request {
        args: &args,
        directory: &directory,
        environment: &environment,
        program: &program,
        version_info: &resources[0].data,
    })?;
    use std::io::Write;
    std::io::stdout().write_all(&outcome.stdout)?;
    std::io::stderr().write_all(&outcome.stderr)?;
    eprintln!(
        "[module compiled in {compiled:?}, run in {:?}]",
        started.elapsed() - compiled
    );
    std::process::exit(outcome.exit_code);
}
