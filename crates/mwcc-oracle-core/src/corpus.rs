use crate::{
    elf, ninja,
    runner::{self, TraceOptions},
    version::Version,
};
use anyhow::{Context, Result, ensure};
use regex::Regex;
use serde::Serialize;
use std::{
    collections::BTreeSet,
    fs,
    path::{Path, PathBuf},
    sync::{
        Mutex,
        atomic::{AtomicUsize, Ordering},
    },
    thread,
    time::Duration,
};

const BASELINE: &[&[&str]] = &[
    &["-nodefaults"],
    &["-proc", "gekko"],
    &["-align", "powerpc"],
    &["-enum", "int"],
    &["-fp", "hardware"],
    &["-O4,p"],
    &["-inline", "auto"],
    &["-maxerrors", "1"],
    &["-nosyspath"],
    &["-RTTI", "off"],
    &["-fp_contract", "on"],
    &["-str", "reuse"],
];

#[derive(Clone, Debug)]
pub struct Job {
    pub name: String,
    pub source: String,
    pub flags: Vec<String>,
    pub directory: PathBuf,
}

fn family(option: &str) -> &str {
    if option.starts_with("-O") || option == "-opt" {
        "-O"
    } else {
        option
    }
}

pub fn canary_jobs(directory: &Path) -> Result<Vec<Job>> {
    let directory = fs::canonicalize(directory)?;
    let mut files = fs::read_dir(&directory)?
        .map(|p| p.map(|p| p.path()))
        .collect::<std::io::Result<Vec<_>>>()?;
    files.sort();
    let mut jobs = vec![];
    for file in files {
        if !file.extension().is_some_and(|s| s == "c" || s == "cpp") {
            continue;
        }
        let data = fs::read(&file)?;
        let text = String::from_utf8_lossy(&data);
        let extra = text
            .lines()
            .next()
            .and_then(|l| l.strip_prefix("// flags:"))
            .map(ninja::split_flags)
            .transpose()?
            .unwrap_or_default();
        let overridden: BTreeSet<_> = extra
            .iter()
            .filter(|a| a.starts_with('-'))
            .map(|a| family(a))
            .collect();
        let mut flags: Vec<_> = BASELINE
            .iter()
            .filter(|group| !overridden.contains(family(group[0])))
            .flat_map(|group| group.iter().map(|s| s.to_string()))
            .collect();
        if file.extension().is_some_and(|s| s == "cpp") {
            flags.push("-lang=c++".into());
        }
        flags.extend(extra);
        jobs.push(Job {
            name: file
                .file_name()
                .context("canary has no name")?
                .to_string_lossy()
                .into_owned(),
            source: file.display().to_string(),
            flags,
            directory: directory.clone(),
        });
    }
    Ok(jobs)
}

pub fn project_jobs(directory: &Path, version: Version) -> Result<Vec<Job>> {
    let directory = fs::canonicalize(directory)?;
    let mut jobs = vec![];
    for r in ninja::parse(&fs::read_to_string(directory.join("build.ninja"))?)? {
        if r.mw_version.as_deref().and_then(|v| Version::parse(v).ok()) != Some(version) {
            continue;
        }
        let flags = r
            .flags
            .into_iter()
            .filter(|f| !matches!(f.as_str(), "-MD" | "-MMD" | "-c" | "--"))
            .collect();
        jobs.push(Job {
            name: r.source.clone(),
            source: r.source,
            flags,
            directory: directory.clone(),
        });
    }
    Ok(jobs)
}

#[derive(Debug)]
pub struct Request {
    pub root: PathBuf,
    pub version: Version,
    pub executable: Option<PathBuf>,
    pub jobs: Vec<Job>,
    pub parallelism: usize,
    pub keep: Option<PathBuf>,
    pub timeout: Duration,
    pub all_traces: bool,
}

#[derive(Debug, Serialize)]
pub struct Outcome {
    pub name: String,
    pub problems: Vec<String>,
}
#[derive(Debug, Serialize)]
pub struct Report {
    pub total: usize,
    pub identical: usize,
    pub outcomes: Vec<Outcome>,
}

fn normalise(text: &str) -> String {
    static EXE: std::sync::LazyLock<Regex> =
        std::sync::LazyLock::new(|| Regex::new(r"### \S*mwcceppc\.exe").unwrap());
    EXE.replace_all(&text.replace('\r', ""), "### mwcceppc.exe")
        .into_owned()
}

pub fn run(request: &Request) -> Result<Report> {
    ensure!(request.parallelism > 0, "--jobs must be positive");
    ensure!(!request.jobs.is_empty(), "no corpus inputs");
    let original = request.root.join(format!(
        "build/compilers/GC/{}/mwcceppc.exe",
        request.version.compiler_directory()
    ));
    let oracle = request.executable.clone().unwrap_or_else(|| {
        request.root.join(format!(
            "build/{}/oracle/mwcceppc.exe",
            request.version.name()
        ))
    });
    ensure!(
        original.is_file(),
        "original compiler missing: {}",
        original.display()
    );
    ensure!(
        oracle.is_file(),
        "oracle compiler missing: {}",
        oracle.display()
    );
    let scratch = tempfile::TempDir::new()?;
    let next = AtomicUsize::new(0);
    let outcomes: Mutex<Vec<Option<Result<Outcome>>>> =
        Mutex::new((0..request.jobs.len()).map(|_| None).collect());
    let options = if request.all_traces {
        TraceOptions {
            backend: true,
            source_arenas: false,
            source_nodes: false,
            schedule: true,
            inline_cost: true,
            full_cost: true,
            instantiation: true,
            locals: true,
            blocks: true,
            cse: true,
            stages: true,
        }
    } else {
        TraceOptions::default()
    };
    let null = Path::new(if cfg!(windows) { "NUL" } else { "/dev/null" });
    let check = |index: usize, job: &Job| -> Result<Outcome> {
        let a = scratch.path().join(format!("{index}.real.o"));
        let b = scratch.path().join(format!("{index}.oracle.o"));
        let real = runner::execute(runner::Invocation {
            root: &request.root,
            executable: &original,
            flags: &job.flags,
            source: &job.source,
            directory: &job.directory,
            output: &a,
            trace_file: null,
            options: &TraceOptions::default(),
            timeout: request.timeout,
        })?;
        let ours = runner::execute(runner::Invocation {
            root: &request.root,
            executable: &oracle,
            flags: &job.flags,
            source: &job.source,
            directory: &job.directory,
            output: &b,
            trace_file: null,
            options: &options,
            timeout: request.timeout,
        })?;
        let mut problems = vec![];
        if real.timed_out || ours.timed_out {
            problems.push(format!(
                "timeout: original={}, oracle={}",
                real.timed_out, ours.timed_out
            ));
        }
        if real.exit_code != ours.exit_code {
            problems.push(format!("exit {:?} vs {:?}", real.exit_code, ours.exit_code));
        }
        if normalise(&real.diagnostics) != normalise(&ours.diagnostics) {
            problems.push("messages differ".into());
        }
        let real_data = if a.is_file() { fs::read(&a)? } else { vec![] };
        let ours_data = if b.is_file() { fs::read(&b)? } else { vec![] };
        let x = elf::comparable_sections(&real_data)?;
        let y = elf::comparable_sections(&ours_data)?;
        if x != y {
            let names: BTreeSet<_> = x
                .keys()
                .chain(y.keys())
                .filter(|k| x.get(*k) != y.get(*k))
                .collect();
            problems.push(format!(
                "sections differ: {}",
                names.into_iter().cloned().collect::<Vec<_>>().join(", ")
            ));
        }
        if !problems.is_empty()
            && let Some(keep) = &request.keep
        {
            fs::create_dir_all(keep)?;
            let safe: String = job
                .name
                .chars()
                .map(|c| {
                    if c.is_alphanumeric() || "_.-".contains(c) {
                        c
                    } else {
                        '_'
                    }
                })
                .collect();
            let stem = format!("{index}-{safe}");
            fs::write(keep.join(format!("{stem}.real.o")), real_data)?;
            fs::write(keep.join(format!("{stem}.oracle.o")), ours_data)?;
            fs::write(keep.join(format!("{stem}.real.txt")), &real.diagnostics)?;
            fs::write(keep.join(format!("{stem}.oracle.txt")), &ours.diagnostics)?;
        }
        Ok(Outcome {
            name: job.name.clone(),
            problems,
        })
    };
    thread::scope(|scope| {
        for _ in 0..request.parallelism.min(request.jobs.len()) {
            scope.spawn(|| {
                loop {
                    let index = next.fetch_add(1, Ordering::Relaxed);
                    let Some(job) = request.jobs.get(index) else {
                        break;
                    };
                    let result =
                        check(index, job).with_context(|| format!("corpus input {}", job.name));
                    outcomes.lock().unwrap()[index] = Some(result);
                }
            });
        }
    });
    let outcomes = outcomes
        .into_inner()
        .unwrap()
        .into_iter()
        .map(|r| r.context("missing corpus worker result")?)
        .collect::<Result<Vec<_>>>()?;
    let identical = outcomes.iter().filter(|r| r.problems.is_empty()).count();
    Ok(Report {
        total: outcomes.len(),
        identical,
        outcomes,
    })
}
