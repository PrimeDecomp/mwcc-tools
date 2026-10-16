//! Exact-input cache contract: scan dependencies and preprocess every candidate
//! before reuse, hash raw dependencies, compiler/runtime files and environment.
//! Re-scan after compilation to reject input changes while the compiler ran.
use super::state::digest;
use crate::{
    process_tree,
    runner::{self, CompileRequest, Invocation, TraceOptions},
};
use anyhow::{Context, Result, ensure};
use serde::{Deserialize, Serialize};
use std::{
    collections::BTreeSet,
    ffi::OsStr,
    fs,
    path::{Path, PathBuf},
    process::{Command, Stdio},
    thread,
    time::{Duration, Instant},
};

#[derive(Clone, Debug, PartialEq, Eq, Serialize, Deserialize)]
pub struct FileInput {
    pub path: PathBuf,
    pub sha256: String,
}
#[derive(Clone, Debug, PartialEq, Eq, Serialize, Deserialize)]
pub struct Snapshot {
    pub format_version: u32,
    pub source: PathBuf,
    pub source_sha256: String,
    pub working_directory: PathBuf,
    pub flags: Vec<String>,
    pub trace_options: serde_json::Value,
    pub runtime: Vec<FileInput>,
    pub environment_sha256: String,
    pub dependencies: Vec<FileInput>,
    pub preprocessed_sha256: String,
    pub stock_preprocessed_sha256: String,
    pub reusable: bool,
    pub cache_limitations: Vec<String>,
}
fn os_bytes(value: &OsStr) -> Vec<u8> {
    #[cfg(windows)]
    {
        use std::os::windows::ffi::OsStrExt;
        value.encode_wide().flat_map(u16::to_le_bytes).collect()
    }
    #[cfg(unix)]
    {
        use std::os::unix::ffi::OsStrExt;
        value.as_bytes().to_vec()
    }
    #[cfg(not(any(windows, unix)))]
    {
        value.to_string_lossy().as_bytes().to_vec()
    }
}
pub fn environment_digest() -> String {
    let mut values = std::env::vars_os()
        .map(|(k, v)| (os_bytes(&k), os_bytes(&v)))
        .collect::<Vec<_>>();
    values.sort();
    digest(&serde_json::to_vec(&values).unwrap())
}
/// Analysis identity is checked once per run; it is not a compiler input and
/// does not need to be repeatedly copied into every compilation artifact.
pub fn analyzer_sha256() -> Result<String> {
    Ok(digest(&fs::read(std::env::current_exe()?)?))
}
fn resolve_program(program: &OsStr) -> Result<PathBuf> {
    let path = Path::new(program);
    if path.is_file() {
        return Ok(path.canonicalize()?);
    }
    for directory in std::env::split_paths(&std::env::var_os("PATH").unwrap_or_default()) {
        let file = directory.join(path);
        if file.is_file() {
            return Ok(file.canonicalize()?);
        }
        if cfg!(windows) {
            for extension in ["exe", "cmd", "bat"] {
                let file = file.with_extension(extension);
                if file.is_file() {
                    return Ok(file.canonicalize()?);
                }
            }
        }
    }
    anyhow::bail!(
        "cannot resolve runtime program {}",
        program.to_string_lossy()
    )
}
fn file(path: &Path) -> Result<FileInput> {
    Ok(FileInput {
        path: path.canonicalize()?,
        sha256: digest(&fs::read(path)?),
    })
}
pub fn runtime(request: &CompileRequest) -> Result<Vec<FileInput>> {
    let root = request.root.canonicalize()?;
    let paths = [
        root.join(format!(
            "build/{}/oracle/mwcceppc.exe",
            request.version.name()
        )),
        root.join(format!(
            "build/compilers/GC/{}/mwcceppc.exe",
            request.version.compiler_directory()
        )),
    ];
    let mut files = BTreeSet::new();
    for path in &paths {
        ensure!(path.is_file(), "compiler is missing: {}", path.display());
        files.insert(path.clone());
        for entry in fs::read_dir(path.parent().unwrap())? {
            let path = entry?.path();
            if path
                .extension()
                .is_some_and(|e| e.to_string_lossy().eq_ignore_ascii_case("dll"))
            {
                files.insert(path);
            }
        }
    }
    let launcher = runner::launcher(&root, &paths[0])?;
    files.insert(resolve_program(launcher.get_program())?);
    files.into_iter().map(|p| file(&p)).collect()
}
/// GNU-style escaping of whitespace coexists with literal Windows backslashes.
pub fn dependency_paths(text: &str, directory: &Path) -> Result<Vec<PathBuf>> {
    let text = text.replace("\r\n", "\n").replace("\\\n", " ");
    let separator = text
        .char_indices()
        .find(|(at, c)| *c == ':' && text[*at + 1..].starts_with(char::is_whitespace))
        .context("dependency output lacks a target separator")?
        .0;
    let mut words = vec![];
    let mut word = String::new();
    let mut chars = text[separator + 1..].chars().peekable();
    while let Some(c) = chars.next() {
        if c == '\\' && chars.peek().is_some_and(|n| n.is_whitespace() || *n == '#') {
            word.push(chars.next().unwrap());
        } else if c.is_whitespace() {
            if !word.is_empty() {
                words.push(std::mem::take(&mut word));
            }
        } else {
            word.push(c);
        }
    }
    if !word.is_empty() {
        words.push(word);
    }
    ensure!(!words.is_empty(), "dependency scan has no inputs");
    let mut paths = BTreeSet::new();
    for word in words {
        let word = word.replace('\\', "/");
        let mut path = PathBuf::from(&word);
        if cfg!(unix) && word.as_bytes().get(1) == Some(&b':') {
            let unix = PathBuf::from(&word[2..]);
            let drive = PathBuf::from(format!(
                "/mnt/{}/{}",
                word[..1].to_ascii_lowercase(),
                word[2..].trim_start_matches('/')
            ));
            path = if unix.exists() { unix } else { drive };
        }
        if !path.is_absolute() {
            path = directory.join(path);
        }
        paths.insert(
            path.canonicalize()
                .with_context(|| format!("resolve dependency {}", path.display()))?,
        );
    }
    Ok(paths.into_iter().collect())
}
pub fn capture(
    request: &CompileRequest,
    scratch: &Path,
    cancel: &dyn Fn() -> bool,
) -> Result<Snapshot> {
    fs::create_dir_all(scratch)?;
    let mut flags = request
        .flags
        .iter()
        .filter(|f| !matches!(f.as_str(), "-MD" | "-MMD" | "-c" | "--"))
        .cloned()
        .collect::<Vec<_>>();
    let mut source = request.source.clone();
    runner::apply_overrides(request, scratch, &mut flags, &mut source)?;
    let actual_source = if Path::new(&source).is_absolute() {
        PathBuf::from(&source)
    } else {
        request.working_directory.join(&source)
    }
    .canonicalize()?;
    // The stock driver can have a different implicit include path. Both closures
    // are inputs to cached stock-equivalence checks, even when the oracle's
    // preprocessed source is unchanged.
    let compilers = [
        (
            "oracle",
            format!("build/{}/oracle/mwcceppc.exe", request.version.name()),
        ),
        (
            "stock",
            format!(
                "build/compilers/GC/{}/mwcceppc.exe",
                request.version.compiler_directory()
            ),
        ),
    ];
    let mut dependencies = BTreeSet::new();
    let mut preprocessed = Vec::new();
    for (role, path) in compilers {
        let compiler = request.root.join(path).canonicalize()?;
        let output = scratch.join(role);
        for (option, extension) in [("-M", "d"), ("-E", "i")] {
            let mut flags = flags.clone();
            flags.push(option.into());
            let execution = runner::execute_controlled(
                Invocation {
                    root: &request.root,
                    executable: &compiler,
                    flags: &flags,
                    source: &source,
                    directory: &request.working_directory,
                    output: &output.with_extension(extension),
                    trace_file: &scratch.join(format!("{role}-preprocessing.trace")),
                    options: &TraceOptions::default(),
                    timeout: request.timeout,
                },
                cancel,
            )?;
            if execution.cancelled {
                return Err(runner::Cancelled.into());
            }
            ensure!(
                execution.exit_code == Some(0) && !execution.timed_out,
                "{role} input scan failed ({:?}): {}",
                execution.exit_code,
                execution.diagnostics
            );
        }
        let bytes = fs::read(output.with_extension("d"))?;
        let scanned = dependency_paths(
            &bytes.iter().map(|b| char::from(*b)).collect::<String>(),
            &request.working_directory,
        )?;
        ensure!(
            scanned.contains(&actual_source),
            "{role} dependency scan omitted the source"
        );
        dependencies.extend(scanned);
        preprocessed.push(digest(&fs::read(output.with_extension("i"))?));
    }
    let mut limitations = vec![];
    let mut inputs = vec![];
    for dependency in dependencies {
        let raw = fs::read(&dependency)?;
        if [b"__TIME__".as_slice(), b"__DATE__", b"__TIMESTAMP__"]
            .iter()
            .any(|needle| raw.windows(needle.len()).any(|part| part == *needle))
        {
            limitations.push(format!("time-dependent macro in {}", dependency.display()));
        }
        inputs.push(FileInput {
            path: dependency,
            sha256: digest(&raw),
        });
    }
    Ok(Snapshot {
        format_version: 1,
        source: actual_source.clone(),
        source_sha256: digest(&fs::read(actual_source)?),
        working_directory: request.working_directory.canonicalize()?,
        flags,
        trace_options: serde_json::to_value(&request.options)?,
        runtime: runtime(request)?,
        environment_sha256: environment_digest(),
        dependencies: inputs,
        preprocessed_sha256: preprocessed[0].clone(),
        stock_preprocessed_sha256: preprocessed[1].clone(),
        reusable: limitations.is_empty(),
        cache_limitations: limitations,
    })
}
pub fn key(snapshot: &Snapshot) -> Result<String> {
    Ok(digest(&serde_json::to_vec(snapshot)?))
}
#[derive(Clone, Debug, Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct CheckCommand {
    pub program: PathBuf,
    pub arguments: Vec<String>,
    pub timeout_seconds: u64,
    #[serde(default)]
    pub inputs: Vec<PathBuf>,
}
pub fn checker_inputs(check: &CheckCommand, directory: &Path) -> Result<Vec<FileInput>> {
    let mut paths = BTreeSet::from([resolve_program(check.program.as_os_str())?]);
    for path in &check.inputs {
        paths.insert(if path.is_absolute() {
            path.clone()
        } else {
            directory.join(path)
        });
    }
    for argument in &check.arguments {
        let path = directory.join(argument);
        if path.is_file() {
            paths.insert(path);
        }
    }
    paths.into_iter().map(|p| file(&p)).collect()
}
#[derive(Clone, Debug, Serialize, Deserialize)]
pub struct CheckOutcome {
    pub command: Vec<String>,
    pub program_sha256: String,
    pub exit_code: Option<i32>,
    pub timed_out: bool,
    pub diagnostics: String,
}
pub fn semantic_check(
    check: &CheckCommand,
    source: &Path,
    object: &Path,
    directory: &Path,
    cancel: &dyn Fn() -> bool,
) -> Result<CheckOutcome> {
    ensure!(
        check.timeout_seconds > 0,
        "semantic-check timeout must be positive"
    );
    if cancel() {
        return Err(runner::Cancelled.into());
    }
    let program = resolve_program(check.program.as_os_str())?;
    let arguments = check
        .arguments
        .iter()
        .map(|arg| {
            arg.replace("{source}", &source.to_string_lossy())
                .replace("{object}", &object.to_string_lossy())
        })
        .collect::<Vec<_>>();
    let log = tempfile::NamedTempFile::new()?;
    let mut command = Command::new(&program);
    command
        .args(&arguments)
        .current_dir(directory)
        .stdin(Stdio::null())
        .stdout(Stdio::from(log.as_file().try_clone()?))
        .stderr(Stdio::from(log.as_file().try_clone()?));
    process_tree::prepare(&mut command);
    let mut child = command.spawn()?;
    let began = Instant::now();
    let mut timed_out = false;
    let status = loop {
        if let Some(status) = child.try_wait()? {
            break status;
        }
        if cancel() {
            process_tree::terminate(&mut child)?;
            return Err(runner::Cancelled.into());
        }
        if began.elapsed() >= Duration::from_secs(check.timeout_seconds) {
            timed_out = true;
            process_tree::terminate(&mut child)?;
            break child.wait()?;
        }
        thread::sleep(Duration::from_millis(10));
    };
    Ok(CheckOutcome {
        command: std::iter::once(program.to_string_lossy().into_owned())
            .chain(arguments)
            .collect(),
        program_sha256: digest(&fs::read(program)?),
        exit_code: status.code(),
        timed_out,
        diagnostics: fs::read(log.path())?
            .iter()
            .map(|b| char::from(*b))
            .collect(),
    })
}
