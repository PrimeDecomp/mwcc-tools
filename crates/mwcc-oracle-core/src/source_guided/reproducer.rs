//! Local, content-addressed retained cases. Verification never executes files;
//! replay rebuilds relocated inputs and does not restore original paths.
use super::{inputs, objective, reduction, search, state};
use crate::{elf, evidence::EvaluationState, provenance, runner, version::Version};
use anyhow::{Context, Result, ensure};
use serde::{Deserialize, Serialize};
use serde_json::{Value, json};
use std::{
    cell::Cell,
    collections::{BTreeMap, BTreeSet},
    fs::{self, OpenOptions},
    path::{Component, Path, PathBuf},
    time::{Duration, Instant},
};

#[derive(Clone, Copy, Debug, Serialize, Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum RunKind {
    Search,
    Reduction,
}

#[derive(Clone, Debug, Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct File {
    pub path: String,
    pub sha256: String,
    pub executable: bool,
}
#[derive(Clone, Debug, Serialize, Deserialize)]
#[serde(tag = "kind", rename_all = "snake_case", deny_unknown_fields)]
pub enum Flag {
    Literal {
        value: String,
    },
    Include {
        option: String,
        directory: String,
        joined: bool,
    },
}
#[derive(Clone, Debug, Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct Case {
    pub role: String,
    pub input_sha256: String,
    pub capture_sha256: String,
    pub object_sha256: String,
    pub source: String,
    pub files: Vec<File>,
    pub directories: Vec<String>,
    pub flags: Vec<Flag>,
}
#[derive(Clone, Debug, Serialize, Deserialize)]
#[serde(tag = "kind", rename_all = "snake_case", deny_unknown_fields)]
pub enum Argument {
    Literal { value: String },
    File { path: String },
}
#[derive(Clone, Debug, Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct Checker {
    pub program: String,
    pub arguments: Vec<Argument>,
    pub inputs: Vec<String>,
    pub timeout_seconds: u64,
    /// Original paths/hashes are identity receipts, never replay destinations.
    pub original_inputs: Vec<inputs::FileInput>,
}
#[derive(Clone, Debug, Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct Origin {
    pub kind: RunKind,
    pub candidate: usize,
    pub plan_sha256: String,
    pub plan_blob_sha256: String,
    pub progress_sha256: String,
    pub analysis_sha256: String,
    pub analyzer_sha256: String,
}
#[derive(Clone, Debug, Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct Manifest {
    pub format_version: u32,
    pub host_os: String,
    pub host_arch: String,
    pub origin: Origin,
    pub replay_analyzer: File,
    pub contract: objective::Contract,
    pub version: Version,
    pub traces: runner::TraceOptions,
    pub timeout_seconds: u64,
    pub cases: Vec<Case>,
    /// Runtime/checker/target files relative to the materialized replay root.
    pub assets: Vec<File>,
    pub checker: Option<Checker>,
    pub target: Option<search::Target>,
    pub blobs: BTreeMap<String, u64>,
    pub capabilities: Value,
    pub limitations: Vec<String>,
}
#[derive(Clone, Debug, Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
struct Completion {
    format_version: u32,
    manifest_sha256: String,
}
#[derive(Clone, Debug, Serialize)]
pub struct Verification {
    pub format_version: u32,
    pub status: String,
    pub manifest_sha256: String,
    pub blobs: usize,
    pub bytes: u64,
    pub host_compatible: bool,
    pub original_analysis_sha256: String,
    pub replay_analyzer_sha256: String,
}
#[derive(Clone, Debug, Serialize)]
pub struct Replay {
    pub format_version: u32,
    pub status: String,
    pub manifest_sha256: String,
    pub original_analysis_sha256: String,
    pub replay_analyzer_sha256: String,
    pub fresh_compilations: usize,
    pub evaluation: Option<objective::EvaluationReport>,
    pub error: Option<String>,
}

fn hash(text: &str) -> Result<()> {
    ensure!(
        text.len() == 64
            && text
                .bytes()
                .all(|b| b.is_ascii_digit() || (b'a'..=b'f').contains(&b)),
        "invalid reproducer content hash"
    );
    Ok(())
}
fn relative(text: &str) -> Result<PathBuf> {
    ensure!(
        !text.is_empty() && !text.contains(['\\', ':']) && !text.starts_with('/'),
        "invalid reproducer relative path"
    );
    let path = PathBuf::from(text);
    ensure!(
        path.components().all(|c| matches!(c, Component::Normal(_))),
        "reproducer path escapes its root"
    );
    for part in text.split('/') {
        ensure!(
            !part.is_empty()
                && part != "."
                && part != ".."
                && !part.ends_with(['.', ' '])
                && !part.bytes().any(|b| b < 32),
            "unsupported reproducer path component"
        );
        let stem = part.split('.').next().unwrap().to_ascii_uppercase();
        ensure!(
            !matches!(stem.as_str(), "CON" | "PRN" | "AUX" | "NUL")
                && !(stem.len() == 4
                    && (stem.starts_with("COM") || stem.starts_with("LPT"))
                    && matches!(stem.as_bytes()[3], b'1'..=b'9')),
            "reserved reproducer path component"
        );
    }
    Ok(path)
}
fn checked_path(root: &Path, text: &str) -> Result<PathBuf> {
    let mut path = root.to_owned();
    for component in relative(text)?.components() {
        path.push(component);
        let metadata = fs::symlink_metadata(&path)?;
        #[cfg(windows)]
        {
            use std::os::windows::fs::MetadataExt;
            ensure!(
                metadata.file_attributes() & 0x400 == 0,
                "reproducer contains a reparse path"
            );
        }
        ensure!(
            !metadata.file_type().is_symlink(),
            "reproducer contains a symlink/reparse path"
        );
    }
    ensure!(
        path.canonicalize()?.starts_with(root.canonicalize()?),
        "reproducer file escapes its root"
    );
    Ok(path)
}
fn original_path(path: &Path) -> Result<String> {
    let text = path
        .to_str()
        .context("non-UTF-8 original path is unsupported")?
        .replace('\\', "/");
    let text = text.strip_prefix("//?/").unwrap_or(&text);
    ensure!(
        if cfg!(windows) {
            text.as_bytes().get(1) == Some(&b':') && text.as_bytes().get(2) == Some(&b'/')
        } else {
            text.starts_with('/')
        },
        "original path belongs to another host or is not absolute"
    );
    let mut parts = vec![];
    for part in text.split('/') {
        match part {
            "" | "." => {}
            ".." => {
                ensure!(parts.pop().is_some(), "original path escapes volume");
            }
            p => parts.push(p),
        }
    }
    Ok(if cfg!(windows) {
        parts.join("/")
    } else {
        format!("/{}", parts.join("/"))
    })
}
fn within(path: &Path, directory: &Path) -> Result<Option<String>> {
    let path = original_path(path)?;
    let directory = original_path(directory)?;
    let (p, d) = if cfg!(windows) {
        (path.to_ascii_lowercase(), directory.to_ascii_lowercase())
    } else {
        (path.clone(), directory.clone())
    };
    if p == d {
        return Ok(Some(String::new()));
    }
    Ok(p.starts_with(&(d.clone() + "/"))
        .then(|| path[directory.len() + 1..].to_owned()))
}
fn same_path(a: &Path, b: &Path) -> Result<bool> {
    Ok(within(a, b)? == Some(String::new()))
}
fn new_directory(path: &Path) -> Result<PathBuf> {
    ensure!(!path.exists(), "reproducer output must be a new directory");
    let parent = path
        .parent()
        .filter(|p| !p.as_os_str().is_empty())
        .unwrap_or(Path::new("."))
        .canonicalize()?;
    let name = path.file_name().context("output has no directory name")?;
    let output = parent.join(name);
    fs::create_dir(&output)?;
    Ok(output)
}
fn outside_new_output(path: &Path, protected: &Path) -> Result<()> {
    let parent = path
        .parent()
        .filter(|p| !p.as_os_str().is_empty())
        .unwrap_or(Path::new("."))
        .canonicalize()?;
    let output = parent.join(path.file_name().context("output has no name")?);
    ensure!(
        within(&output, protected)?.is_none(),
        "new output must be outside the retained run/bundle"
    );
    Ok(())
}
fn executable(path: &Path) -> Result<()> {
    #[cfg(unix)]
    {
        use std::os::unix::fs::PermissionsExt;
        fs::set_permissions(path, fs::Permissions::from_mode(0o700))?;
    }
    #[cfg(not(unix))]
    {
        let _ = path;
    }
    Ok(())
}
struct Writer {
    root: PathBuf,
    blobs: BTreeMap<String, u64>,
}
impl Writer {
    fn blob(&mut self, bytes: &[u8]) -> Result<String> {
        let sha = state::digest(bytes);
        let path = self.root.join("blobs").join(&sha);
        if !self.blobs.contains_key(&sha) {
            fs::write(path, bytes)?;
            self.blobs.insert(sha.clone(), bytes.len() as u64);
        }
        Ok(sha)
    }
    fn retained(&mut self, run: &Path, sha: &str) -> Result<String> {
        hash(sha)?;
        let bytes =
            fs::read(run.join("input-files").join(sha)).context("retained input blob missing")?;
        ensure!(state::digest(&bytes) == sha, "retained input blob changed");
        self.blob(&bytes)
    }
}
fn mapped_flags(snapshot: &inputs::Snapshot) -> Result<(Vec<Flag>, Vec<String>)> {
    let mut flags = vec![];
    let mut directories = BTreeSet::from(["project".to_owned()]);
    let mut i = 0;
    while i < snapshot.flags.len() {
        let token = &snapshot.flags[i];
        let include = if matches!(token.as_str(), "-i" | "-I") {
            i += 1;
            Some((
                token.clone(),
                snapshot
                    .flags
                    .get(i)
                    .context("include option lacks directory")?
                    .clone(),
                false,
            ))
        } else {
            token
                .strip_prefix("-I")
                .filter(|p| !p.is_empty())
                .map(|directory| ("-I".into(), directory.into(), true))
        };
        if let Some((option, directory, joined)) = include {
            let path = if Path::new(&directory).is_absolute() {
                PathBuf::from(directory)
            } else {
                snapshot.working_directory.join(directory)
            };
            let tail = within(&path, &snapshot.working_directory)?.context(
                "include directory outside captured working directory is not portable in schema 1",
            )?;
            let mapped = if tail.is_empty() {
                "project".into()
            } else {
                format!("project/{tail}")
            };
            relative(&mapped)?;
            directories.insert(mapped.clone());
            flags.push(Flag::Include {
                option,
                directory: mapped,
                joined,
            });
        } else {
            literal_flag(token)?;
            let wd = original_path(&snapshot.working_directory)?;
            ensure!(
                !Path::new(token).is_absolute()
                    && !token.replace('\\', "/").contains(&wd)
                    && token.as_bytes().get(1) != Some(&b':'),
                "unsupported absolute/path-bearing compiler argument {token}"
            );
            flags.push(Flag::Literal {
                value: token.clone(),
            });
        }
        i += 1;
    }
    Ok((flags, directories.into_iter().collect()))
}
fn literal_flag(token: &str) -> Result<()> {
    ensure!(
        !token.starts_with('@')
            && !token.contains(['/', '\\', ':'])
            && ![
                "-prefix",
                "-include",
                "-precompile",
                "-o",
                "-cwd",
                "-E",
                "-M",
                "-P"
            ]
            .iter()
            .any(|p| token.starts_with(p))
            && !matches!(token, "-i" | "-I"),
        "unsupported portable compiler argument {token}"
    );
    Ok(())
}
fn case(writer: &mut Writer, run: &Path, role: &str, key: &str) -> Result<(Case, search::Saved)> {
    let saved = search::load_artifact(run, key)?;
    let snapshot = &saved.inputs;
    ensure!(
        snapshot.format_version == 1 && snapshot.reusable,
        "non-reusable/unsupported captured inputs cannot be exported for replay"
    );
    ensure!(
        saved.compilation.check_status == runner::CheckStatus::Verified
            && !saved.compilation.original_fallback,
        "reproducer requires stock-verified oracle captures"
    );
    let mut files = vec![];
    let mut source = None;
    for input in &snapshot.dependencies {
        let is_source = same_path(&input.path, &snapshot.source)?;
        let path = if is_source && role != "original" {
            format!(
                "variant/{}",
                snapshot
                    .source
                    .file_name()
                    .context("source has no name")?
                    .to_str()
                    .context("source filename is not UTF-8")?
            )
        } else {
            let tail = within(&input.path, &snapshot.working_directory)?.context(
                "dependency outside working directory is unsupported in portable schema 1",
            )?;
            ensure!(!tail.is_empty(), "dependency is a directory");
            format!("project/{tail}")
        };
        relative(&path)?;
        if is_source {
            ensure!(
                source.replace(path.clone()).is_none(),
                "duplicate source dependency"
            );
            ensure!(
                input.sha256 == snapshot.source_sha256
                    && input.sha256 == saved.compilation.source_sha256,
                "source digest disagrees with capture"
            );
        }
        writer.retained(run, &input.sha256)?;
        files.push(File {
            path,
            sha256: input.sha256.clone(),
            executable: false,
        });
    }
    let (flags, directories) = mapped_flags(snapshot)?;
    let capture = fs::read(run.join("cache").join(key).join("capture.json"))?;
    Ok((
        Case {
            role: role.into(),
            input_sha256: key.into(),
            capture_sha256: writer.blob(&capture)?,
            object_sha256: writer.blob(&saved.compilation.object)?,
            source: source.context("captured source dependency missing")?,
            files,
            directories,
            flags,
        },
        saved,
    ))
}
fn identity(
    analyzer: &str,
    check: Option<&Vec<inputs::FileInput>>,
    target: Option<&str>,
) -> Result<String> {
    Ok(state::digest(&serde_json::to_vec(
        &json!({"analyzer_sha256":analyzer,"check_inputs":check,"target":target}),
    )?))
}
fn runtime_destination(path: &Path, oracle: &Path, version: Version) -> Result<String> {
    if let Some(tail) = within(path, oracle)? {
        let oracle_dir = format!("build/{}/oracle/", version.name());
        let stock_dir = format!("build/compilers/GC/{}/", version.compiler_directory());
        if tail.starts_with(&oracle_dir) || tail.starts_with(&stock_dir) {
            return Ok(format!("compiler/{tail}"));
        }
    }
    ensure!(cfg!(unix), "external compiler runtime unsupported");
    Ok("compiler/build/tools/wibo".into())
}

/// Export one captured search/reduction case. Old checker/target files and the
/// original analyzer must still be available and match the saved analysis hash.
pub fn export(
    kind: RunKind,
    run: &Path,
    id: usize,
    destination: &Path,
    run_analyzer: Option<&Path>,
) -> Result<Manifest> {
    let run = run.canonicalize()?;
    outside_new_output(destination, &run)?;
    let lock = OpenOptions::new().read(true).open(run.join("run.lock"))?;
    lock.try_lock_shared()
        .context("another process owns this retained run")?;
    let plan_bytes = fs::read(run.join("plan.json"))?;
    let progress_bytes = fs::read(run.join("progress.json"))?;
    let (plan, plan_sha, analysis_sha, original_key, baseline_key, candidate_key) = match kind {
        RunKind::Search => {
            let plan: search::Plan = serde_json::from_slice(&plan_bytes)?;
            search::validate(&plan)?;
            let p: search::Progress = serde_json::from_slice(&progress_bytes)?;
            ensure!(
                p.format_version == 1,
                "unsupported retained search progress"
            );
            let selected = p
                .outcomes
                .iter()
                .find(|o| o.id == id)
                .context("candidate unavailable")?;
            (
                plan,
                p.plan_sha256,
                p.analysis_sha256,
                p.original_input_sha256,
                p.baseline_artifact,
                selected.artifact.clone(),
            )
        }
        RunKind::Reduction => {
            let reduced: reduction::Plan = serde_json::from_slice(&plan_bytes)?;
            reduction::validate(&reduced)?;
            let p: reduction::Progress = serde_json::from_slice(&progress_bytes)?;
            ensure!(
                p.format_version == 1,
                "unsupported retained reduction progress"
            );
            let key = if id == 0 {
                p.baseline_artifact.clone()
            } else {
                p.outcomes
                    .iter()
                    .find(|o| o.candidate.id == id)
                    .and_then(|o| o.artifact.clone())
            };
            let plan = search::Plan {
                format_version: 1,
                build: reduced.build,
                contract: reduced.contract,
                transformations: vec![],
                maximum_depth: 0,
                generation_limit: 1,
                semantic_check: reduced.semantic_check,
                target: reduced.target,
            };
            ensure!(
                state::digest(&serde_json::to_vec(&serde_json::from_slice::<
                    reduction::Plan,
                >(&plan_bytes)?)?)
                    == p.plan_sha256,
                "saved reduction plan changed"
            );
            (
                plan,
                p.plan_sha256,
                p.analysis_sha256,
                p.original_input_sha256,
                p.baseline_artifact,
                key,
            )
        }
    };
    if matches!(kind, RunKind::Search) {
        ensure!(
            state::digest(&serde_json::to_vec(&plan)?) == plan_sha,
            "saved search plan changed"
        );
    }
    let original_key = original_key.context("original calibration capture unavailable")?;
    let baseline_key = baseline_key.context("baseline capture unavailable")?;
    let candidate_key = candidate_key.context("candidate has no retained capture")?;
    let original = search::load_artifact(&run, &original_key)?;
    let checker_inputs = plan
        .semantic_check
        .as_ref()
        .map(|c| inputs::checker_inputs(c, &original.inputs.working_directory))
        .transpose()?;
    let target = plan
        .target
        .as_ref()
        .map(|t| fs::read(&t.object))
        .transpose()?;
    let current = std::env::current_exe()?;
    let analyzer = run_analyzer.unwrap_or(&current);
    let analyzer_bytes =
        fs::read(analyzer).context("original run analyzer unavailable; supply --run-analyzer")?;
    let analyzer_sha = state::digest(&analyzer_bytes);
    ensure!(
        identity(
            &analyzer_sha,
            checker_inputs.as_ref(),
            target.as_ref().map(|b| state::digest(b)).as_deref()
        )? == analysis_sha,
        "original analyzer/checker/proof inputs/target do not match retained analysis identity"
    );
    let output = new_directory(destination)?;
    fs::create_dir(output.join("blobs"))?;
    let mut writer = Writer {
        root: output.clone(),
        blobs: BTreeMap::new(),
    };
    writer.blob(&analyzer_bytes)?;
    let plan_blob_sha = writer.blob(&plan_bytes)?;
    writer.blob(&progress_bytes)?;
    let tool_bytes = fs::read(&current)?;
    let tool_sha = writer.blob(&tool_bytes)?;
    let tool_name = if cfg!(windows) {
        "tools/mwcc-tools.exe"
    } else {
        "tools/mwcc-tools"
    };
    fs::create_dir(output.join("tools"))?;
    fs::write(output.join(tool_name), tool_bytes)?;
    executable(&output.join(tool_name))?;
    let mut cases = vec![];
    let mut saved_cases = vec![];
    for (role, key) in [
        ("original", &original_key),
        ("baseline", &baseline_key),
        ("candidate", &candidate_key),
    ] {
        let (c, s) = case(&mut writer, &run, role, key)?;
        cases.push(c);
        saved_cases.push(s);
    }
    let version = original.compilation.version;
    ensure!(
        saved_cases.iter().all(|s| s.compilation.version == version
            && s.compilation.trace_options.backend
            && s.inputs.working_directory == original.inputs.working_directory),
        "captured cases have different compiler/workspace context"
    );
    let mut assets = BTreeMap::<String, File>::new();
    for saved in &saved_cases {
        for input in &saved.inputs.runtime {
            let path = runtime_destination(&input.path, &plan.build.oracle_root, version)?;
            relative(&path)?;
            writer.retained(&run, &input.sha256)?;
            let file = File {
                path: path.clone(),
                sha256: input.sha256.clone(),
                executable: cfg!(unix) && path.ends_with("/wibo"),
            };
            if let Some(old) = assets.insert(path, file) {
                ensure!(
                    old.sha256 == input.sha256,
                    "runtime closure differs between captures"
                );
            }
        }
    }
    let mut checker = None;
    if let Some(check) = &plan.semantic_check {
        ensure!(
            !check.arguments.iter().any(|a| matches!(
                a.as_str(),
                "-c" | "/c" | "-Command" | "-EncodedCommand" | "-m"
            )),
            "embedded shell/interpreter commands are unsupported in portable schema 1"
        );
        let captured = checker_inputs.as_ref().unwrap();
        // The resolved program is identified independently of PATH ordering.
        let resolved = inputs::CheckCommand {
            inputs: vec![],
            arguments: vec![],
            ..check.clone()
        };
        let program = inputs::checker_inputs(&resolved, &original.inputs.working_directory)?
            .into_iter()
            .next()
            .context("checker program missing")?;
        let name = program
            .path
            .file_stem()
            .context("checker has no filename")?
            .to_string_lossy()
            .to_ascii_lowercase();
        ensure!(
            !matches!(
                name.as_str(),
                "sh" | "bash"
                    | "dash"
                    | "zsh"
                    | "cmd"
                    | "powershell"
                    | "pwsh"
                    | "node"
                    | "perl"
                    | "ruby"
                    | "lua"
            ) && !name.starts_with("python"),
            "interpreter/shell checker closure is unsupported; preserve its purpose and package a native checker instead"
        );
        let bytes = fs::read(&program.path)?;
        ensure!(
            if cfg!(windows) {
                bytes.starts_with(b"MZ")
            } else {
                bytes.starts_with(b"\x7fELF")
            },
            "only native executable checkers are supported; interpreter/script runtime closure must not be guessed"
        );
        let program_path = format!(
            "analysis/bin/{}",
            program
                .path
                .file_name()
                .unwrap()
                .to_str()
                .context("checker filename is not UTF-8")?
        );
        relative(&program_path)?;
        let mut paths = BTreeMap::new();
        for input in captured {
            let path = if input.path == program.path {
                program_path.clone()
            } else {
                let tail = within(&input.path, &original.inputs.working_directory)?.context(
                    "checker proof input outside captured working directory is unsupported",
                )?;
                format!("analysis/project/{tail}")
            };
            relative(&path)?;
            let bytes = fs::read(&input.path)?;
            ensure!(
                state::digest(&bytes) == input.sha256,
                "checker input changed while exporting"
            );
            writer.blob(&bytes)?;
            assets.insert(
                path.clone(),
                File {
                    path: path.clone(),
                    sha256: input.sha256.clone(),
                    executable: input.path == program.path,
                },
            );
            paths.insert(input.path.clone(), path);
        }
        if cfg!(windows) {
            for entry in fs::read_dir(program.path.parent().unwrap())? {
                let p = entry?.path();
                if p.extension()
                    .is_some_and(|e| e.to_string_lossy().eq_ignore_ascii_case("dll"))
                {
                    let path = format!(
                        "analysis/bin/{}",
                        p.file_name()
                            .unwrap()
                            .to_str()
                            .context("DLL filename is not UTF-8")?
                    );
                    relative(&path)?;
                    let sha = writer.blob(&fs::read(&p)?)?;
                    assets.insert(
                        path.clone(),
                        File {
                            path,
                            sha256: sha,
                            executable: false,
                        },
                    );
                }
            }
        }
        let mut arguments = vec![];
        for argument in &check.arguments {
            let path = if Path::new(argument).is_absolute() {
                PathBuf::from(argument)
            } else {
                original.inputs.working_directory.join(argument)
            };
            if let Some(mapped) = paths.get(&path).or_else(|| {
                paths
                    .iter()
                    .find(|(p, _)| same_path(p, &path).unwrap_or(false))
                    .map(|(_, p)| p)
            }) {
                arguments.push(Argument::File {
                    path: mapped.clone(),
                });
            } else {
                ensure!(
                    !Path::new(argument).is_absolute()
                        && !argument
                            .replace('\\', "/")
                            .contains(&original_path(&original.inputs.working_directory)?),
                    "undeclared absolute checker argument"
                );
                arguments.push(Argument::Literal {
                    value: argument.clone(),
                });
            }
        }
        checker = Some(Checker {
            program: program_path,
            arguments,
            inputs: paths.values().cloned().collect(),
            timeout_seconds: check.timeout_seconds,
            original_inputs: captured.clone(),
        });
    }
    let target = match (&plan.target, target) {
        (Some(t), Some(bytes)) => {
            let sha = writer.blob(&bytes)?;
            assets.insert(
                "analysis/target.o".into(),
                File {
                    path: "analysis/target.o".into(),
                    sha256: sha,
                    executable: false,
                },
            );
            Some(search::Target {
                object: "analysis/target.o".into(),
                link: t.link.clone(),
            })
        }
        _ => None,
    };
    let manifest=Manifest{format_version:1,host_os:std::env::consts::OS.into(),host_arch:std::env::consts::ARCH.into(),origin:Origin{kind,candidate:id,plan_sha256:plan_sha,plan_blob_sha256:plan_blob_sha,progress_sha256:state::digest(&progress_bytes),analysis_sha256:analysis_sha,analyzer_sha256:analyzer_sha},replay_analyzer:File{path:tool_name.into(),sha256:tool_sha,executable:true},contract:plan.contract,version,traces:original.compilation.trace_options.clone(),timeout_seconds:plan.build.timeout_seconds,cases,assets:assets.into_values().collect(),checker,target,blobs:writer.blobs,capabilities:json!({"retained_original_analyzer_identity":true,"pinned_replay_analyzer":true,"relocated_native_replay":true,"checker_closure":"declared_files_and_adjacent_native_dlls_only","transitive_checker_closure_proven":false,"os_sandbox":false}),limitations:vec!["Same host OS/architecture and compatible OS runtime required; this is not an OS sandbox.".into(),"Compiler dependencies/include roots must be within captured working directory; absolute includes or ambient fallbacks are rejected by fresh scans.".into(),"Original environment is retained as a digest only. Relocation creates a new input/environment identity; old cache identities are not reused.".into(),"Checker closure is declared files plus adjacent native DLLs, not proof of all transitive code/data/process dependencies. Script/interpreter commands are unsupported.".into(),"Path-sensitive macros/debug metadata must reproduce exact original objects; mismatches are not waived. Windows legacy compiler path limits still apply.".into()]};
    ensure!(
        fs::read(run.join("plan.json"))? == plan_bytes
            && fs::read(run.join("progress.json"))? == progress_bytes,
        "retained run changed during export"
    );
    search::atomic_json(&output.join("manifest.json"), &manifest)?;
    let bytes = fs::read(output.join("manifest.json"))?;
    search::atomic_json(
        &output.join("complete.json"),
        &Completion {
            format_version: 1,
            manifest_sha256: state::digest(&bytes),
        },
    )?;
    verify(&output)?;
    Ok(manifest)
}

fn blob(root: &Path, manifest: &Manifest, sha: &str) -> Result<Vec<u8>> {
    hash(sha)?;
    let size = manifest
        .blobs
        .get(sha)
        .context("manifest references an undeclared blob")?;
    let path = checked_path(root, &format!("blobs/{sha}"))?;
    ensure!(
        path.is_file() && fs::metadata(&path)?.len() == *size,
        "reproducer blob size differs"
    );
    let bytes = fs::read(path)?;
    ensure!(state::digest(&bytes) == sha, "reproducer blob was modified");
    Ok(bytes)
}
fn load(root: &Path) -> Result<(Manifest, String)> {
    let bytes = fs::read(checked_path(root, "manifest.json")?)?;
    let completion: Completion =
        serde_json::from_slice(&fs::read(checked_path(root, "complete.json")?)?)?;
    ensure!(
        completion.format_version == 1 && state::digest(&bytes) == completion.manifest_sha256,
        "incomplete or modified reproducer manifest"
    );
    let manifest: Manifest = serde_json::from_slice(&bytes)?;
    ensure!(
        manifest.format_version == 1,
        "unsupported reproducer schema"
    );
    Ok((manifest, completion.manifest_sha256))
}
fn file_set<'a>(files: impl Iterator<Item = &'a File>, windows: bool) -> Result<()> {
    let mut names = BTreeSet::new();
    for file in files {
        relative(&file.path)?;
        hash(&file.sha256)?;
        let name = if windows {
            file.path.to_ascii_lowercase()
        } else {
            file.path.clone()
        };
        ensure!(names.insert(name), "reproducer has colliding file paths");
    }
    for name in &names {
        let mut prefix = String::new();
        for part in name.split('/').take(name.split('/').count() - 1) {
            if !prefix.is_empty() {
                prefix.push('/');
            }
            prefix.push_str(part);
            ensure!(
                !names.contains(&prefix),
                "reproducer file/directory paths collide"
            );
        }
    }
    Ok(())
}
fn saved(root: &Path, manifest: &Manifest, case: &Case) -> Result<search::Saved> {
    let mut saved: search::Saved =
        serde_json::from_slice(&blob(root, manifest, &case.capture_sha256)?)?;
    ensure!(
        saved.inputs.format_version == 1
            && saved.inputs.reusable
            && inputs::key(&saved.inputs)? == case.input_sha256,
        "reproducer snapshot/key is invalid"
    );
    saved.compilation.object = blob(root, manifest, &case.object_sha256)?;
    ensure!(
        saved.compilation.version == manifest.version
            && saved.compilation.check_status == runner::CheckStatus::Verified
            && !saved.compilation.original_fallback
            && saved.compilation.object_sha256 == case.object_sha256,
        "reproducer capture/object/compiler binding is invalid"
    );
    ensure!(
        serde_json::to_value(&saved.compilation.trace_options)?
            == serde_json::to_value(&manifest.traces)?
            && saved.inputs.trace_options == serde_json::to_value(&manifest.traces)?,
        "reproducer trace options disagree"
    );
    let source = case
        .files
        .iter()
        .find(|f| f.path == case.source)
        .context("reproducer source not in closure")?;
    ensure!(
        source.sha256 == saved.inputs.source_sha256
            && source.sha256 == saved.compilation.source_sha256,
        "reproducer source/capture digest disagrees"
    );
    let expected = saved
        .inputs
        .dependencies
        .iter()
        .map(|input| input.sha256.clone())
        .collect::<BTreeSet<_>>();
    let actual = case
        .files
        .iter()
        .map(|input| input.sha256.clone())
        .collect::<BTreeSet<_>>();
    ensure!(
        expected == actual && case.files.len() == saved.inputs.dependencies.len(),
        "reproducer dependency closure differs from snapshot"
    );
    Ok(saved)
}
/// Offline integrity/shape checking only. This never launches a bundled tool,
/// compiler, launcher or checker. It is not an authenticity signature.
pub fn verify(bundle: &Path) -> Result<Verification> {
    let root = bundle.canonicalize()?;
    let (manifest, manifest_sha) = load(&root)?;
    let compatible =
        manifest.host_os == std::env::consts::OS && manifest.host_arch == std::env::consts::ARCH;
    ensure!(
        matches!(manifest.host_os.as_str(), "windows" | "linux" | "macos"),
        "unsupported recorded host OS"
    );
    ensure!(
        manifest.timeout_seconds > 0 && manifest.traces.backend,
        "invalid reproducer compiler bounds/options"
    );
    objective::validate(&manifest.contract)?;
    hash(&manifest.origin.analyzer_sha256)?;
    ensure!(
        manifest
            .blobs
            .contains_key(&manifest.origin.analyzer_sha256),
        "original analyzer blob missing"
    );
    let plan_bytes = blob(&root, &manifest, &manifest.origin.plan_blob_sha256)?;
    let (contract, original_checker, target_link, plan_sha, oracle_root) =
        match manifest.origin.kind {
            RunKind::Search => {
                let plan: search::Plan = serde_json::from_slice(&plan_bytes)?;
                search::validate(&plan)?;
                (
                    plan.contract.clone(),
                    plan.semantic_check.clone(),
                    plan.target.as_ref().map(|t| t.link.clone()),
                    state::digest(&serde_json::to_vec(&plan)?),
                    plan.build.oracle_root,
                )
            }
            RunKind::Reduction => {
                let plan: reduction::Plan = serde_json::from_slice(&plan_bytes)?;
                reduction::validate(&plan)?;
                (
                    plan.contract.clone(),
                    plan.semantic_check.clone(),
                    plan.target.as_ref().map(|t| t.link.clone()),
                    state::digest(&serde_json::to_vec(&plan)?),
                    plan.build.oracle_root,
                )
            }
        };
    ensure!(
        plan_sha == manifest.origin.plan_sha256
            && serde_json::to_value(contract)? == serde_json::to_value(&manifest.contract)?
            && original_checker.is_some() == manifest.checker.is_some()
            && target_link == manifest.target.as_ref().map(|t| t.link.clone()),
        "reproducer changed original predicate/purpose/checker/target"
    );
    let progress_bytes = blob(&root, &manifest, &manifest.origin.progress_sha256)?;
    let (original_key, baseline_key, candidate_key, analysis_sha, progress_plan_sha) =
        match manifest.origin.kind {
            RunKind::Search => {
                let p: search::Progress = serde_json::from_slice(&progress_bytes)?;
                ensure!(
                    p.format_version == 1,
                    "unsupported retained search progress"
                );
                let key = p
                    .outcomes
                    .iter()
                    .find(|o| o.id == manifest.origin.candidate)
                    .and_then(|o| o.artifact.clone());
                (
                    p.original_input_sha256,
                    p.baseline_artifact,
                    key,
                    p.analysis_sha256,
                    p.plan_sha256,
                )
            }
            RunKind::Reduction => {
                let p: reduction::Progress = serde_json::from_slice(&progress_bytes)?;
                ensure!(
                    p.format_version == 1,
                    "unsupported retained reduction progress"
                );
                let key = if manifest.origin.candidate == 0 {
                    p.baseline_artifact.clone()
                } else {
                    p.outcomes
                        .iter()
                        .find(|o| o.candidate.id == manifest.origin.candidate)
                        .and_then(|o| o.artifact.clone())
                };
                (
                    p.original_input_sha256,
                    p.baseline_artifact,
                    key,
                    p.analysis_sha256,
                    p.plan_sha256,
                )
            }
        };
    ensure!(
        analysis_sha == manifest.origin.analysis_sha256
            && progress_plan_sha == manifest.origin.plan_sha256,
        "retained progress analysis/plan identity disagrees"
    );
    ensure!(
        manifest.cases.len() == 3
            && manifest
                .cases
                .iter()
                .zip([original_key, baseline_key, candidate_key])
                .all(|(case, key)| key.as_deref() == Some(case.input_sha256.as_str())),
        "reproducer cases differ from selected retained progress"
    );
    file_set(
        manifest
            .assets
            .iter()
            .chain(std::iter::once(&manifest.replay_analyzer)),
        manifest.host_os == "windows",
    )?;
    let assets = manifest
        .assets
        .iter()
        .map(|f| (f.path.as_str(), f))
        .collect::<BTreeMap<_, _>>();
    for file in &manifest.assets {
        blob(&root, &manifest, &file.sha256)?;
    }
    let roles = manifest
        .cases
        .iter()
        .map(|c| c.role.as_str())
        .collect::<Vec<_>>();
    ensure!(
        roles == ["original", "baseline", "candidate"],
        "reproducer needs exactly original/baseline/candidate cases"
    );
    for case in &manifest.cases {
        relative(&case.source)?;
        file_set(case.files.iter(), manifest.host_os == "windows")?;
        for directory in &case.directories {
            relative(directory)?;
            ensure!(
                directory == "project" || directory.starts_with("project/"),
                "invalid include directory namespace"
            );
        }
        for file in &case.files {
            ensure!(
                file.path.starts_with("project/") || file.path.starts_with("variant/"),
                "invalid source/dependency namespace"
            );
            blob(&root, &manifest, &file.sha256)?;
        }
        for flag in &case.flags {
            match flag {
                Flag::Literal { value } => literal_flag(value)?,
                Flag::Include {
                    option, directory, ..
                } => {
                    ensure!(
                        matches!(option.as_str(), "-i" | "-I")
                            && case.directories.contains(directory),
                        "invalid mapped include option"
                    );
                    relative(directory)?;
                }
            }
        }
        let old = saved(&root, &manifest, case)?;
        let oracle = format!(
            "compiler/build/{}/oracle/mwcceppc.exe",
            manifest.version.name()
        );
        let stock = format!(
            "compiler/build/compilers/GC/{}/mwcceppc.exe",
            manifest.version.compiler_directory()
        );
        ensure!(
            assets
                .get(oracle.as_str())
                .is_some_and(|f| f.sha256 == old.compilation.producer_sha256)
                && assets
                    .get(stock.as_str())
                    .is_some_and(|f| Some(&f.sha256) == old.compilation.original_sha256.as_ref()),
            "compiler assets disagree with captured identities"
        );
        if compatible {
            let (flags, dirs) = mapped_flags(&old.inputs)?;
            ensure!(
                serde_json::to_value(flags)? == serde_json::to_value(&case.flags)?
                    && dirs == case.directories,
                "mapped flags differ from captured flags"
            );
            for dependency in &old.inputs.dependencies {
                let path = if same_path(&dependency.path, &old.inputs.source)?
                    && case.role != "original"
                {
                    format!(
                        "variant/{}",
                        old.inputs
                            .source
                            .file_name()
                            .unwrap()
                            .to_str()
                            .context("source filename is not UTF-8")?
                    )
                } else {
                    format!(
                        "project/{}",
                        within(&dependency.path, &old.inputs.working_directory)?
                            .context("dependency outside portable workspace")?
                    )
                };
                ensure!(
                    case.files
                        .iter()
                        .any(|f| f.path == path && f.sha256 == dependency.sha256),
                    "dependency destination mapping changed"
                );
                if same_path(&dependency.path, &old.inputs.source)? {
                    ensure!(
                        case.source == path,
                        "source destination differs from retained source binding"
                    );
                }
            }
            for runtime in &old.inputs.runtime {
                let path = runtime_destination(&runtime.path, &oracle_root, manifest.version)?;
                ensure!(
                    assets
                        .get(path.as_str())
                        .is_some_and(|f| f.sha256 == runtime.sha256),
                    "retained runtime file missing from materialized closure"
                );
            }
        }
    }
    let target_sha = manifest
        .target
        .as_ref()
        .map(|t| -> Result<&str> {
            let path = t.object.to_str().context("invalid target path")?;
            relative(path)?;
            Ok(&assets.get(path).context("target blob missing")?.sha256)
        })
        .transpose()?;
    if let Some(check) = &manifest.checker {
        ensure!(check.timeout_seconds > 0, "invalid checker timeout");
        relative(&check.program)?;
        let program = assets
            .get(check.program.as_str())
            .context("checker program asset missing")?;
        ensure!(
            check.program.starts_with("analysis/bin/")
                && check
                    .original_inputs
                    .iter()
                    .any(|f| f.sha256 == program.sha256),
            "checker program not in original declared closure"
        );
        let bytes = blob(&root, &manifest, &program.sha256)?;
        ensure!(
            if manifest.host_os == "windows" {
                bytes.starts_with(b"MZ")
            } else {
                bytes.starts_with(b"\x7fELF")
            },
            "unsupported checker executable shape"
        );
        for path in &check.inputs {
            relative(path)?;
            ensure!(
                assets.contains_key(path.as_str()),
                "checker input asset missing"
            );
        }
        for file in &check.original_inputs {
            hash(&file.sha256)?;
            ensure!(
                check.inputs.iter().any(|p| assets
                    .get(p.as_str())
                    .is_some_and(|f| f.sha256 == file.sha256)),
                "original checker/proof input missing from bundle"
            );
        }
        for arg in &check.arguments {
            match arg {
                Argument::File { path } => {
                    relative(path)?;
                    ensure!(
                        check.inputs.contains(path),
                        "checker argument not in declared closure"
                    );
                }
                Argument::Literal { value } => ensure!(
                    !matches!(
                        value.as_str(),
                        "-c" | "/c" | "-Command" | "-EncodedCommand" | "-m"
                    ),
                    "unsupported embedded checker command"
                ),
            }
        }
        if compatible {
            let original = original_checker.as_ref().unwrap();
            let old = saved(&root, &manifest, &manifest.cases[0])?;
            let programs = check
                .original_inputs
                .iter()
                .filter(|f| {
                    if original.program.is_absolute() {
                        same_path(&f.path, &original.program).unwrap_or(false)
                    } else {
                        f.path.file_name().is_some_and(|name| {
                            name == original.program.as_os_str()
                                || (cfg!(windows)
                                    && name.to_string_lossy().eq_ignore_ascii_case(&format!(
                                        "{}.exe",
                                        original.program.to_string_lossy()
                                    )))
                        })
                    }
                })
                .collect::<Vec<_>>();
            ensure!(
                programs.len() == 1,
                "original checker program binding is ambiguous"
            );
            let expected_program = format!(
                "analysis/bin/{}",
                programs[0].path.file_name().unwrap().to_string_lossy()
            );
            ensure!(
                check.program == expected_program
                    && program.sha256 == programs[0].sha256
                    && check.timeout_seconds == original.timeout_seconds,
                "checker program/timeout differs from original plan"
            );
            let mut paths = BTreeMap::new();
            for file in &check.original_inputs {
                let mapped = if file.path == programs[0].path {
                    expected_program.clone()
                } else {
                    format!(
                        "analysis/project/{}",
                        within(&file.path, &old.inputs.working_directory)?
                            .context("checker proof outside portable workspace")?
                    )
                };
                ensure!(
                    assets
                        .get(mapped.as_str())
                        .is_some_and(|f| f.sha256 == file.sha256),
                    "checker file mapping differs from original closure"
                );
                paths.insert(file.path.clone(), mapped);
            }
            let mut arguments = vec![];
            for arg in &original.arguments {
                let path = if Path::new(arg).is_absolute() {
                    PathBuf::from(arg)
                } else {
                    old.inputs.working_directory.join(arg)
                };
                if let Some(mapped) = paths
                    .iter()
                    .find(|(p, _)| same_path(p, &path).unwrap_or(false))
                    .map(|(_, p)| p)
                {
                    arguments.push(Argument::File {
                        path: mapped.clone(),
                    });
                } else {
                    arguments.push(Argument::Literal { value: arg.clone() });
                }
            }
            ensure!(
                serde_json::to_value(arguments)? == serde_json::to_value(&check.arguments)?
                    && paths.into_values().collect::<Vec<_>>() == check.inputs,
                "checker arguments/inputs differ from original plan"
            );
        }
    }
    ensure!(
        identity(
            &manifest.origin.analyzer_sha256,
            manifest.checker.as_ref().map(|c| &c.original_inputs),
            target_sha
        )? == manifest.origin.analysis_sha256,
        "retained analysis identity differs from bundled analyzer/checker/target"
    );
    // Validate small contracts and path mappings before reading large analyzer
    // blobs. Every blob is still hashed before integrity can be reported.
    for sha in manifest.blobs.keys() {
        blob(&root, &manifest, sha)?;
    }
    let tool = checked_path(&root, &manifest.replay_analyzer.path)?;
    ensure!(
        tool.is_file() && state::digest(&fs::read(&tool)?) == manifest.replay_analyzer.sha256,
        "pinned replay analyzer is missing or modified"
    );
    ensure!(
        manifest
            .blobs
            .contains_key(&manifest.replay_analyzer.sha256),
        "replay analyzer blob missing"
    );
    Ok(Verification {
        format_version: 1,
        status: "verified_integrity".into(),
        manifest_sha256: manifest_sha,
        blobs: manifest.blobs.len(),
        bytes: manifest.blobs.values().sum(),
        host_compatible: compatible,
        original_analysis_sha256: manifest.origin.analysis_sha256,
        replay_analyzer_sha256: manifest.replay_analyzer.sha256,
    })
}

fn materialize_file(bundle: &Path, manifest: &Manifest, root: &Path, file: &File) -> Result<()> {
    let path = root.join(relative(&file.path)?);
    // Leave margin for paths added by the unmodified legacy compiler/launcher.
    ensure!(
        !cfg!(windows) || path.as_os_str().len() < 240,
        "portable replay path exceeds conservative Windows compiler limit; choose a shorter output root"
    );
    fs::create_dir_all(path.parent().unwrap())?;
    let mut out = OpenOptions::new()
        .write(true)
        .create_new(true)
        .open(&path)?;
    use std::io::Write;
    out.write_all(&blob(bundle, manifest, &file.sha256)?)?;
    if file.executable {
        executable(&path)?;
    }
    Ok(())
}
fn check_materialized(root: &Path, manifest: &Manifest) -> Result<()> {
    for file in &manifest.assets {
        ensure!(
            state::digest(&fs::read(checked_path(root, &file.path)?)?) == file.sha256,
            "materialized runtime/checker/target changed"
        );
    }
    for case in &manifest.cases {
        let directory = root.join("cases").join(&case.role);
        for file in &case.files {
            ensure!(
                state::digest(&fs::read(checked_path(&directory, &file.path)?)?) == file.sha256,
                "materialized source/dependency changed"
            );
        }
    }
    Ok(())
}
fn compiler_flags(case: &Case) -> Result<Vec<String>> {
    let mut flags = vec![];
    for flag in &case.flags {
        match flag {
            Flag::Literal { value } => flags.push(value.clone()),
            Flag::Include {
                option,
                directory: include,
                joined,
            } => {
                // Every compiler cwd is its own case/project. Relative include
                // roots preserve the same actual ordered flags between cases,
                // while fresh scans still prove their raw dependency closure.
                let include = relative(include)?;
                let tail = include.strip_prefix("project")?;
                let path = if tail.as_os_str().is_empty() {
                    ".".into()
                } else {
                    tail.to_string_lossy().into_owned()
                };
                if *joined {
                    flags.push(format!("{option}{path}"));
                } else {
                    flags.extend([option.clone(), path]);
                }
            }
        }
    }
    Ok(flags)
}
fn snapshot_closure(
    snapshot: &inputs::Snapshot,
    case: &Case,
    root: &Path,
    manifest: &Manifest,
) -> Result<()> {
    let directory = root.join("cases").join(&case.role);
    ensure!(snapshot.reusable, "relocated inputs are not reusable");
    let mut actual = BTreeMap::new();
    for file in &snapshot.dependencies {
        let path=within(&file.path,&directory)?.context("fresh dependency resolves outside relocated closure (absolute include/ambient fallback)")?;
        ensure!(
            actual.insert(path, file.sha256.clone()).is_none(),
            "duplicate relocated dependency"
        );
    }
    let expected = case
        .files
        .iter()
        .map(|f| (f.path.clone(), f.sha256.clone()))
        .collect::<BTreeMap<_, _>>();
    ensure!(
        actual == expected,
        "fresh dependency content/closure differs from retained inputs"
    );
    let expected_runtime = manifest
        .assets
        .iter()
        .filter(|f| f.path.starts_with("compiler/"))
        .map(|f| (f.path.clone(), f.sha256.clone()))
        .collect::<BTreeMap<_, _>>();
    let mut actual_runtime = BTreeMap::new();
    for file in &snapshot.runtime {
        let path=within(&file.path,root)?.context("compiler launcher/runtime resolves outside relocated closure; unset external MWCC_WIBO override")?;
        actual_runtime.insert(path, file.sha256.clone());
    }
    ensure!(
        actual_runtime == expected_runtime,
        "fresh runtime closure differs from retained compiler files"
    );
    Ok(())
}

/// Fresh relocated original/private-baseline/candidate compilation and checking.
/// No original path is used as a replay input; a new output directory is required.
pub fn replay(bundle: &Path, output: &Path, budget: &search::Budget) -> Result<Replay> {
    let began = Instant::now();
    let integrity = verify(bundle)?;
    ensure!(
        integrity.host_compatible,
        "bundle requires its recorded OS/architecture"
    );
    ensure!(
        inputs::analyzer_sha256()? == integrity.replay_analyzer_sha256,
        "replay analyzer changed; invoke the pinned tool under bundle/tools"
    );
    ensure!(
        budget.seconds > 0 && budget.compilations >= 3,
        "portable replay requires a positive time budget and at least three fresh compilations"
    );
    let bundle = bundle.canonicalize()?;
    let (manifest, manifest_sha) = load(&bundle)?;
    outside_new_output(output, &bundle)?;
    let output = new_directory(output)?;
    let control = search::Control {
        budget,
        began,
        compilations: Cell::new(0),
    };
    let mut report = Replay {
        format_version: 1,
        status: "failed_reproduction".into(),
        manifest_sha256: manifest_sha,
        original_analysis_sha256: manifest.origin.analysis_sha256.clone(),
        replay_analyzer_sha256: manifest.replay_analyzer.sha256.clone(),
        fresh_compilations: 0,
        evaluation: None,
        error: None,
    };
    let attempt = (|| -> Result<()> {
        for file in &manifest.assets {
            materialize_file(&bundle, &manifest, &output, file)?;
        }
        fs::create_dir_all(output.join("analysis/project"))?;
        for case in &manifest.cases {
            let directory = output.join("cases").join(&case.role);
            fs::create_dir_all(directory.join("project"))?;
            for d in &case.directories {
                fs::create_dir_all(directory.join(relative(d)?))?;
            }
            for file in &case.files {
                materialize_file(&bundle, &manifest, &directory, file)?;
            }
        }
        check_materialized(&output, &manifest)?;
        let mut fresh = vec![];
        for case in &manifest.cases {
            if control.cancelled() {
                return Err(runner::Cancelled.into());
            }
            let directory = output.join("cases").join(&case.role);
            let object = directory.join("object.o");
            let request = runner::CompileRequest {
                root: output.join("compiler"),
                working_directory: directory.join("project"),
                source: directory
                    .join(relative(&case.source)?)
                    .to_string_lossy()
                    .into_owned(),
                flags: compiler_flags(case)?,
                version: manifest.version,
                options: manifest.traces.clone(),
                overrides: vec![],
                check: true,
                original_fallback: false,
                cut_limit: None,
                merge_limit: None,
                keep_object: None,
                timeout: Duration::from_secs(manifest.timeout_seconds),
            };
            // Resolve the launcher before any compiler execution; an inherited
            // launcher override must not reconnect to the original installation.
            for runtime in inputs::runtime(&request)? {
                ensure!(
                    within(&runtime.path, &output)?.is_some(),
                    "external compiler launcher override is unsupported; use bundled wibo"
                );
            }
            let before = inputs::capture(&request, &directory.join("scan-before"), &|| {
                control.cancelled()
            })?;
            snapshot_closure(&before, case, &output, &manifest)?;
            ensure!(
                control.compilations.get() < budget.compilations,
                "compilation budget reached"
            );
            control.compilations.set(control.compilations.get() + 1);
            let compilation = runner::compile_controlled(&request, &|| control.cancelled())?;
            let after = inputs::capture(&request, &directory.join("scan-after"), &|| {
                control.cancelled()
            })?;
            ensure!(
                before == after,
                "relocated inputs changed during compilation"
            );
            snapshot_closure(&after, case, &output, &manifest)?;
            ensure!(
                compilation.check_status == runner::CheckStatus::Verified
                    && !compilation.original_fallback
                    && compilation.object_sha256 == case.object_sha256,
                "relocated fresh stock/reference object differs (path/environment-sensitive reproduction is unsupported)"
            );
            fs::write(&object, &compilation.object)?;
            let saved = search::Saved {
                inputs: before,
                compilation,
            };
            search::atomic_json(&directory.join("capture.json"), &saved)?;
            fresh.push(saved);
        }
        ensure!(
            elf::comparable_sections(&fresh[0].compilation.object)?
                == elf::comparable_sections(&fresh[1].compilation.object)?,
            "relocated private baseline calibration failed"
        );
        let check = manifest
            .checker
            .as_ref()
            .map(|check| -> Result<inputs::CheckCommand> {
                Ok(inputs::CheckCommand {
                    program: output.join(relative(&check.program)?),
                    arguments: check
                        .arguments
                        .iter()
                        .map(|a| -> Result<String> {
                            Ok(match a {
                                Argument::Literal { value } => value.clone(),
                                Argument::File { path } => {
                                    output.join(relative(path)?).to_string_lossy().into_owned()
                                }
                            })
                        })
                        .collect::<Result<_>>()?,
                    inputs: check
                        .inputs
                        .iter()
                        .map(|p| relative(p).map(|p| output.join(p)))
                        .collect::<Result<_>>()?,
                    timeout_seconds: check.timeout_seconds,
                })
            })
            .transpose()?;
        let target = manifest
            .target
            .as_ref()
            .map(|t| -> Result<search::Target> {
                Ok(search::Target {
                    object: output
                        .join(relative(t.object.to_str().context("invalid target path")?)?),
                    link: t.link.clone(),
                })
            })
            .transpose()?;
        let plan = search::Plan {
            format_version: 1,
            build: search::Build {
                oracle_root: output.join("compiler"),
                working_directory: output.join("analysis/project"),
                source: "unused".into(),
                version: Some(manifest.version),
                flags: vec![],
                from_ninja: None,
                traces: manifest.traces.clone(),
                timeout_seconds: manifest.timeout_seconds,
            },
            contract: manifest.contract.clone(),
            transformations: vec![],
            maximum_depth: 0,
            generation_limit: 1,
            semantic_check: check,
            target,
        };
        let baseline_source = output
            .join("cases/baseline")
            .join(relative(&manifest.cases[1].source)?);
        let candidate_source = output
            .join("cases/candidate")
            .join(relative(&manifest.cases[2].source)?);
        let baseline_object = output.join("cases/baseline/object.o");
        let candidate_object = output.join("cases/candidate/object.o");
        let baseline_check = search::checker(
            &plan,
            &baseline_source,
            &baseline_object,
            &baseline_source,
            &baseline_object,
            &plan.build.working_directory,
            &control,
        )?;
        let candidate_check = search::checker(
            &plan,
            &candidate_source,
            &candidate_object,
            &baseline_source,
            &baseline_object,
            &plan.build.working_directory,
            &control,
        )?;
        let baseline_report = provenance::parse(&fresh[1].compilation.trace)?;
        let candidate_report = provenance::parse(&fresh[2].compilation.trace)?;
        let raw_baseline = fs::read(&baseline_source)?;
        let raw_candidate = fs::read(&candidate_source)?;
        let mut evaluation = objective::evaluate(
            &manifest.contract,
            &search::input(&fresh[1], &baseline_report, &raw_baseline, &baseline_check),
            &search::input(
                &fresh[2],
                &candidate_report,
                &raw_candidate,
                &candidate_check,
            ),
        )?;
        evaluation.final_target_mismatch = search::final_mismatch(&plan, &fresh[2].compilation)?;
        let satisfied = evaluation.overall.state == EvaluationState::Satisfied;
        report.evaluation = Some(evaluation);
        check_materialized(&output, &manifest)?;
        verify(&bundle)?;
        report.status = if satisfied {
            "reproduced"
        } else {
            "predicate_not_satisfied"
        }
        .into();
        Ok(())
    })();
    report.fresh_compilations = control.compilations.get();
    if let Err(error) = attempt {
        report.status = if error.is::<runner::Cancelled>() {
            "cancelled"
        } else {
            "failed_reproduction"
        }
        .into();
        report.error = Some(format!("{error:#}"));
    }
    search::atomic_json(&output.join("replay.json"), &report)?;
    Ok(report)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn relocated_include_flags_are_equal_and_resolve_in_each_private_workspace() -> Result<()> {
        let temp = tempfile::tempdir()?;
        let mut observed = vec![];
        for (role, bytes) in [
            ("baseline", b"baseline header".as_slice()),
            ("candidate", b"candidate header".as_slice()),
        ] {
            let cwd = temp.path().join(role).join("project");
            fs::create_dir_all(cwd.join("include"))?;
            fs::write(cwd.join("include/input.h"), bytes)?;
            let case = Case {
                role: role.into(),
                input_sha256: String::new(),
                capture_sha256: String::new(),
                object_sha256: String::new(),
                source: "variant/unit.c".into(),
                files: vec![],
                directories: vec![],
                flags: vec![
                    Flag::Literal {
                        value: "-nodefaults".into(),
                    },
                    Flag::Include {
                        option: "-i".into(),
                        directory: "project/include".into(),
                        joined: false,
                    },
                    Flag::Include {
                        option: "-I".into(),
                        directory: "project".into(),
                        joined: true,
                    },
                    Flag::Literal {
                        value: "-O4,p".into(),
                    },
                ],
            };
            let flags = compiler_flags(&case)?;
            assert_eq!(fs::read(cwd.join(&flags[2]).join("input.h"))?, bytes);
            assert_eq!(
                cwd.join(flags[3].strip_prefix("-I").unwrap())
                    .canonicalize()?,
                cwd.canonicalize()?
            );
            observed.push(flags);
        }
        assert_eq!(
            observed[0], observed[1],
            "actual flags must satisfy the strict compiler-context comparison"
        );
        assert_eq!(
            observed[0],
            ["-nodefaults", "-i", "include", "-I.", "-O4,p"]
        );
        Ok(())
    }
}
