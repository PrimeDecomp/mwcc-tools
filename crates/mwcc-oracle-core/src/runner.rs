//! Isolated compiler execution shared by interactive analysis and corpus checks.
use crate::{elf, process_tree, version::Version};
use anyhow::{Context, Result, bail, ensure};
use serde::{Deserialize, Serialize};
use sha2::{Digest, Sha256};
use std::{
    collections::BTreeMap,
    ffi::OsString,
    fs,
    path::{Path, PathBuf},
    process::{Child, Command, ExitStatus, Stdio},
    thread,
    time::{Duration, Instant},
};
use tempfile::TempDir;

const TRACE_ENV: &[&str] = &[
    "MWCC_ORACLE_BACKEND",
    "MWCC_ORACLE_SOURCE_ARENAS",
    "MWCC_ORACLE_SOURCE_NODES",
    "MWCC_ORACLE_SCHEDULE",
    "MWCC_ORACLE_INLINE_COST",
    "MWCC_ORACLE_FULL_COST",
    "MWCC_ORACLE_INSTANTIATION",
    "MWCC_ORACLE_LOCALS",
    "MWCC_ORACLE_BLOCKS",
    "MWCC_ORACLE_CSE",
    "MWCC_ORACLE_STAGES",
    "MWCC_ORACLE_IRO_DUMP",
];

#[derive(Clone, Debug, Default, Serialize, Deserialize)]
#[serde(default, deny_unknown_fields)]
pub struct TraceOptions {
    pub backend: bool,
    pub source_arenas: bool,
    pub source_nodes: bool,
    pub schedule: bool,
    pub inline_cost: bool,
    pub full_cost: bool,
    pub instantiation: bool,
    pub locals: bool,
    pub blocks: bool,
    pub cse: bool,
    pub stages: bool,
}

impl TraceOptions {
    fn environment(&self) -> BTreeMap<String, String> {
        let mut env = BTreeMap::new();
        for (key, enabled, value) in [
            ("MWCC_ORACLE_BACKEND", self.backend, "1"),
            ("MWCC_ORACLE_SOURCE_ARENAS", self.source_arenas, "1"),
            ("MWCC_ORACLE_SOURCE_NODES", self.source_nodes, "1"),
            ("MWCC_ORACLE_SCHEDULE", self.schedule, "1"),
            ("MWCC_ORACLE_INLINE_COST", self.inline_cost, "1"),
            ("MWCC_ORACLE_FULL_COST", self.full_cost, "1"),
            ("MWCC_ORACLE_INSTANTIATION", self.instantiation, "1"),
            ("MWCC_ORACLE_LOCALS", self.locals, "1"),
            ("MWCC_ORACLE_BLOCKS", self.blocks, "2"),
            ("MWCC_ORACLE_CSE", self.cse, "2"),
            ("MWCC_ORACLE_STAGES", self.stages, "1"),
        ] {
            if enabled {
                env.insert(key.into(), value.into());
            }
        }
        env
    }
}

#[derive(Clone, Debug, Serialize, Deserialize, PartialEq, Eq)]
#[serde(tag = "status", rename_all = "snake_case")]
pub enum CheckStatus {
    Verified,
    Mismatch,
    Skipped,
    OriginalUnavailable,
    OriginalFailed {
        exit_code: Option<i32>,
        timed_out: bool,
        diagnostics: String,
    },
}

impl CheckStatus {
    pub fn identical(&self) -> Option<bool> {
        match self {
            Self::Verified => Some(true),
            Self::Mismatch | Self::OriginalFailed { .. } => Some(false),
            _ => None,
        }
    }
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum BlockLimit {
    Count(u8),
    Unlimited,
}

impl BlockLimit {
    pub fn parse(text: &str) -> Result<Self> {
        if text == "none" {
            return Ok(Self::Unlimited);
        }
        let value = if let Some(hex) = text.strip_prefix("0x") {
            u8::from_str_radix(hex, 16)?
        } else {
            text.parse()?
        };
        ensure!(value <= 127, "a block limit is 0 to 127, or none");
        Ok(Self::Count(value))
    }
}

#[derive(Clone, Debug)]
pub struct CompileRequest {
    pub root: PathBuf,
    pub working_directory: PathBuf,
    pub source: String,
    pub flags: Vec<String>,
    pub version: Version,
    pub options: TraceOptions,
    pub overrides: Vec<String>,
    pub check: bool,
    pub original_fallback: bool,
    pub cut_limit: Option<BlockLimit>,
    pub merge_limit: Option<BlockLimit>,
    pub keep_object: Option<PathBuf>,
    pub timeout: Duration,
}

#[derive(Clone, Debug, Serialize, Deserialize)]
pub struct Compilation {
    pub version: Version,
    pub source: String,
    pub oracle: String,
    pub producer_sha256: String,
    pub original_sha256: Option<String>,
    pub source_sha256: String,
    pub flags: Vec<String>,
    pub trace_options: TraceOptions,
    pub object_sha256: String,
    pub check_status: CheckStatus,
    pub identical: Option<bool>,
    pub differences: Vec<String>,
    pub diagnostics: String,
    pub trace: String,
    pub trace_present: bool,
    pub original_fallback: bool,
    #[serde(skip)]
    pub object: Vec<u8>,
}

#[derive(Debug)]
pub struct Execution {
    pub exit_code: Option<i32>,
    pub timed_out: bool,
    pub diagnostics: String,
    pub cancelled: bool,
}
#[derive(Debug)]
pub struct Cancelled;
impl std::fmt::Display for Cancelled {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.write_str("compiler operation cancelled")
    }
}
impl std::error::Error for Cancelled {}

pub fn launcher(root: &Path, executable: &Path) -> Result<Command> {
    launcher_with_override(root, executable, std::env::var_os("MWCC_WIBO"))
}
fn launcher_with_override(
    root: &Path,
    executable: &Path,
    selected: Option<OsString>,
) -> Result<Command> {
    if cfg!(windows) {
        return Ok(Command::new(executable));
    }
    let local = root.join("build/tools/wibo");
    let mut command = Command::new(if let Some(selected) = selected {
        selected
    } else if local.is_file() {
        local.into_os_string()
    } else {
        OsString::from("wibo")
    });
    command.arg(executable);
    Ok(command)
}

/// Redirect both streams to the same file: no pipe deadlocks and diagnostic
/// ordering is the compiler's ordering, as in the reference merged stream.
pub struct Invocation<'a> {
    pub root: &'a Path,
    pub executable: &'a Path,
    pub flags: &'a [String],
    pub source: &'a str,
    pub directory: &'a Path,
    pub output: &'a Path,
    pub trace_file: &'a Path,
    pub options: &'a TraceOptions,
    pub timeout: Duration,
}

pub fn execute(invocation: Invocation<'_>) -> Result<Execution> {
    execute_controlled(invocation, &|| false)
}
/// Run the compiler: in this process where its module is built (crate::in_process), else as a process.
pub fn execute_controlled(
    invocation: Invocation<'_>,
    cancel: &dyn Fn() -> bool,
) -> Result<Execution> {
    #[cfg(feature = "wasm")]
    if let Some((version, module)) =
        crate::in_process::module(invocation.root, invocation.executable)
    {
        // (the source arena and node streams come from the guarded probes, which only the x86 oracle has)
        let probes = invocation.options.source_arenas || invocation.options.source_nodes;
        if probes && crate::in_process::is_oracle(version, &module) {
            ensure!(
                invocation.executable.is_file(),
                "{} is not built: the source arena and node streams need its guarded probes, which the \
                 WebAssembly oracle does not have",
                invocation.executable.display()
            );
            return execute_process_controlled(invocation, cancel);
        }
        if cancel() {
            return Err(Cancelled.into());
        }
        ensure!(
            !invocation.timeout.is_zero(),
            "compiler timeout must be positive"
        );
        return crate::in_process::execute(&invocation, version, &module, cancel);
    }
    execute_process_controlled(invocation, cancel)
}
/// The environment the compiler's process gets: ours, the trace options and file in place of ours.
#[cfg(feature = "wasm")]
pub(crate) fn process_environment(invocation: &Invocation<'_>) -> Vec<(String, String)> {
    let mut environment: BTreeMap<String, String> = std::env::vars()
        .filter(|(key, _)| !TRACE_ENV.contains(&key.as_str()))
        .collect();
    environment.insert(
        "MWCC_ORACLE_TRACE_FILE".into(),
        invocation.trace_file.display().to_string(),
    );
    environment.extend(invocation.options.environment());
    environment.into_iter().collect()
}
/// Run the executable itself, as a process (the reference a module is compared with).
/// The module that runs in EXECUTABLE's place (crate::in_process), if any.
fn module(root: &Path, executable: &Path) -> Option<PathBuf> {
    #[cfg(feature = "wasm")]
    return crate::in_process::module(root, executable).map(|(_, module)| module);
    #[cfg(not(feature = "wasm"))]
    return {
        let _ = (root, executable);
        None
    };
}

/// Whether EXECUTABLE can run: it is there, or a module runs in its place.
pub fn runs(root: &Path, executable: &Path) -> bool {
    executable.is_file() || module(root, executable).is_some()
}

pub fn execute_process(invocation: Invocation<'_>) -> Result<Execution> {
    execute_process_controlled(invocation, &|| false)
}
fn execute_process_controlled(
    invocation: Invocation<'_>,
    cancel: &dyn Fn() -> bool,
) -> Result<Execution> {
    if cancel() {
        return Err(Cancelled.into());
    }
    let Invocation {
        root,
        executable,
        flags,
        source,
        directory,
        output,
        trace_file,
        options,
        timeout,
    } = invocation;
    ensure!(!timeout.is_zero(), "compiler timeout must be positive");
    let log = crate::temporary::diagnostic_log()?;
    let mut command = launcher(root, executable)?;
    command
        .args(flags)
        .arg("-c")
        .arg(source)
        .arg("-o")
        .arg(output)
        .current_dir(directory)
        .stdout(Stdio::from(log.as_file().try_clone()?))
        .stderr(Stdio::from(log.as_file().try_clone()?))
        .env("MWCC_ORACLE_TRACE_FILE", trace_file);
    for key in TRACE_ENV {
        command.env_remove(key);
    }
    command.envs(options.environment());
    if output.exists() {
        fs::remove_file(output)?;
    }
    process_tree::prepare(&mut command);
    let mut child = command
        .spawn()
        .with_context(|| format!("launch {}", executable.display()))?;
    let (status, timed_out, cancelled) = wait_controlled(&mut child, timeout, cancel)?;
    let bytes = fs::read(log.path())?;
    let diagnostics = bytes.iter().map(|b| char::from(*b)).collect();
    Ok(Execution {
        exit_code: status.code(),
        timed_out,
        diagnostics,
        cancelled,
    })
}

fn wait_controlled(
    child: &mut Child,
    timeout: Duration,
    cancel: &dyn Fn() -> bool,
) -> Result<(ExitStatus, bool, bool)> {
    let began = Instant::now();
    loop {
        if let Some(status) = child.try_wait()? {
            return Ok((status, false, false));
        }
        let cancelled = cancel();
        if cancelled || began.elapsed() >= timeout {
            process_tree::terminate(child)?;
            return Ok((child.wait()?, !cancelled, cancelled));
        }
        thread::sleep(Duration::from_millis(10));
    }
}

fn absolute(base: &Path, path: &Path) -> Result<PathBuf> {
    let full = if path.is_absolute() {
        path.to_owned()
    } else {
        base.join(path)
    };
    fs::canonicalize(&full).with_context(|| format!("resolve {}", full.display()))
}

pub(crate) fn apply_overrides(
    request: &CompileRequest,
    scratch: &Path,
    flags: &mut Vec<String>,
    source: &mut String,
) -> Result<()> {
    let cwd = std::env::current_dir()?;
    for (index, item) in request.overrides.iter().enumerate() {
        let (path, replacement) = item
            .split_once('=')
            .context("--override needs PATH=REPLACEMENT")?;
        let replacement = absolute(&cwd, Path::new(replacement))?;
        ensure!(replacement.is_file(), "override replacement is not a file");
        let target = absolute(&request.working_directory, Path::new(path))?;
        if target == absolute(&request.working_directory, Path::new(&request.source))? {
            flags.splice(
                0..0,
                [
                    "-i".into(),
                    target
                        .parent()
                        .context("source has no parent")?
                        .display()
                        .to_string(),
                ],
            );
            *source = replacement.display().to_string();
            continue;
        }
        let mut found = false;
        for n in 0..flags.len().saturating_sub(1) {
            if !matches!(flags[n].as_str(), "-i" | "-I") {
                continue;
            }
            let root = absolute(&request.working_directory, Path::new(&flags[n + 1]))?;
            if let Ok(relative) = target.strip_prefix(&root) {
                let shadow = scratch.join(format!("override{index}"));
                let copy = shadow.join(relative);
                fs::create_dir_all(copy.parent().context("override has no parent")?)?;
                fs::copy(&replacement, &copy)?;
                let include_option = flags[n].clone();
                flags.splice(n..n, [include_option, shadow.display().to_string()]);
                found = true;
                break;
            }
        }
        ensure!(
            found,
            "--override: {path} is not under an include directory (-i/-I)"
        );
    }
    Ok(())
}

pub fn patch_limits(
    data: &mut [u8],
    version: Version,
    cut: Option<BlockLimit>,
    merge: Option<BlockLimit>,
) -> Result<()> {
    let (cuts, merge_site): (&[usize], usize) = match version {
        Version::Gc13 => (
            &[
                0x32d15, 0x32db1, 0x32e11, 0x32e71, 0x32f54, 0x32fe1, 0x33055,
            ],
            0xfe91d,
        ),
        Version::Gc132 => (
            &[
                0x32a95, 0x32b31, 0x32b91, 0x32bf1, 0x32cd4, 0x32d61, 0x32dd5,
            ],
            0xff3fd,
        ),
        Version::Gc26 => (
            &[
                0x32b75, 0x32c11, 0x32c71, 0x32cd1, 0x32db4, 0x32e41, 0x32eb5,
            ],
            0x10025d,
        ),
        Version::Gc27 => (
            &[
                0x32b75, 0x32c11, 0x32c71, 0x32cd1, 0x32db4, 0x32e41, 0x32eb5,
            ],
            0x10017d,
        ),
        _ => bail!("block-limit patches are unavailable for {}", version.name()),
    };
    // Validate every site before mutating any of them.
    for offset in cuts {
        ensure!(
            data.get(*offset..offset + 2) == Some(&[0x66, 0x83])
                && data.get(offset + 3..offset + 6) == Some(&[0x28, 0x64, 0x7e]),
            "unexpected cut compare at {offset:#x}"
        );
    }
    ensure!(
        data.get(merge_site..merge_site + 5) == Some(&[0x83, 0xf9, 0x64, 0x0f, 0x8e]),
        "unexpected merge compare at {merge_site:#x}"
    );
    for offset in cuts {
        match cut {
            Some(BlockLimit::Count(n)) => data[offset + 4] = n,
            Some(BlockLimit::Unlimited) => data[offset + 5] = 0xeb,
            None => (),
        }
    }
    match merge {
        Some(BlockLimit::Count(n)) => data[merge_site + 2] = n,
        Some(BlockLimit::Unlimited) => {
            data[merge_site + 3..merge_site + 5].copy_from_slice(&[0x90, 0xe9])
        }
        None => (),
    }
    Ok(())
}

fn patched_copy(path: &Path, destination: &Path, request: &CompileRequest) -> Result<PathBuf> {
    fs::create_dir_all(destination)?;
    let mut bytes = fs::read(path)?;
    patch_limits(
        &mut bytes,
        request.version,
        request.cut_limit,
        request.merge_limit,
    )?;
    let output = destination.join(path.file_name().context("executable has no filename")?);
    fs::write(&output, bytes)?;
    let stub = path.with_file_name("lmgr326b.dll");
    if stub.is_file() {
        fs::copy(stub, destination.join("lmgr326b.dll"))?;
    }
    Ok(output)
}

pub fn compile(request: &CompileRequest) -> Result<Compilation> {
    compile_controlled(request, &|| false)
}
pub fn compile_controlled(
    request: &CompileRequest,
    cancel: &dyn Fn() -> bool,
) -> Result<Compilation> {
    let root = fs::canonicalize(&request.root).context("oracle root does not exist")?;
    let relative_oracle = format!("build/{}/oracle/mwcceppc.exe", request.version.name());
    let mut oracle = root.join(&relative_oracle);
    let mut original = root.join(format!(
        "build/compilers/GC/{}/mwcceppc.exe",
        request.version.compiler_directory()
    ));
    let in_process = |path: &Path| module(&root, path);
    let fallback = !runs(&root, &oracle) && request.original_fallback && original.is_file();
    if fallback {
        oracle = original.clone();
    }
    ensure!(
        runs(&root, &oracle),
        "{} is missing; build the oracle for {}",
        oracle.display(),
        request.version.name()
    );
    let oracle_label = oracle
        .strip_prefix(&root)?
        .to_string_lossy()
        .replace('\\', "/");
    let scratch = TempDir::new()?;
    let mut flags: Vec<_> = request
        .flags
        .iter()
        .filter(|f| !matches!(f.as_str(), "-MD" | "-MMD" | "-c" | "--"))
        .cloned()
        .collect();
    let mut source = request.source.clone();
    apply_overrides(request, scratch.path(), &mut flags, &mut source)?;
    if request.cut_limit.is_some() || request.merge_limit.is_some() {
        oracle = patched_copy(&oracle, &scratch.path().join("oracle"), request)?;
        if original.is_file() {
            original = patched_copy(&original, &scratch.path().join("original"), request)?;
        }
    }
    // (of what runs: the module, where it runs in process)
    let producer_sha256 = format!(
        "{:x}",
        Sha256::digest(fs::read(in_process(&oracle).unwrap_or(oracle.clone()))?)
    );
    let original_runs = in_process(&original).unwrap_or(original.clone());
    let original_sha256 = if original_runs.is_file() {
        Some(format!("{:x}", Sha256::digest(fs::read(&original_runs)?)))
    } else {
        None
    };
    let source_sha256 = format!(
        "{:x}",
        Sha256::digest(
            fs::read(request.working_directory.join(&source))
                .context("read compiler source for provenance")?
        )
    );
    let trace_path = scratch.path().join("trace.txt");
    let obj_path = scratch.path().join("oracle.o");
    let run = execute_controlled(
        Invocation {
            root: &root,
            executable: &oracle,
            flags: &flags,
            source: &source,
            directory: &request.working_directory,
            output: &obj_path,
            trace_file: &trace_path,
            options: &request.options,
            timeout: request.timeout,
        },
        cancel,
    )?;
    if run.cancelled {
        return Err(Cancelled.into());
    }
    if run.timed_out {
        bail!(
            "oracle compiler timed out after {} seconds\n{}",
            request.timeout.as_secs_f64(),
            run.diagnostics
        );
    }
    ensure!(
        run.exit_code == Some(0),
        "oracle compiler failed ({:?})\n{}",
        run.exit_code,
        run.diagnostics
    );
    let object = fs::read(&obj_path).context("oracle compiler succeeded without an object")?;
    let trace_present = trace_path.is_file();
    let trace = if trace_present {
        fs::read_to_string(&trace_path).context("trace is not UTF-8")?
    } else {
        String::new()
    };
    if !fallback && trace.trim().is_empty() {
        let has_code = elf::Elf::parse(&object)?
            .symbols()?
            .iter()
            .any(|s| s.kind == 2 && s.size > 0);
        ensure!(
            !has_code,
            "oracle emitted function code without a trace; rebuild the oracle and check its trace-file access"
        );
    }
    let mut differences = vec![];
    let check_status = if !request.check {
        CheckStatus::Skipped
    } else if !original_runs.is_file() {
        CheckStatus::OriginalUnavailable
    } else {
        let other = scratch.path().join("original.o");
        let run = execute_controlled(
            Invocation {
                root: &root,
                executable: &original,
                flags: &flags,
                source: &source,
                directory: &request.working_directory,
                output: &other,
                trace_file: &scratch.path().join("original.trace"),
                options: &TraceOptions::default(),
                timeout: request.timeout,
            },
            cancel,
        )?;
        if run.cancelled {
            return Err(Cancelled.into());
        }
        if run.timed_out || run.exit_code != Some(0) || !other.is_file() {
            CheckStatus::OriginalFailed {
                exit_code: run.exit_code,
                timed_out: run.timed_out,
                diagnostics: run.diagnostics,
            }
        } else {
            let other = fs::read(other)?;
            if object == other {
                CheckStatus::Verified
            } else {
                differences = elf::differences(&object, &other)?;
                CheckStatus::Mismatch
            }
        }
    };
    if let Some(path) = &request.keep_object {
        fs::write(path, &object).with_context(|| format!("write {}", path.display()))?;
    }
    Ok(Compilation {
        version: request.version,
        source: request.source.clone(),
        oracle: oracle_label,
        producer_sha256,
        original_sha256,
        source_sha256,
        flags,
        trace_options: request.options.clone(),
        object_sha256: format!("{:x}", Sha256::digest(&object)),
        identical: check_status.identical(),
        check_status,
        differences,
        diagnostics: run.diagnostics,
        trace,
        trace_present,
        original_fallback: fallback,
        object,
    })
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn compiler_timeout_or_cancellation_stops_descendants_before_reporting_completion() {
        for cancellation in [false, true] {
            let scratch = tempfile::tempdir().unwrap();
            let marker = scratch.path().join("late-write");
            let ready = scratch.path().join("ready");
            #[cfg(unix)]
            let mut command = {
                let mut command = Command::new("/bin/sh");
                command
                    .args([
                        "-c",
                        "( sleep 2; printf leaked > \"$1\" ) & printf ready > \"$2\"; wait",
                        "compiler-timeout-test",
                    ])
                    .arg(&marker)
                    .arg(&ready);
                command
            };
            #[cfg(windows)]
            let mut command = {
                let powershell = PathBuf::from(std::env::var_os("SystemRoot").unwrap())
                    .join("System32/WindowsPowerShell/v1.0/powershell.exe");
                let quote = |path: &Path| path.to_string_lossy().replace('\'', "''");
                let child = scratch.path().join("child.ps1");
                let parent = scratch.path().join("parent.ps1");
                fs::write(
                    &child,
                    format!(
                        "Start-Sleep -Seconds 2\nSet-Content -LiteralPath '{}' -Value leaked\n",
                        quote(&marker)
                    ),
                )
                .unwrap();
                fs::write(&parent, format!("Start-Process -FilePath '{}' -WindowStyle Hidden -ArgumentList '-NoProfile -NonInteractive -File \"{}\"' | Out-Null\nSet-Content -LiteralPath '{}' -Value ready\nStart-Sleep -Seconds 20\n", quote(&powershell), quote(&child), quote(&ready))).unwrap();
                let mut command = Command::new(powershell);
                command
                    .args([
                        "-NoLogo",
                        "-NoProfile",
                        "-NonInteractive",
                        "-ExecutionPolicy",
                        "Bypass",
                        "-File",
                    ])
                    .arg(parent);
                command
            };
            process_tree::prepare(&mut command);
            let mut child = command
                .stdout(Stdio::null())
                .stderr(Stdio::null())
                .spawn()
                .unwrap();
            let began = Instant::now();
            while !ready.exists() && began.elapsed() < Duration::from_secs(10) {
                thread::sleep(Duration::from_millis(10));
            }
            let was_ready = ready.exists();
            let (status, timed_out, cancelled) =
                wait_controlled(&mut child, Duration::from_millis(20), &|| cancellation).unwrap();
            assert!(was_ready, "compiler did not start its descendant");
            assert_eq!(timed_out, !cancellation);
            assert_eq!(cancelled, cancellation);
            assert!(!status.success());
            assert!(child.try_wait().unwrap().is_some());
            thread::sleep(Duration::from_secs(3));
            assert!(
                !marker.exists(),
                "compiler descendant wrote after timeout was reported"
            );
        }
    }
    #[test]
    fn invalid_limit_patch_is_atomic() {
        let mut data = vec![0; 0x110000];
        let before = data.clone();
        assert!(patch_limits(&mut data, Version::Gc132, Some(BlockLimit::Count(5)), None).is_err());
        assert_eq!(data, before);
    }
    #[test]
    fn check_status_never_promotes_unknown_to_success() {
        assert_eq!(CheckStatus::Skipped.identical(), None);
        assert_eq!(CheckStatus::OriginalUnavailable.identical(), None);
        assert_eq!(CheckStatus::Mismatch.identical(), Some(false));
        assert_eq!(CheckStatus::Verified.identical(), Some(true));
        assert!(BlockLimit::parse("128").is_err());
        assert_eq!(BlockLimit::parse("0x7f").unwrap(), BlockLimit::Count(127));
    }
    #[test]
    fn launcher_selection_is_explicit_and_preserves_argument_boundaries() {
        let root = tempfile::tempdir().unwrap();
        let executable = root.path().join("compiler with spaces.exe");
        let override_path = root.path().join("chosen runner");
        let local = root.path().join("build/tools/wibo");
        fs::create_dir_all(local.parent().unwrap()).unwrap();
        fs::write(&local, b"local runner").unwrap();
        let selected = launcher_with_override(
            root.path(),
            &executable,
            Some(override_path.clone().into_os_string()),
        )
        .unwrap();
        let fallback = launcher_with_override(root.path(), &executable, None).unwrap();
        if cfg!(windows) {
            assert_eq!(selected.get_program(), executable.as_os_str());
            assert!(selected.get_args().next().is_none());
        } else {
            assert_eq!(selected.get_program(), override_path.as_os_str());
            assert_eq!(fallback.get_program(), local.as_os_str());
            assert_eq!(
                selected.get_args().collect::<Vec<_>>(),
                [executable.as_os_str()]
            );
        }
    }
}
