use crate::{
    elf, ninja,
    runner::{self, TraceOptions},
    version::Version,
};
use anyhow::{Context, Result, ensure};
use regex::Regex;
use serde::{Deserialize, Serialize};
use std::{
    collections::{BTreeMap, BTreeSet},
    fs,
    path::{Path, PathBuf},
    sync::{
        Mutex,
        atomic::{AtomicUsize, Ordering},
    },
    thread,
    time::Duration,
};

mod replay;
pub use replay::{Manifest, Project, stage};

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

#[derive(Clone, Debug, Deserialize, Serialize)]
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
    project_jobs_for(directory, version, &[], None).map(|(jobs, _)| jobs)
}

/// Resolve a generated graph without rewriting its working directory or compiler labels.
pub fn project_jobs_for(
    directory: &Path,
    version: Version,
    additional_versions: &[String],
    relocate: Option<&Path>,
) -> Result<(Vec<Job>, Project)> {
    let directory = std::path::absolute(directory)?;
    let graph = directory.join("build.ninja");
    let contents = fs::read_to_string(&graph)?;
    let mut jobs = vec![];
    let mut labels = BTreeSet::new();
    let mut edges = Vec::new();
    for r in ninja::parse(&contents)? {
        let Some(label) = &r.mw_version else { continue };
        if Version::parse(label).ok() != Some(version)
            && !additional_versions
                .iter()
                .any(|v| &v.replace('\\', "/") == label)
        {
            continue;
        }
        if r.precompile {
            continue;
        }
        labels.insert(label.clone());
        let mut flags: Vec<_> = r
            .flags
            .iter()
            .filter(|f| !matches!(f.as_str(), "-MD" | "-MMD" | "-c" | "--"))
            .cloned()
            .collect();
        let absolute = |path: &str| directory.join(path).to_string_lossy().replace('\\', "/");
        let mut source = r.source.clone();
        if relocate.is_some() {
            source = absolute(&source);
            let mut flags = flags.iter_mut();
            while let Some(flag) = flags.next() {
                if is_path_option(flag)
                    && let Some(value) = flags.next()
                {
                    *value = absolute(value);
                } else if let Some(value) =
                    flag.strip_prefix("-I").filter(|v| !matches!(*v, "" | "-"))
                {
                    *flag = format!("-I{}", absolute(value));
                }
            }
        }
        jobs.push(Job {
            name: source.clone(),
            source,
            flags,
            directory: relocate.unwrap_or(&directory).to_owned(),
        });
        edges.push(r);
    }
    let project = Project::new(graph, contents.as_bytes(), labels, &edges)?;
    Ok((jobs, project))
}

fn is_path_option(flag: &str) -> bool {
    matches!(flag, "-i" | "-I" | "-ir" | "-include" | "-prefix")
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
    /// The records left out of that comparison, by prefix (`--ignore st.`): the streams one side is known to lack.
    pub ignore: Vec<String>,
    /// A compiler run in this process instead of the oracle (`executable` is then the path it runs as).
    pub in_process: Option<InProcess>,
    /// Explicit native reference for direct comparison with the candidate.
    pub against: Option<PathBuf>,
    pub manifest: Option<Manifest>,
    pub cache: Option<PathBuf>,
    pub strict: bool,
    pub details: bool,
    pub preflight: bool,
    pub trace_only: Vec<String>,
    pub iro_phases: Option<String>,
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
    #[serde(skip_serializing_if = "Vec::is_empty")]
    pub categories: Vec<String>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub first_object_difference: Option<String>,
}
#[derive(Debug, Serialize)]
pub struct Report {
    pub total: usize,
    pub identical: usize,
    pub outcomes: Vec<Outcome>,
    #[serde(skip_serializing_if = "BTreeMap::is_empty")]
    pub categories: BTreeMap<String, usize>,
    #[serde(skip_serializing_if = "BTreeMap::is_empty")]
    pub clusters: BTreeMap<String, usize>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub cached_reference: Option<usize>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub preflight_checked: Option<usize>,
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
    comparable_record_with(line, false)
}

/// comparable_record's, but (CREATORS) a temporary's creator (`ce.object caller=`) kept: the WebAssembly oracle gives
/// the original's return address, as the x86 oracle linked from the original's split does.
fn comparable_record_with(line: &str, creators: bool) -> Option<String> {
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
    let line = if creators && line.starts_with("ce.object ") {
        std::borrow::Cow::Borrowed(line)
    } else {
        CALLER.replace_all(line, "caller=*")
    };
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

/// The stream of trace_difference_with that keeps the temporaries' creators.
pub const CREATORS: &str = "ce.object";

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
                // A node's type is a pointer and can land below the usual image range on Windows.
                let node_type = kind == "pv.source.node" && &c[1] == "type" && &c[2] != "00000000";
                let address = PROVENANCE_ADDRESSES.contains(&&c[1])
                    || node_type
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

/// TRACE without the records that begin with one of PREFIXES.
fn without(trace: &str, prefixes: &[String]) -> String {
    if prefixes.is_empty() {
        return trace.to_owned();
    }
    trace
        .lines()
        .filter(|line| !prefixes.iter().any(|p| line.starts_with(p.as_str())))
        .collect::<Vec<_>>()
        .join("\n")
}

/// How trace OURS differs from THEIRS, if it does: comparable_record's, and provenance_records' of the STREAMS. A
/// stream `ce.object` keeps the temporaries' creators (comparable_record_with).
pub fn trace_difference_with(ours: &str, theirs: &str, streams: &[&str]) -> Option<String> {
    let creators = streams.contains(&CREATORS);
    let records = |trace: &str| -> Vec<String> {
        let mut out: Vec<String> = trace
            .lines()
            .filter_map(|line| comparable_record_with(line, creators))
            .collect();
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

fn first_difference(a: &[u8], b: &[u8]) -> usize {
    a.iter()
        .zip(b)
        .position(|(a, b)| a != b)
        .unwrap_or(a.len().min(b.len()))
}

fn generated_names_only(a: &elf::SectionContents, b: &elf::SectionContents) -> bool {
    let (elf::SectionContents::Data(a), elf::SectionContents::Data(b)) = (a, b) else {
        return false;
    };
    let generated =
        |name: &[u8]| name.len() > 1 && name[0] == b'@' && name[1..].iter().all(u8::is_ascii_digit);
    let a = a.split(|byte| *byte == 0);
    let b = b.split(|byte| *byte == 0);
    a.clone().count() == b.clone().count()
        && a.zip(b)
            .all(|(a, b)| a == b || (generated(a) && generated(b)))
}

fn object_location(a: &[u8], b: &[u8], sections: &BTreeSet<&String>) -> Option<String> {
    let (a, b) = (elf::Elf::parse(a).ok()?, elf::Elf::parse(b).ok()?);
    // Prefer the first changed instruction byte over follow-on table differences.
    let section = sections
        .iter()
        .find(|name| name.starts_with(".text"))
        .or_else(|| sections.first())?;
    let (index, left) = a
        .sections
        .iter()
        .enumerate()
        .find(|(_, s)| &s.name == *section)?;
    let right = b.sections.iter().find(|s| &s.name == *section)?;
    let offset = first_difference(left.data, right.data);
    let symbol = a.symbols().ok()?.into_iter().find(|s| {
        let start = s.value as usize;
        s.section == index
            && s.kind == 2
            && (start..start.saturating_add(s.size as usize)).contains(&offset)
    });
    Some(match symbol {
        Some(s) => format!(
            "{section}+0x{offset:x}, {}+0x{:x}",
            s.name,
            offset - s.value as usize
        ),
        None => format!("{section}+0x{offset:x}"),
    })
}

fn compare(
    job: &Job,
    reference: &replay::Captured,
    candidate: &replay::Captured,
    output: &Path,
    strict: bool,
    details: bool,
) -> Result<Outcome> {
    let mut outcome = Outcome {
        name: job.name.clone(),
        problems: vec![],
        categories: vec![],
        first_object_difference: None,
    };
    let mut add = |category: &str, problem: String| {
        outcome.problems.push(problem);
        if details {
            outcome.categories.push(category.to_owned());
        }
    };
    let (real, ours) = (&reference.execution, &candidate.execution);
    if real.timed_out || ours.timed_out {
        add(
            "timeout",
            format!(
                "timeout: reference={}, candidate={}",
                real.timed_out, ours.timed_out
            ),
        );
    }
    if real.exit_code != ours.exit_code {
        add(
            "exit",
            format!("exit {:?} vs {:?}", real.exit_code, ours.exit_code),
        );
    }
    let messages_differ = if strict {
        real.diagnostics != ours.diagnostics
    } else {
        normalise(&real.diagnostics) != normalise(&ours.diagnostics)
    };
    if messages_differ {
        add("diagnostics", "messages differ".into());
    }
    let a = reference.files.get(output);
    let b = candidate.files.get(output);
    let a_data = a.map_or(&[][..], Vec::as_slice);
    let b_data = b.map_or(&[][..], Vec::as_slice);
    if (strict || a_data != b_data) && a.is_some() != b.is_some() {
        add("object presence", "primary output presence differs".into());
    }
    if a_data != b_data {
        let sections = |data: &[u8]| {
            let sections = elf::sections_for_comparison(data, strict);
            if strict && sections.is_err() {
                // An invalid or partial ELF is still an observable raw output.
                return Ok(BTreeMap::from([(
                    "<file>".into(),
                    elf::SectionContents::Data(data.to_vec()),
                )]));
            }
            sections
        };
        let x = sections(a_data)?;
        let y = sections(b_data)?;
        let names: BTreeSet<_> = x
            .keys()
            .chain(y.keys())
            .filter(|k| x.get(*k) != y.get(*k))
            .collect();
        if !names.is_empty() {
            let names_display = names
                .iter()
                .map(|name| name.as_str())
                .collect::<Vec<_>>()
                .join(", ");
            let category = match names.first().map(|name| name.as_str()) {
                Some(".strtab")
                    if names.len() == 1
                        && x.get(".strtab")
                            .zip(y.get(".strtab"))
                            .is_some_and(|(x, y)| generated_names_only(x, y)) =>
                {
                    "generated symbol IDs"
                }
                _ => "object sections",
            };
            add(category, format!("sections differ: {names_display}"));
            if details {
                for (family, prefixes) in [
                    ("instructions", &[".text"][..]),
                    ("relocations", &[".rel"][..]),
                    ("symbols", &[".symtab", ".strtab"][..]),
                    ("data", &[".data", ".sdata", ".rodata", ".bss", ".sbss"][..]),
                    ("debug and metadata", &[".debug", ".line", ".comment"][..]),
                ] {
                    if names
                        .iter()
                        .any(|name| prefixes.iter().any(|prefix| name.starts_with(prefix)))
                    {
                        outcome.categories.push(family.into());
                    }
                }
                outcome.first_object_difference = object_location(a_data, b_data, &names);
            }
        } else if strict {
            add(
                "raw object",
                format!(
                    "primary output bytes differ at 0x{:x}",
                    first_difference(a_data, b_data)
                ),
            );
        }
    }
    if strict {
        let paths: BTreeSet<_> = reference
            .files
            .keys()
            .chain(candidate.files.keys())
            .collect();
        for path in paths {
            if path != output && reference.files.get(path) != candidate.files.get(path) {
                outcome
                    .problems
                    .push(format!("side output differs: {}", path.display()));
                if details && !outcome.categories.iter().any(|cat| cat == "side outputs") {
                    outcome.categories.push("side outputs".into());
                }
            }
        }
    }
    Ok(outcome)
}

fn compare_dump(a: Option<String>, b: Option<String>) -> Option<String> {
    if a.is_none() && b.is_none() {
        return Some("optimizer dumps missing on both sides".into());
    }
    if a.is_none() || b.is_none() {
        return Some(format!(
            "optimizer dump missing: reference={}, candidate={}",
            a.is_none(),
            b.is_none()
        ));
    }
    let (a, b) = (a.as_deref().unwrap_or(""), b.as_deref().unwrap_or(""));
    let (mut left, mut right) = (a.lines(), b.lines());
    let mut phase = None;
    for line in 1.. {
        let (x, y) = (left.next(), right.next());
        if x != y {
            let context = phase.map_or_else(String::new, |header| format!(" ({header})"));
            return Some(format!(
                "optimizer dumps first differ at line {line}{context}: {x:?} vs {y:?}"
            ));
        }
        match x {
            Some(header) if header.starts_with("Dumping function ") => {
                phase = Some(header.trim_end())
            }
            None => break,
            _ => {}
        }
    }
    None
}

pub fn run(request: &Request) -> Result<Report> {
    ensure!(request.parallelism > 0, "--jobs must be positive");
    ensure!(!request.jobs.is_empty(), "no corpus inputs");
    ensure!(
        request.cache.is_none() || (request.manifest.is_some() && request.against.is_none()),
        "--cache needs a manifest and the stock reference"
    );
    ensure!(
        request.trace_reference.is_some()
            || (request.trace_only.is_empty() && request.iro_phases.is_none()),
        "trace and optimizer filters need --trace-reference"
    );
    if let Some(manifest) = &request.manifest {
        ensure!(
            manifest.version == request.version,
            "manifest compiler version differs from the request"
        );
        manifest.verify(&request.root)?;
    }
    let original = std::path::absolute(request.against.clone().unwrap_or_else(|| {
        mwcc_project::compilers::stock_executable(&request.root, request.version)
    }))?;
    let oracle = request.executable.clone().unwrap_or_else(|| {
        request.root.join(format!(
            "build/{}/oracle/mwcceppc.exe",
            request.version.name()
        ))
    });
    ensure!(
        original.is_file(),
        "reference compiler missing: {}",
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
    let cache = request
        .cache
        .as_ref()
        .zip(request.manifest.as_ref())
        .map(|(directory, manifest)| {
            replay::Cache::open(directory, manifest, &original, request.timeout)
        })
        .transpose()?;
    let keys = if cache.is_some() {
        let keys = request
            .jobs
            .iter()
            .map(replay::Cache::key)
            .collect::<Result<Vec<_>>>()?;
        ensure!(
            keys.iter().collect::<BTreeSet<_>>().len() == keys.len(),
            "duplicate corpus invocations cannot share a cached output path"
        );
        keys
    } else {
        (0..request.jobs.len())
            .map(|index| index.to_string())
            .collect()
    };
    let cache_hits = AtomicUsize::new(0);
    let preflight_checked = AtomicUsize::new(0);
    let work = cache
        .as_ref()
        .map_or_else(|| scratch.path().join("work"), replay::Cache::work);
    if work.exists() {
        // Only the run holding the cache lock can own this directory.
        fs::remove_dir_all(&work).context("clear leftover corpus work")?;
    }
    fs::create_dir(&work).context("create corpus work directory")?;
    for key in &keys {
        let case = work.join(key);
        fs::create_dir(&case).with_context(|| format!("create corpus case {}", case.display()))?;
        fs::create_dir(case.join("out")).context("create compiler output directory")?;
        fs::create_dir(case.join("trace")).context("create compiler trace directory")?;
    }
    let details = request.details || request.strict;
    let next = AtomicUsize::new(0);
    let outcomes: Mutex<Vec<Option<Outcome>>> =
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
            ..TraceOptions::default()
        }
    } else {
        TraceOptions::default()
    };
    options.source_arenas = source_arenas;
    options.source_nodes = request.source_nodes;
    options.listing = request.trace_only.iter().any(|prefix| {
        ["st.stage", "st.listing"]
            .iter()
            .any(|record| record.starts_with(prefix))
    });
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
        // (and the temporaries' creators, which a module run in place of the oracle gives as the original's
        // addresses)
        if request.in_process.is_some() || runner::module(&request.root, &oracle).is_some() {
            streams.push(CREATORS);
        }
        streams.extend([
            "pv.function.begin",
            "pv.value.",
            "pv.counter.",
            "pv.category.",
            "pv.scalar.",
        ]);
    }
    streams.extend(
        request
            .trace_only
            .iter()
            .filter(|prefix| prefix.starts_with("pv."))
            .map(String::as_str),
    );
    let null = Path::new(if cfg!(windows) { "NUL" } else { "/dev/null" });
    let reference = request
        .trace_reference
        .as_ref()
        .map(std::path::absolute)
        .transpose()?;
    let check = |index: usize, job: &Job| -> Result<Outcome> {
        let key = &keys[index];
        let case = work.join(key);
        let output_dir = case.join("out");
        let trace_dir = case.join("trace");
        let basename = Path::new("result.o");
        let output = output_dir.join(basename);
        let trace = trace_dir.join("candidate.trace");
        let reference_trace = trace_dir.join("reference.trace");
        let dump = trace_dir.join("candidate.iro");
        let reference_dump = trace_dir.join("reference.iro");
        let invoke = |executable, trace_file, options| runner::Invocation {
            root: &request.root,
            executable,
            flags: &job.flags,
            source: &job.source,
            directory: &job.directory,
            output: &output,
            trace_file,
            options,
            timeout: request.timeout,
        };
        let prepare = || -> Result<()> {
            for entry in fs::read_dir(&output_dir).context("list compiler output directory")? {
                let entry = entry?;
                let path = entry.path();
                let clear = if entry.file_type()?.is_dir() {
                    fs::remove_dir_all(&path)
                } else {
                    fs::remove_file(&path)
                };
                clear.with_context(|| format!("clear compiler output {}", path.display()))?;
            }
            Ok(())
        };
        let defaults = TraceOptions::default();
        let run_reference = || {
            prepare()?;
            replay::Captured::collect(
                &output,
                runner::execute_process(invoke(&original, null, &defaults))?,
            )
        };
        let hit = match &cache {
            Some(cache) if !request.preflight => cache.load(key).context("read stock cache")?,
            _ => None,
        };
        let real = if let Some(hit) = hit {
            cache_hits.fetch_add(1, Ordering::Relaxed);
            hit
        } else {
            let real = run_reference().context("capture reference output")?;
            if request.preflight {
                let repeat = run_reference().context("repeat reference invocation")?;
                let outcome = compare(job, &real, &repeat, basename, true, true)?;
                ensure!(
                    outcome.problems.is_empty(),
                    "reference preflight failed: {}",
                    outcome.problems.join("; ")
                );
                preflight_checked.fetch_add(1, Ordering::Relaxed);
            }
            if let Some(cache) = &cache {
                cache
                    .store(key, &real)
                    .context("save stock result to the pending cache")?;
            }
            real
        };
        let trace_options = |path: &Path| {
            let mut options = options.clone();
            if let Some(phases) = &request.iro_phases {
                options.iro_phases = Some(phases.clone());
                options.iro_dump = Some(path.display().to_string());
            }
            options
        };
        prepare()?;
        let candidate_options = trace_options(&dump);
        let invocation = invoke(
            &oracle,
            if reference.is_some() { &trace } else { null },
            &candidate_options,
        );
        let execution = match &request.in_process {
            Some(InProcess(compile)) => compile(invocation),
            None => runner::execute(invocation),
        }
        .context("run candidate compiler")?;
        let ours =
            replay::Captured::collect(&output, execution).context("capture candidate output")?;
        let mut outcome = compare(job, &real, &ours, basename, request.strict, details)?;
        if let Some(executable) = &reference {
            prepare()?;
            let reference_options = trace_options(&reference_dump);
            let traced =
                runner::execute_process(invoke(executable, &reference_trace, &reference_options))?;
            let pick = |trace: &[u8]| {
                let trace = String::from_utf8_lossy(trace);
                let trace = without(&trace, &request.ignore);
                if request.trace_only.is_empty() {
                    return trace;
                }
                trace
                    .lines()
                    .filter(|line| {
                        request
                            .trace_only
                            .iter()
                            .any(|prefix| line.starts_with(prefix))
                    })
                    .collect::<Vec<_>>()
                    .join("\n")
            };
            let candidate_trace = pick(&ours.execution.trace);
            let stock_trace = pick(&traced.trace);
            let trace_diff = if traced.timed_out {
                Some("trace reference timed out".to_owned())
            } else if !request.trace_only.is_empty()
                && candidate_trace.is_empty()
                && stock_trace.is_empty()
            {
                Some("selected trace records missing on both sides".to_owned())
            } else {
                trace_difference_with(&candidate_trace, &stock_trace, &streams)
            };
            if let Some(difference) = trace_diff {
                outcome.problems.push(difference);
                if details {
                    outcome.categories.push("trace".into());
                }
            }
            if request.iro_phases.is_some() {
                let read_dump = |path: &Path| match fs::read_to_string(path) {
                    Ok(text) => Ok(Some(text)),
                    Err(error) if error.kind() == std::io::ErrorKind::NotFound => Ok(None),
                    Err(error) => Err(error),
                };
                if let Some(difference) =
                    compare_dump(read_dump(&dump)?, read_dump(&reference_dump)?)
                {
                    outcome.problems.push(difference);
                    if details {
                        outcome.categories.push("optimizer dump".into());
                    }
                }
            }
        }
        if !outcome.problems.is_empty()
            && let Some(keep) = &request.keep
        {
            fs::create_dir_all(keep)?;
            let safe: String = job
                .name
                .chars()
                .map(|c| {
                    if c.is_ascii_alphanumeric() || "_.-".contains(c) {
                        c
                    } else {
                        '_'
                    }
                })
                .take(100)
                .collect();
            let stem = format!("{index}-{safe}");
            for (suffix, result) in [("real", &real), ("oracle", &ours)] {
                let object = result.files.get(basename).map_or(&[][..], Vec::as_slice);
                fs::write(keep.join(format!("{stem}.{suffix}.o")), object)?;
                fs::write(
                    keep.join(format!("{stem}.{suffix}.txt")),
                    result
                        .execution
                        .diagnostics
                        .chars()
                        .map(|ch| ch as u8)
                        .collect::<Vec<_>>(),
                )?;
                for (path, data) in &result.files {
                    if path != basename {
                        let target = keep.join(format!("{stem}.{suffix}.outputs")).join(path);
                        fs::create_dir_all(target.parent().context("side output has no parent")?)?;
                        fs::write(target, data)?;
                    }
                }
            }
            for (path, suffix) in [
                (&trace, "oracle.trace"),
                (&reference_trace, "reference.trace"),
                (&dump, "oracle.iro"),
                (&reference_dump, "reference.iro"),
            ] {
                if path.is_file() {
                    fs::copy(path, keep.join(format!("{stem}.{suffix}")))?;
                }
            }
        }
        Ok(outcome)
    };
    thread::scope(|scope| {
        for _ in 0..request.parallelism.min(request.jobs.len()) {
            scope.spawn(|| {
                loop {
                    let index = next.fetch_add(1, Ordering::Relaxed);
                    let Some(job) = request.jobs.get(index) else {
                        break;
                    };
                    let outcome = check(index, job).unwrap_or_else(|error| Outcome {
                        name: job.name.clone(),
                        problems: vec![format!("harness error: {error:#}")],
                        categories: if details {
                            vec!["harness".into()]
                        } else {
                            vec![]
                        },
                        first_object_difference: None,
                    });
                    outcomes.lock().unwrap()[index] = Some(outcome);
                }
            });
        }
    });
    let outcomes = outcomes
        .into_inner()
        .unwrap()
        .into_iter()
        .map(|r| r.context("missing corpus worker result"))
        .collect::<Result<Vec<_>>>()?;
    let identical = outcomes.iter().filter(|r| r.problems.is_empty()).count();
    if let Some(manifest) = &request.manifest {
        manifest.verify(&request.root)?;
    }
    if let Some(cache) = &cache {
        cache.publish()?;
    }
    let (mut categories, mut clusters) = (BTreeMap::new(), BTreeMap::new());
    if details {
        for outcome in outcomes
            .iter()
            .filter(|outcome| !outcome.problems.is_empty())
        {
            for category in &outcome.categories {
                *categories.entry(category.clone()).or_insert(0) += 1;
            }
            *clusters.entry(outcome.categories.join(", ")).or_insert(0) += 1;
        }
    }
    let total = outcomes.len();
    Ok(Report {
        total,
        identical,
        outcomes,
        categories,
        clusters,
        cached_reference: cache.map(|_| cache_hits.into_inner()),
        preflight_checked: request.preflight.then(|| preflight_checked.into_inner()),
    })
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn strict_checks_console_bytes_and_side_outputs() {
        let capture = |diagnostics: &str, sidecar: u8| replay::Captured {
            execution: runner::Execution {
                exit_code: Some(0),
                timed_out: false,
                cancelled: false,
                diagnostics: diagnostics.into(),
                trace: vec![],
            },
            files: BTreeMap::from([
                (PathBuf::from("result.o"), vec![1]),
                (PathBuf::from("result.irobj"), vec![sidecar]),
            ]),
        };
        let reference = capture("### /stock/mwcceppc.exe\r\n", 1);
        let candidate = capture("### /source/mwcceppc.exe\n", 2);
        let job = Job {
            name: "test.c".into(),
            source: "test.c".into(),
            flags: vec![],
            directory: ".".into(),
        };
        let out = Path::new("result.o");
        assert!(
            compare(&job, &reference, &candidate, out, false, true)
                .unwrap()
                .problems
                .is_empty()
        );
        assert_eq!(
            compare(&job, &reference, &candidate, out, true, true)
                .unwrap()
                .categories,
            ["diagnostics", "side outputs"]
        );
    }

    #[test]
    fn reports_optimizer_context_and_never_ignores_missing_dump() {
        let header = "Dumping function f after first pass";
        let a = format!("{header}\n1");
        let b = format!("{header}\n2");
        assert!(compare_dump(Some(a), Some(b)).unwrap().contains(header));
        assert!(compare_dump(None, Some(String::new())).is_some());
    }
}
