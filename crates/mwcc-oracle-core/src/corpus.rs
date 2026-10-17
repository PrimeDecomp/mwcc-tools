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
    /// The source arena stream (`pv.arena.*`), and (SOURCE_NODES) the source node stream: traced, and compared
    /// with the reference's.
    pub source_arenas: bool,
    pub source_nodes: bool,
    /// Another oracle to compare the oracle's trace with (the same streams, on each input).
    pub trace_reference: Option<PathBuf>,
    /// A compiler run in this process instead of the oracle (`executable` is then the path it runs as).
    pub in_process: Option<InProcess>,
}

/// A compiler run in this process: it takes an invocation as `runner::execute` does.
pub struct InProcess(
    pub Box<dyn Fn(runner::Invocation<'_>) -> Result<runner::Execution> + Send + Sync>,
);

impl std::fmt::Debug for InProcess {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.write_str("InProcess")
    }
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

/// Is VALUE an address in the compiler's image or heap (which two links of it place differently)?
fn is_address(value: u32) -> bool {
    (0x0040_0000..0x1000_0000).contains(&value)
}

/// A trace record as two links of the same compiler give it, or None for a provenance record (`pv.*`, from the
/// guarded probes, which only the oracle linked from the original's split has): the addresses (is_address) in its
/// 8-digit hex words, its 7- to 9-digit decimal values, `caller=` (a return address, unpadded) and, after the two
/// kind bytes, each 12-byte operand of `ops=` that holds one (as a little-endian word at an even offset), made `*`.
pub fn comparable_record(line: &str) -> Option<String> {
    static WORD: std::sync::LazyLock<Regex> =
        std::sync::LazyLock::new(|| Regex::new(r"\b[0-9a-f]{8}\b").unwrap());
    static OPERAND: std::sync::LazyLock<Regex> =
        std::sync::LazyLock::new(|| Regex::new(r"\b[0-9a-f]{24}\b").unwrap());
    static CALLER: std::sync::LazyLock<Regex> =
        std::sync::LazyLock::new(|| Regex::new(r"\bcaller=[0-9a-f]+\b").unwrap());
    // (a decimal field holding one: `st.block`'s weight, not yet set, is a stale pointer)
    static DECIMAL: std::sync::LazyLock<Regex> =
        std::sync::LazyLock::new(|| Regex::new(r"=([0-9]{7,9})\b").unwrap());
    if line.starts_with("pv.") {
        return None;
    }
    let line = CALLER.replace_all(line, "caller=*");
    let line = OPERAND.replace_all(&line, |c: &regex::Captures| {
        let bytes: Vec<u8> = (0..12)
            .map(|i| u8::from_str_radix(&c[0][2 * i..2 * i + 2], 16).unwrap_or(0))
            .collect();
        if (2..=8)
            .step_by(2)
            .any(|at| is_address(u32::from_le_bytes(bytes[at..at + 4].try_into().unwrap())))
        {
            format!("{}********************", &c[0][..4])
        } else {
            c[0].to_owned()
        }
    });
    let line = DECIMAL.replace_all(&line, |c: &regex::Captures| {
        if c[1].parse().is_ok_and(is_address) {
            "=*".to_owned()
        } else {
            c[0].to_owned()
        }
    });
    Some(
        WORD.replace_all(&line, |c: &regex::Captures| {
            if u32::from_str_radix(&c[0], 16).is_ok_and(is_address) {
                "*".to_owned()
            } else {
                c[0].to_owned()
            }
        })
        .into_owned(),
    )
}

/// How trace OURS differs from THEIRS (comparable_record), if it does.
pub fn trace_difference(ours: &str, theirs: &str) -> Option<String> {
    trace_difference_with(ours, theirs, &[])
}

/// The fields of a provenance record that hold an address in the compiler's memory, or its stack: made `*`.
/// (`caller`: a clone's return address, which the WebAssembly oracle cannot read and gives as 0)
const PROVENANCE_ADDRESSES: [&str; 9] = [
    "pools",
    "lock",
    "retail_esp",
    "address",
    "block",
    "freep",
    "caller",
    "operand",
    "output",
];
/// The fields that hold one of the original's addresses, the same in both: kept.
const PROVENANCE_SITES: [&str; 1] = ["site"];
/// The fields that depend on the order the compiler made its records in: their numbers, and a pool's free space
/// after an allocation. Dropped from the records whose order is not compared (PROVENANCE_UNORDERED), as `event` is
/// from every record.
const PROVENANCE_ORDER: [&str; 5] = ["request", "parent_request", "allocation", "event", "free"];
/// The records whose order follows the compiler's evaluation order, which C leaves unspecified (a call's arguments:
/// MWCC evaluates them from the last, clang from the first). Between two other records, they are compared as a
/// multiset.
const PROVENANCE_UNORDERED: [&str; 2] = ["pv.arena.request", "pv.arena.alloc"];
/// The fields that number another stream's records, which the WebAssembly oracle lacks yet (a birth's expression,
/// the expression stream's): dropped.
const PROVENANCE_PENDING: [(&str, &str); 1] = [("pv.value.birth", "expression")];

/// A provenance record (`pv.*`) as two oracles give it: its addresses (PROVENANCE_ADDRESSES, and is_address's but
/// for the original's, PROVENANCE_SITES) made `*`, and its numbers in the compiler's order (PROVENANCE_ORDER)
/// dropped.
pub fn comparable_provenance(line: &str) -> Option<String> {
    static FIELD: std::sync::LazyLock<Regex> =
        std::sync::LazyLock::new(|| Regex::new(r"\b([a-z_0-9]+)=([0-9a-f]{8})\b").unwrap());
    static ORDER: std::sync::LazyLock<Regex> =
        std::sync::LazyLock::new(|| Regex::new(r" ([a-z_]+)=[0-9]+\b").unwrap());
    if !line.starts_with("pv.") {
        return None;
    }
    let kind = line.split(' ').next().unwrap_or("");
    let unordered = PROVENANCE_UNORDERED.contains(&kind);
    let line = ORDER.replace_all(line, |c: &regex::Captures| {
        let field = &c[1];
        if field == "event"
            || (unordered && PROVENANCE_ORDER.contains(&field))
            || PROVENANCE_PENDING.contains(&(kind, field))
        {
            String::new()
        } else {
            c[0].to_owned()
        }
    });
    Some(
        FIELD
            .replace_all(&line, |c: &regex::Captures| {
                let address = PROVENANCE_ADDRESSES.contains(&&c[1])
                    || (!PROVENANCE_SITES.contains(&&c[1])
                        && u32::from_str_radix(&c[2], 16).is_ok_and(is_address));
                if address {
                    format!("{}=*", &c[1])
                } else {
                    c[0].to_owned()
                }
            })
            .into_owned(),
    )
}

/// The provenance records of TRACE (comparable_provenance's) of the STREAMS (their records' prefixes, `pv.arena.`):
/// each run of PROVENANCE_UNORDERED's, sorted.
fn provenance_records(trace: &str, streams: &[&str]) -> Vec<String> {
    let mut out = vec![];
    let mut run: Vec<String> = vec![];
    for record in trace
        .lines()
        .filter(|line| streams.iter().any(|s| line.starts_with(s)))
        .filter_map(comparable_provenance)
    {
        if PROVENANCE_UNORDERED.contains(&record.split(' ').next().unwrap_or("")) {
            run.push(record);
            continue;
        }
        run.sort();
        out.append(&mut run);
        out.push(record);
    }
    run.sort();
    out.append(&mut run);
    out
}

/// How trace OURS differs from THEIRS, if it does: comparable_record's, and provenance_records' of the STREAMS.
pub fn trace_difference_with(ours: &str, theirs: &str, streams: &[&str]) -> Option<String> {
    let records = |trace: &str| -> Vec<String> {
        let mut out: Vec<String> = trace.lines().filter_map(comparable_record).collect();
        out.extend(provenance_records(trace, streams));
        out
    };
    let a = records(ours);
    let b = records(theirs);
    if a == b {
        return None;
    }
    let at = a
        .iter()
        .zip(&b)
        .position(|(x, y)| x != y)
        .unwrap_or(a.len().min(b.len()));
    Some(format!(
        "traces differ ({} vs {} records), first at record {}: {:?} vs {:?}",
        a.len(),
        b.len(),
        at + 1,
        a.get(at).map_or("(end)", String::as_str),
        b.get(at).map_or("(end)", String::as_str)
    ))
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
        runner::runs(&request.root, &oracle) || request.in_process.is_some(),
        "oracle compiler missing: {}",
        oracle.display()
    );
    // (the driver finds itself by the path it is run as, from the job's directory)
    let oracle = std::path::absolute(&oracle)?;
    let scratch = tempfile::TempDir::new()?;
    let next = AtomicUsize::new(0);
    let outcomes: Mutex<Vec<Option<Result<Outcome>>>> =
        Mutex::new((0..request.jobs.len()).map(|_| None).collect());
    let source_arenas = request.source_arenas || request.source_nodes;
    let mut options = if request.all_traces {
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
    options.source_arenas = source_arenas;
    options.source_nodes = request.source_nodes;
    // (the provenance streams the WebAssembly oracle has too, compared when they are traced)
    let mut streams = vec![];
    if source_arenas {
        streams.push("pv.arena.");
    }
    if request.source_nodes {
        streams.push("pv.source.");
    }
    if request.all_traces {
        // (the register counters' records with the births' bindings, and the scalarization bindings; not yet the
        // functions' ends, which count the other streams' records)
        streams.extend([
            "pv.function.begin",
            "pv.value.",
            "pv.counter.",
            "pv.category.",
            "pv.scalar.",
        ]);
    }
    let null = Path::new(if cfg!(windows) { "NUL" } else { "/dev/null" });
    let reference = request
        .trace_reference
        .as_ref()
        .map(std::path::absolute)
        .transpose()?;
    let check = |index: usize, job: &Job| -> Result<Outcome> {
        let a = scratch.path().join(format!("{index}.real.o"));
        let b = scratch.path().join(format!("{index}.oracle.o"));
        let trace = scratch.path().join(format!("{index}.oracle.trace"));
        let reference_trace = scratch.path().join(format!("{index}.reference.trace"));
        // (the original itself: what the module, or the oracle, is compared with)
        let real = runner::execute_process(runner::Invocation {
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
        let invocation = runner::Invocation {
            root: &request.root,
            executable: &oracle,
            flags: &job.flags,
            source: &job.source,
            directory: &job.directory,
            output: &b,
            trace_file: if reference.is_some() { &trace } else { null },
            options: &options,
            timeout: request.timeout,
        };
        let ours = match &request.in_process {
            Some(InProcess(compile)) => compile(invocation)?,
            None => runner::execute(invocation)?,
        };
        let traces = match &reference {
            Some(executable) => {
                let c = scratch.path().join(format!("{index}.reference.o"));
                // (the executable itself, as the original: never a module run in its place)
                runner::execute_process(runner::Invocation {
                    root: &request.root,
                    executable,
                    flags: &job.flags,
                    source: &job.source,
                    directory: &job.directory,
                    output: &c,
                    trace_file: &reference_trace,
                    options: &options,
                    timeout: request.timeout,
                })?;
                let read = |p: &Path| -> Result<String> {
                    Ok(if p.is_file() {
                        String::from_utf8_lossy(&fs::read(p)?).into_owned()
                    } else {
                        String::new()
                    })
                };
                Some((read(&trace)?, read(&reference_trace)?))
            }
            None => None,
        };
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
        if let Some((ours, theirs)) = &traces
            && let Some(difference) = trace_difference_with(ours, theirs, &streams)
        {
            problems.push(difference);
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
            if let Some((ours, theirs)) = &traces {
                fs::write(keep.join(format!("{stem}.oracle.trace")), ours)?;
                fs::write(keep.join(format!("{stem}.reference.trace")), theirs)?;
            }
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
