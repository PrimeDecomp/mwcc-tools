//! The compilers run in this process: for the versions gc2's sources build (crate::wasm_build), a run of the stock
//! compiler (`build/compilers/GC/<dir>/mwcceppc.exe`) or of the oracle (`build/<version>/oracle/mwcceppc.exe`) runs
//! the compiler module (`build/<version>/wasm/mwcc.wasm`) instead, when it is built: no process, no Windows
//! executable. A run of the oracle traces (mwcc-wasm's `Run::trace`); a run of the stock compiler does not, and
//! its hooks only pass through. `runner` dispatches here; `MWCC_IN_PROCESS=0` turns it off.
//!
//! A module is loaded once per process (and again when its file changes, mwcc-wasm's `load`), from the compiled
//! artifact its build wrote beside it (`mwcc.cache/`), with the version information of gc2's resource script, which
//! the driver reads from its executable (`mwcc.version`). Each run instantiates it afresh, with the environment a
//! process would get (the trace options). The trace stays in memory; it is written to
//! `MWCC_ORACLE_TRACE_FILE` once the run ends (or, without one, after the run's standard error, as the oracle writes
//! it there).
use crate::{
    runner::{Execution, Invocation},
    version::Version,
};
use anyhow::{Context, Result, anyhow};
use std::{fs, path::Path, sync::Arc};

// (which compiler a module runs in place of: mwcc-project)
pub use mwcc_project::compilers::{linker_module_of_copy, module, module_of_copy, traces};
pub use mwcc_wasm::{SharedFiles, Trapped};

fn load(module: &Path) -> Result<Arc<mwcc_wasm::Loaded>> {
    mwcc_wasm::load(module).map_err(|e| anyhow!("{e:?}"))
}

/// INVOCATION, run in MODULE (VERSION's) as `runner` would run the executable, with the environment its process
/// would get.
pub fn execute(
    invocation: &Invocation<'_>,
    version: Version,
    module: &Path,
    cancel: &dyn Fn() -> bool,
) -> Result<Execution> {
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
    let outcome = run_kept(
        &Run {
            root: invocation.root,
            version,
            module,
            program: invocation.executable,
            args: &args,
            directory: invocation.directory,
            environment: &environment,
            timeout: Some(invocation.timeout),
            trace: traces(invocation.root, invocation.executable),
            overlay: None,
            transcode_sjis: false,
            command_line: None,
            files: None,
        },
        cancel,
    )?;
    Ok(Execution {
        exit_code: Some(outcome.exit_code),
        timed_out: outcome.timed_out,
        diagnostics: outcome.console.iter().map(|b| char::from(*b)).collect(),
        cancelled: outcome.cancelled,
        trace: outcome.trace,
    })
}

/// Load MODULE (VERSION's) now, so the runs that follow find it loaded.
pub fn preload(module: &Path) -> Result<()> {
    load(module).map(|_| ())
}

/// A run of a module in place of its executable's process.
pub struct Run<'a> {
    pub root: &'a Path,
    pub version: Version,
    pub module: &'a Path,
    /// The executable it stands for (the process's argv[0]).
    pub program: &'a Path,
    /// The arguments, as the process gets them (without argv[0]).
    pub args: &'a [String],
    pub directory: &'a Path,
    pub environment: &'a [(String, String)],
    pub timeout: Option<std::time::Duration>,
    /// Whether it traces: its trace goes where the oracle's process would write it ([`deliver_trace`]).
    pub trace: bool,
    /// The files it reads in place of the disk's, and whether it reads sources as sjiswrap gives them
    /// ([`mwcc_wasm::Run`]).
    pub overlay: Option<&'a mwcc_wasm::Overlay>,
    pub transcode_sjis: bool,
    /// The command line as the process gets it, verbatim ([`mwcc_wasm::Run::command_line`]).
    pub command_line: Option<&'a str>,
    /// The files it shares with the build's other runs ([`mwcc_wasm::SharedFiles`]).
    pub files: Option<&'a std::sync::Arc<mwcc_wasm::SharedFiles>>,
}

/// Where a traced run's TRACE goes: appended to `MWCC_ORACLE_TRACE_FILE` of its environment (relative to its
/// directory), or else after its standard error (and console), as the oracle's process writes it.
fn deliver_trace(run: &Run<'_>, outcome: &mut mwcc_wasm::Outcome) -> Result<()> {
    let trace = std::mem::take(&mut outcome.trace);
    if !write_trace_file(run, &trace)? {
        outcome.stderr.extend_from_slice(&trace);
        outcome.console.extend_from_slice(&trace);
    }
    Ok(())
}

/// TRACE appended to RUN's `MWCC_ORACLE_TRACE_FILE` (relative to its directory): whether it names one.
fn write_trace_file(run: &Run<'_>, trace: &[u8]) -> Result<bool> {
    let Some(path) = run
        .environment
        .iter()
        .rev()
        .find(|(key, _)| key == "MWCC_ORACLE_TRACE_FILE")
        .map(|(_, value)| run.directory.join(value))
    else {
        return Ok(false);
    };
    use std::io::Write as _;
    fs::OpenOptions::new()
        .create(true)
        .append(true)
        .open(&path)
        .and_then(|mut file| file.write_all(trace))
        .with_context(|| format!("write the trace to {}", path.display()))?;
    Ok(true)
}

/// RUN's module run as its process would be, on a thread of its own (with room for the compiler's recursion) while
/// this one polls CANCEL; a traced run's trace goes where the process would write it.
pub fn run(run: &Run<'_>, cancel: &dyn Fn() -> bool) -> Result<mwcc_wasm::Outcome> {
    let mut outcome = match run_kept(run, cancel) {
        Ok(outcome) => outcome,
        Err(error) => {
            // (a trap: what was traced before it)
            if run.trace
                && let Some(trapped) = error.downcast_ref::<mwcc_wasm::Trapped>()
            {
                write_trace_file(run, &trapped.trace)?;
            }
            return Err(error);
        }
    };
    if run.trace {
        deliver_trace(run, &mut outcome)?;
    }
    Ok(outcome)
}

/// [`run`], with the trace kept in the outcome.
fn run_kept(run: &Run<'_>, cancel: &dyn Fn() -> bool) -> Result<mwcc_wasm::Outcome> {
    let loaded = load(run.module)?;
    let program = std::path::absolute(run.program)?;
    let program = program.display().to_string();
    loaded
        .run(
            &mwcc_wasm::Run {
                args: run.args,
                directory: run.directory,
                environment: run.environment,
                program: &program,
                version_info: &[],
                timeout: run.timeout,
                cancel: None,
                trace: run.trace,
                overlay: run.overlay,
                transcode_sjis: run.transcode_sjis,
                record_reads: false,
                command_line: run.command_line,
                shared_files: run.files,
            },
            cancel,
        )
        .map_err(|e| match e.downcast_ref::<mwcc_wasm::Trapped>() {
            // (kept, for the trace before the trap)
            Some(trapped) => anyhow::Error::new(trapped.clone()),
            None => anyhow!("{e:?}"),
        })
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::wasm_build;
    use sha1::{Digest, Sha1};

    #[test]
    fn a_copy_is_known_by_the_size_and_sha1_of_its_versions_config() {
        let root = tempfile::tempdir().unwrap();
        let project = tempfile::tempdir().unwrap();
        let compiler = project.path().join("mwcceppc.exe");
        fs::write(&compiler, b"a compiler").unwrap();
        let linker = project.path().join("mwldeppc.exe");
        fs::write(&linker, b"a linker").unwrap();
        let config = root.path().join("config/GC_2_7/config.json");
        fs::create_dir_all(config.parent().unwrap()).unwrap();
        fs::write(
            &config,
            serde_json::json!({
                "original": "build/compilers/GC/2.7/mwcceppc.exe",
                "sha1": format!("{:x}", Sha1::digest(b"a compiler")),
                "size": 10,
            })
            .to_string(),
        )
        .unwrap();
        // (no stock compiler in the root, and no module yet)
        assert!(!root.path().join("build/compilers").exists());
        assert_eq!(module_of_copy(root.path(), &compiler), None);
        let module = root.path().join(wasm_build::module_output("GC_2_7"));
        fs::create_dir_all(module.parent().unwrap()).unwrap();
        fs::write(&module, b"").unwrap();
        assert_eq!(
            module_of_copy(root.path(), &compiler),
            Some((Version::parse("GC_2_7").unwrap(), module))
        );
        assert_eq!(module_of_copy(root.path(), &linker), None);
        // (the same size, other bytes)
        fs::write(&compiler, b"b compiler").unwrap();
        assert_eq!(module_of_copy(root.path(), &compiler), None);
    }

    /// A header and an include directory named in another case than the file system's are found, as the original
    /// finds them on Windows and under wibo. (The module is copied, so that its cache stays this test's.)
    #[test]
    fn a_header_named_in_another_case_is_found() {
        let root = Path::new(env!("CARGO_MANIFEST_DIR")).join("../..");
        let built = root.join(wasm_build::module_output("GC_2_7"));
        if !built.is_file() {
            eprintln!("(skipped: {} is not built)", built.display());
            return;
        }
        let modules = tempfile::tempdir().unwrap();
        let module = modules.path().join("mwcc.wasm");
        fs::copy(&built, &module).unwrap();
        fs::copy(
            mwcc_wasm::version_info_path(&built),
            mwcc_wasm::version_info_path(&module),
        )
        .unwrap();
        let project = tempfile::tempdir().unwrap();
        fs::create_dir_all(project.path().join("Include/Kyoto")).unwrap();
        fs::write(
            project.path().join("Include/Kyoto/CCrc32.hpp"),
            "extern int crc;\n",
        )
        .unwrap();
        fs::write(
            project.path().join("a.c"),
            "#include \"Kyoto/CCRC32.hpp\"\nint f(void) { return crc; }\n",
        )
        .unwrap();
        let args: Vec<String> = [
            "-nodefaults",
            "-proc",
            "gekko",
            "-O4,p",
            "-i",
            "include",
            "-MMD",
            "-c",
            "a.c",
            "-o",
            "a.o",
        ]
        .map(String::from)
        .into();
        let outcome = run(
            &Run {
                root: &root,
                version: Version::parse("GC_2_7").unwrap(),
                module: &module,
                program: &root.join("build/compilers/GC/2.7/mwcceppc.exe"),
                args: &args,
                directory: project.path(),
                environment: &[],
                timeout: Some(std::time::Duration::from_secs(120)),
                trace: false,
                overlay: None,
                transcode_sjis: false,
                command_line: None,
                files: None,
            },
            &|| false,
        )
        .unwrap();
        let console = String::from_utf8_lossy(&outcome.console);
        assert_eq!(outcome.exit_code, 0, "{console}");
        assert!(project.path().join("a.o").is_file(), "{console}");
        let dependencies = fs::read_to_string(project.path().join("a.d")).unwrap();
        assert!(
            dependencies.to_ascii_lowercase().contains("ccrc32.hpp"),
            "{dependencies}"
        );
    }

    #[test]
    fn the_configs_give_the_originals_sizes() {
        let root = Path::new(env!("CARGO_MANIFEST_DIR")).join("../..");
        for name in wasm_build::VERSIONS {
            let config: serde_json::Value = serde_json::from_slice(
                &fs::read(root.join(format!("config/{name}/config.json"))).unwrap(),
            )
            .unwrap();
            assert!(config["size"].as_u64().is_some(), "{name}");
            let original = root.join(config["original"].as_str().unwrap());
            if let Ok(data) = fs::read(original) {
                assert_eq!(config["size"].as_u64(), Some(data.len() as u64), "{name}");
                assert_eq!(
                    config["sha1"],
                    format!("{:x}", Sha1::digest(&data)),
                    "{name}"
                );
            }
        }
    }
}
