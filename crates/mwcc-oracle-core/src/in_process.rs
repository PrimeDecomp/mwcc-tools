//! The compilers run in this process: for the versions gc2's sources build (crate::wasm_build), a run of the stock
//! compiler (`build/compilers/GC/<dir>/mwcceppc.exe`) or of the oracle (`build/<version>/oracle/mwcceppc.exe`) runs
//! its WebAssembly module instead, when the module is built: no process, no Windows executable. `runner` dispatches
//! here; `MWCC_IN_PROCESS=0` turns it off.
//!
//! A module is compiled once per process (and again when its file changes). Each run instantiates it afresh, with
//! the environment a process would get (the trace options and file) and the version information of gc2's resource
//! script, which the driver reads from its executable.
use crate::{
    runner::{Execution, Invocation},
    source_only,
    source_tree::SourceTree,
    version::Version,
    wasm_build,
};
use anyhow::{Context, Result, anyhow};
use std::{
    collections::HashMap,
    fs,
    path::{Path, PathBuf},
    sync::{
        Arc, Mutex,
        atomic::{AtomicBool, Ordering},
    },
    time::SystemTime,
};

/// A module and the version information its runs get.
struct Loaded {
    compiler: mwcc_wasm::Compiler,
    version_info: Vec<u8>,
    stamp: (SystemTime, u64),
}

static LOADED: Mutex<Option<HashMap<PathBuf, Arc<Loaded>>>> = Mutex::new(None);

fn enabled() -> bool {
    std::env::var("MWCC_IN_PROCESS").map_or(true, |v| v != "0")
}

fn same(a: &Path, b: &Path) -> bool {
    match (fs::canonicalize(a), fs::canonicalize(b)) {
        (Ok(a), Ok(b)) => a == b,
        // (a compiler that is not there, but whose module is)
        _ => std::path::absolute(a).ok() == std::path::absolute(b).ok(),
    }
}

/// The module that runs in EXECUTABLE's place, if it is built.
pub fn module(root: &Path, executable: &Path) -> Option<(Version, PathBuf)> {
    if !enabled() {
        return None;
    }
    for name in wasm_build::VERSIONS {
        let version = Version::parse(name).ok()?;
        let stock = root.join(format!(
            "build/compilers/GC/{}/mwcceppc.exe",
            version.compiler_directory()
        ));
        let oracle = root.join(format!("build/{name}/oracle/mwcceppc.exe"));
        let module = if same(executable, &stock) {
            root.join(wasm_build::output(name))
        } else if same(executable, &oracle) {
            root.join(wasm_build::oracle_output(name))
        } else {
            continue;
        };
        return module.is_file().then_some((version, module));
    }
    None
}

/// Whether MODULE is VERSION's oracle (not its stock compiler).
pub fn is_oracle(version: Version, module: &Path) -> bool {
    module.ends_with(wasm_build::oracle_output(version.name()))
}

/// The VS_VERSIONINFO of VERSION's driver (gc2's `mwcceppc.rc`, with its `VERSION`).
fn version_info(root: &Path, version: Version) -> Result<Vec<u8>> {
    let tree = SourceTree::gc2(version.name());
    let define = tree
        .version_define(root, version.name())?
        .context("gc2 has no version number")?;
    let gc2 = root.join("gc2");
    let resources = source_only::compile_resources(
        &fs::read_to_string(gc2.join(source_only::RESOURCES))?,
        &fs::read_to_string(gc2.join("include/version.h"))?,
        define,
        &fs::read(gc2.join(source_only::RESOURCES.replace(".rc", ".ico")))?,
    )?;
    Ok(resources
        .into_iter()
        .next()
        .context("the resource script has no version information")?
        .data)
}

fn load(root: &Path, version: Version, module: &Path) -> Result<Arc<Loaded>> {
    let metadata = fs::metadata(module)?;
    let stamp = (metadata.modified()?, metadata.len());
    let mut loaded = LOADED.lock().unwrap();
    let loaded = loaded.get_or_insert_with(HashMap::new);
    if let Some(it) = loaded.get(module)
        && it.stamp == stamp
    {
        return Ok(it.clone());
    }
    let compiler = mwcc_wasm::Compiler::new(&fs::read(module)?)
        .map_err(|e| anyhow!("{e:?}"))
        .with_context(|| format!("load {}", module.display()))?;
    let it = Arc::new(Loaded {
        compiler,
        version_info: version_info(root, version)?,
        stamp,
    });
    loaded.insert(module.to_path_buf(), it.clone());
    Ok(it)
}

/// INVOCATION, run in MODULE (VERSION's) as `runner` would run the executable, with the environment its process
/// would get.
pub fn execute(
    invocation: &Invocation<'_>,
    version: Version,
    module: &Path,
    cancel: &dyn Fn() -> bool,
) -> Result<Execution> {
    let loaded = load(invocation.root, version, module)?;
    let environment = crate::runner::process_environment(invocation);
    let mut args = invocation.flags.to_vec();
    args.extend([
        "-c".into(),
        invocation.source.into(),
        "-o".into(),
        invocation.output.display().to_string(),
    ]);
    if invocation.output.exists() {
        fs::remove_file(invocation.output)?;
    }
    let program = std::path::absolute(invocation.executable)?;
    let program = program.display().to_string();
    let stop = Arc::new(AtomicBool::new(false));
    // (the run on a thread of its own, with room for the compiler's recursion; this one polls the cancellation)
    let caller = std::thread::current();
    let outcome = std::thread::scope(|scope| -> Result<mwcc_wasm::Outcome> {
        let run = std::thread::Builder::new()
            .stack_size(64 << 20)
            .spawn_scoped(scope, || {
                let outcome = loaded
                    .compiler
                    .run(&mwcc_wasm::Request {
                        args: &args,
                        directory: invocation.directory,
                        environment: &environment,
                        program: &program,
                        version_info: &loaded.version_info,
                        timeout: Some(invocation.timeout),
                        cancel: Some(stop.clone()),
                    })
                    .map_err(|e| anyhow!("{e:?}"));
                caller.unpark();
                outcome
            })?;
        while !run.is_finished() {
            if cancel() {
                stop.store(true, Ordering::Relaxed);
            }
            std::thread::park_timeout(std::time::Duration::from_millis(10));
        }
        run.join()
            .map_err(|_| anyhow!("the in-process compiler panicked"))?
    })?;
    Ok(Execution {
        exit_code: Some(outcome.exit_code),
        timed_out: outcome.timed_out,
        diagnostics: outcome.console.iter().map(|b| char::from(*b)).collect(),
        cancelled: outcome.cancelled,
    })
}
