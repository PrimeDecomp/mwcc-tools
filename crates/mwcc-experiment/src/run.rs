//! A plan's run: its candidates rendered, the unmodified control compiled first (and compared with the build's
//! object), each candidate evaluated through the stages, in parallel, with one flushed `results.jsonl` row as each
//! finishes, the control repeated at the end, and `coverage.json`. The project's files are never written: every
//! compile reads its candidate's files through the overlay.
use crate::{
    Timing,
    plan::{Files, Plan, Rendered, ScoreStage, Stage},
};
use anyhow::{Context as _, Result, bail};
use mwcc_compare::{FunctionScore, ScoreOptions, UnitScore, score::Target};
use mwcc_project::{
    CompileCache, CompileOptions, Compiled, Overlay, Persist, PersistPool, PoolStats, Unit,
    compile_unit,
};
use serde::{Deserialize, Serialize};
use serde_json::{Value, json};
use std::{
    collections::{BTreeMap, BTreeSet},
    fs,
    io::Write,
    path::{Path, PathBuf},
    sync::{
        Mutex,
        atomic::{AtomicBool, AtomicUsize, Ordering},
    },
    time::{Duration, Instant},
};

/// What a run needs beyond its plan.
pub struct Context<'a> {
    pub unit: &'a Unit,
    /// The unit's target (a score stage needs it).
    pub target: Option<&'a Target>,
    /// The scoring options of the unit (objdiff.json's).
    pub score_options: ScoreOptions,
    /// The checkout of mwcc whose modules run the compiler.
    pub oracle_root: &'a Path,
    pub cancel: &'a (dyn Fn() -> bool + Sync),
    /// The compile cache candidates may be answered from (never the repeated control).
    pub cache: Option<&'a CompileCache>,
    /// The persistent compilers candidates may be compiled by (never the control), each cut before its first
    /// difference from the control's source.
    pub pool: Option<&'a PersistPool>,
}

#[derive(Clone, Copy, Debug, Serialize, Deserialize, PartialEq, Eq, PartialOrd, Ord)]
#[serde(rename_all = "snake_case")]
pub enum Status {
    Ok,
    CompileError,
    EvaluationError,
}

impl Status {
    fn label(self) -> &'static str {
        match self {
            Self::Ok => "ok",
            Self::CompileError => "compile_error",
            Self::EvaluationError => "evaluation_error",
        }
    }
}

/// decomp_experiment's `score_symbols` entry of a function.
#[derive(Clone, Debug, Serialize, Deserialize, PartialEq)]
pub struct SymbolScore {
    pub percent: Option<f32>,
    pub original: usize,
    pub emitted: usize,
    pub differing: usize,
    pub exact_instructions: bool,
    /// The strict level's verdict (with the scores' `strict`): `exact`, or its most fundamental difference (`size`,
    /// `code`, `reloc-layout`, `target-name`, `unresolved`, `literal`).
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub strict: Option<String>,
}

/// One finished candidate (a `results.jsonl` line).
#[derive(Clone, Debug, Serialize, Deserialize)]
pub struct Row {
    pub name: String,
    pub directory: PathBuf,
    pub status: Status,
    /// A `trace.realize` variant: the model edit it realizes.
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub suggested: Option<String>,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub error: Option<String>,
    /// The candidate whose files and flags it has (it was not evaluated again: its row is that one's).
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub duplicate_of: Option<String>,
    /// Each of the plan's symbols.
    pub scores: BTreeMap<String, SymbolScore>,
    /// With the plan's `functions`, every target function's match (none: unpaired), by its name.
    #[serde(default, skip_serializing_if = "BTreeMap::is_empty")]
    pub functions: BTreeMap<String, Option<f32>>,
    /// The unit's fuzzy match.
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub fuzzy: Option<f64>,
    /// The stage that stopped it.
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub pruned: Option<String>,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub object_sha1: Option<String>,
    /// The compile cache answered its compile.
    #[serde(default, skip_serializing_if = "std::ops::Not::not")]
    pub cached: bool,
    /// A persistent compiler compiled it (after the control's context, parsed once).
    #[serde(default, skip_serializing_if = "std::ops::Not::not")]
    pub fast: bool,
    pub timing: Timing,
}

/// The unmodified control: compiled first and last.
#[derive(Clone, Debug, Default, Serialize)]
pub struct Control {
    pub status: Option<Status>,
    /// Its object equals the build's (only meaningful without baseline overrides).
    pub identical_to_build: Option<bool>,
    /// The repeat at the end gave the same object.
    pub deterministic: Option<bool>,
    pub fuzzy: Option<f64>,
    pub scores: BTreeMap<String, SymbolScore>,
    #[serde(skip_serializing_if = "BTreeMap::is_empty")]
    pub functions: BTreeMap<String, Option<f32>>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub console: Option<String>,
}

/// The persistent compilers' counters over a run (the pool's, which other runs may share).
#[derive(Clone, Debug, Default, Serialize)]
pub struct Persistent {
    /// Candidates they compiled (failures they reported among them).
    pub fast: u64,
    pub trusted_failures: u64,
    /// Their failures compiled normally, and of those, the ones the normal compile accepted (each a persistent
    /// compiler dropped: there should be none).
    pub rechecked: u64,
    pub mismatches: u64,
    /// With `MWCC_PERSIST_VERIFY`, candidates also compiled normally and compared.
    pub verified: u64,
    /// Candidates compiled normally, as none of their context was ready.
    pub not_ready: u64,
    pub starts: u64,
    pub start_failures: u64,
}

impl Persistent {
    fn between(before: &PoolStats, after: &PoolStats) -> Self {
        Self {
            fast: after.fast - before.fast,
            trusted_failures: after.trusted_failures - before.trusted_failures,
            rechecked: after.rechecked - before.rechecked,
            mismatches: after.mismatches - before.mismatches,
            verified: after.verified - before.verified,
            not_ready: after.not_ready - before.not_ready,
            starts: after.starts - before.starts,
            start_failures: after.start_failures - before.start_failures,
        }
    }
}

#[derive(Clone, Debug, Serialize)]
pub struct Summary {
    pub output: PathBuf,
    pub candidates: usize,
    pub evaluated: usize,
    /// Rows a resumed run found already written.
    pub resumed: usize,
    pub statuses: BTreeMap<String, usize>,
    /// Candidates with an earlier one's files and flags (not evaluated again).
    pub duplicates: usize,
    /// Candidates the compile cache answered, and the scores the target had kept (an equal object's).
    pub compile_hits: usize,
    pub score_hits: usize,
    /// What the persistent compilers did meanwhile (with a pool).
    #[serde(skip_serializing_if = "Option::is_none")]
    pub persistent: Option<Persistent>,
    pub control: Control,
    /// Why it stopped early, if it did (a budget, a cancel).
    #[serde(skip_serializing_if = "Option::is_none")]
    pub stopped: Option<String>,
    pub timing: Timing,
    /// (compile + score + model) / (wall time * jobs): the share of the workers' time spent working.
    pub productive: f64,
    pub jobs: usize,
    pub wall_ms: f64,
}

fn ms(duration: Duration) -> f64 {
    duration.as_secs_f64() * 1e3
}

fn sha1(bytes: &[u8]) -> String {
    use sha1::{Digest, Sha1};
    format!("{:x}", Sha1::digest(bytes))
}

/// decomp_common's `find_symbol` over a score: an exact name (mangled or demangled), else an exact short method
/// name, else a case-insensitive substring; more than one is an error.
pub fn find_function<'a>(score: &'a UnitScore, query: &str) -> Result<Option<&'a FunctionScore>> {
    let names = |f: &'a FunctionScore| {
        [Some(f.name.as_str()), f.demangled.as_deref()]
            .into_iter()
            .flatten()
    };
    let exact: Vec<_> = score
        .functions
        .iter()
        .filter(|f| names(f).any(|n| n == query))
        .collect();
    let short: Vec<_> = score
        .functions
        .iter()
        .filter(|f| {
            names(f).any(|n| n.split('(').next().unwrap_or(n).rsplit("::").next() == Some(query))
        })
        .collect();
    let lower = query.to_lowercase();
    let found = if !exact.is_empty() {
        exact
    } else if !short.is_empty() {
        short
    } else {
        score
            .functions
            .iter()
            .filter(|f| names(f).any(|n| n.to_lowercase().contains(&lower)))
            .collect()
    };
    if found.len() > 1 {
        let listed: Vec<&str> = found.iter().map(|f| f.name.as_str()).collect();
        bail!(
            "ambiguous symbol {query:?}; use an exact mangled name: {}",
            listed.join(", ")
        );
    }
    Ok(found.first().copied())
}

/// The entries of SYMBOLS (each found as `find_function` finds it), as decomp_experiment's `score_symbols` gives
/// them.
pub fn symbol_scores(
    score: &UnitScore,
    symbols: &[String],
) -> Result<BTreeMap<String, SymbolScore>> {
    let mut out = BTreeMap::new();
    for query in symbols {
        let found = find_function(score, query)?
            .with_context(|| format!("missing comparison symbol: {query}"))?;
        out.insert(
            query.clone(),
            SymbolScore {
                percent: found.match_percent,
                original: found.stats.original,
                emitted: found.stats.emitted,
                differing: found.stats.differing,
                exact_instructions: found.stats.exact_instructions,
                strict: found.strict.as_ref().map(|s| s.class.label().to_string()),
            },
        );
    }
    Ok(out)
}

/// Every target function's match, by name.
fn function_matches(score: &UnitScore) -> BTreeMap<String, Option<f32>> {
    score
        .functions
        .iter()
        .map(|f| (f.name.clone(), f.match_percent))
        .collect()
}

/// The project's path of FILE, a plan's (relative to the project).
fn project_path(unit: &Unit, file: &str) -> PathBuf {
    unit.directory
        .join(file.replace(['\\', '/'], std::path::MAIN_SEPARATOR_STR))
}

fn write_files(directory: &Path, files: &Files) -> Result<()> {
    for (path, text) in files {
        if let Some(text) = text {
            let at = directory.join(path);
            if let Some(parent) = at.parent() {
                fs::create_dir_all(parent)?;
            }
            fs::write(&at, text)?;
        }
    }
    Ok(())
}

/// The directory of candidate INDEX, with its files and `variant.json` (and its original, for a duplicate).
fn candidate_directory(
    out: &Path,
    index: usize,
    candidate: &Rendered,
    duplicate_of: Option<&str>,
) -> Result<PathBuf> {
    let directory = out.join(format!("{index:04}"));
    fs::create_dir_all(&directory)?;
    write_files(&directory, &candidate.files)?;
    let absent: Vec<&String> = candidate
        .files
        .iter()
        .filter(|(_, t)| t.is_none())
        .map(|(p, _)| p)
        .collect();
    let mut variant = json!({"name": candidate.name, "absent": absent, "inline_max_size": candidate.inline_max_size});
    if let Some(original) = duplicate_of {
        variant["duplicate_of"] = json!(original);
    }
    fs::write(directory.join("variant.json"), variant.to_string() + "\n")?;
    Ok(directory)
}

/// What one compile and score of FILES gave.
struct Evaluated {
    compiled: Compiled,
    score: Option<UnitScore>,
    timing: Timing,
}

fn evaluate(
    cx: &Context<'_>,
    plan: &Plan,
    files: &Files,
    inline_max_size: Option<u32>,
    rows: bool,
    fresh: bool,
    reference: Option<&[u8]>,
) -> Result<Evaluated> {
    let mut overlay = Overlay::new();
    for (path, text) in files {
        let at = project_path(cx.unit, path);
        match text {
            Some(text) => overlay.insert(at, text.as_bytes()),
            None => overlay.insert_absent(at),
        }
    }
    let mut timing = Timing::default();
    let compiled = compile_unit(
        cx.unit,
        &overlay,
        &CompileOptions {
            oracle_root: cx.oracle_root,
            inline_max_size,
            trace: None,
            timeout: Duration::from_millis(plan.timeout_ms.unwrap_or(600_000)),
            cancel: cx.cancel,
            cache: cx.cache.filter(|_| !fresh),
            persist: cx.pool.zip(reference).map(|(pool, reference)| Persist {
                pool,
                reference,
                confirm_failures: false,
            }),
        },
    )?;
    timing.compile += ms(compiled.time);
    let mut score = None;
    if compiled.exit_code == 0
        && let Some(object) = &compiled.object
        && plan.stages.iter().any(|s| matches!(s, Stage::Score(_)))
    {
        let target = cx.target.context("a score stage needs the unit's target")?;
        let started = Instant::now();
        score = Some(target.score(
            object,
            &ScoreOptions {
                rows,
                ..cx.score_options.clone()
            },
        )?);
        timing.score += ms(started.elapsed());
    }
    Ok(Evaluated {
        compiled,
        score,
        timing,
    })
}

/// Whether a score stage stops a candidate of SCORE (SCORES: its symbols').
fn pruned(
    stage: &ScoreStage,
    score: &UnitScore,
    scores: &BTreeMap<String, SymbolScore>,
    plan: &Plan,
) -> bool {
    stage
        .prune_if_fuzzy_below
        .is_some_and(|below| score.fuzzy.is_some_and(|f| f < below))
        || stage.prune_if_symbol_below.is_some_and(|below| {
            plan.symbols
                .first()
                .and_then(|s| scores.get(s))
                .is_some_and(|s| f64::from(s.percent.unwrap_or(0.0)) < below)
        })
}

/// Run PLAN for the unit of CX: each finished candidate's row given to ON_ROW as it is written.
pub fn run(plan: &Plan, cx: &Context<'_>, on_row: &(dyn Fn(&Row) + Sync)) -> Result<Summary> {
    let started = Instant::now();
    let unit = cx.unit;
    let source = unit.source.replace('\\', "/");
    let declared: Vec<String> = if plan.files.is_empty() {
        vec![source.clone()]
    } else {
        plan.files.iter().map(|f| f.replace('\\', "/")).collect()
    };
    if declared.len() != declared.iter().collect::<BTreeSet<_>>().len() {
        bail!("files must be unique");
    }
    if let Some(stray) = plan
        .baseline
        .keys()
        .find(|k| !declared.contains(&k.replace('\\', "/")))
    {
        bail!("the baseline has {stray}, which is not one of the plan's files");
    }
    let out = &plan.output;
    if out.exists() && !out.join("manifest.json").is_file() && fs::read_dir(out)?.next().is_some() {
        bail!(
            "{} exists and is not a run to resume (it has no manifest.json)",
            out.display()
        );
    }
    fs::create_dir_all(out)?;
    // (one run of a directory at a time: a resume waits for a cancelled run to finish)
    let lock = fs::File::create(out.join("run.lock"))?;
    let waited = Instant::now();
    loop {
        match lock.try_lock() {
            Ok(()) => break,
            Err(fs::TryLockError::WouldBlock) if waited.elapsed() < Duration::from_secs(60) => {
                std::thread::sleep(Duration::from_millis(20))
            }
            Err(fs::TryLockError::WouldBlock) => {
                bail!("{} is being run by another run", out.display())
            }
            Err(fs::TryLockError::Error(e)) => return Err(e).context("lock the run's directory"),
        }
    }
    let resuming = out.join("manifest.json").is_file();
    // (the files at entry, and the baseline over them)
    let mut original = Files::new();
    for file in &declared {
        let path = project_path(unit, file);
        let text = match fs::read(&path) {
            Ok(bytes) => {
                Some(String::from_utf8(bytes).with_context(|| format!("{file} is not UTF-8"))?)
            }
            Err(e) if e.kind() == std::io::ErrorKind::NotFound => None,
            Err(e) => return Err(e).with_context(|| format!("read {}", path.display())),
        };
        original.insert(file.clone(), text);
    }
    let mut baseline = original.clone();
    for (file, text) in &plan.baseline {
        baseline.insert(file.replace('\\', "/"), text.clone());
    }
    let candidates: Vec<Rendered> = {
        let mut list: Vec<Rendered> = plan
            .space
            .candidates(&source)?
            .iter()
            .map(|c| c.render(&baseline))
            .collect();
        if let Some(limit) = plan.budget.candidates {
            list.truncate(limit);
        }
        list
    };
    let mut names = BTreeSet::new();
    if let Some(duplicate) = candidates.iter().find(|c| !names.insert(c.name.as_str())) {
        bail!("two candidates are named {:?}", duplicate.name);
    }
    // (what a resumed run must share with the one it resumes: all but where it is, its budget and its jobs)
    let comparable = |plan: &Value| {
        let mut plan = plan.clone();
        if let Some(map) = plan.as_object_mut() {
            for key in ["output", "budget", "jobs"] {
                map.remove(key);
            }
        }
        plan
    };
    if resuming {
        let manifest: Value = serde_json::from_str(&fs::read_to_string(out.join("manifest.json"))?)
            .context("the manifest of the run to resume is not JSON")?;
        if comparable(&manifest["plan"]) != comparable(&serde_json::to_value(plan)?) {
            bail!(
                "{} is a run of another plan: only its output, budget and jobs may change on a resume",
                out.display()
            );
        }
    } else {
        write_files(&out.join("original"), &original)?;
        write_files(&out.join("baseline"), &baseline)?;
        let manifest = json!({
            "files": declared,
            "absent_original": original.iter().filter(|(_, t)| t.is_none()).map(|(p, _)| p).collect::<Vec<_>>(),
            "root": unit.directory,
            "unit": unit.name,
            "symbols": plan.symbols,
            "argv": std::iter::once(unit.compiler.display().to_string())
                .chain(unit.arguments(&unit.source, "OBJECT", None))
                .collect::<Vec<_>>(),
            "plan": plan,
        });
        fs::write(
            out.join("manifest.json"),
            serde_json::to_string_pretty(&manifest)? + "\n",
        )?;
    }
    // (the rows a resumed run has: a duplicate's original may be among them)
    let written: BTreeMap<String, Row> = fs::read_to_string(out.join("results.jsonl"))
        .unwrap_or_default()
        .lines()
        .filter_map(|l| serde_json::from_str::<Row>(l).ok())
        .map(|row| (row.name.clone(), row))
        .collect();
    let done: BTreeSet<String> = written.keys().cloned().collect();
    // (each candidate with an earlier one's files and flags, and the earlier one's duplicates)
    let mut first_of: BTreeMap<(&Files, Option<u32>), usize> = BTreeMap::new();
    let mut duplicate_of: Vec<Option<usize>> = vec![None; candidates.len()];
    let mut duplicates: BTreeMap<usize, Vec<usize>> = BTreeMap::new();
    for (index, candidate) in candidates.iter().enumerate() {
        if candidate.error.is_some() {
            continue;
        }
        let first = *first_of
            .entry((&candidate.files, candidate.inline_max_size))
            .or_insert(index);
        if first != index {
            duplicate_of[index] = Some(first);
            duplicates.entry(first).or_default().push(index);
        }
    }
    let mut timing = Timing::default();
    let score_hits_before = cx.target.map_or(0, |t| t.score_hits());
    // (the control: the baseline as it is)
    let mut control = Control::default();
    let first = evaluate(cx, plan, &baseline, None, false, false, None)?;
    // (what the candidates' sources are cut against: the control's)
    let reference: Option<Vec<u8>> = match baseline.get(&source) {
        _ if cx.pool.is_none() => None,
        Some(text) => text.as_ref().map(|t| t.as_bytes().to_vec()),
        None => fs::read(unit.source_path()).ok(),
    };
    let pool_before = cx.pool.map(PersistPool::stats);
    timing.add(&first.timing);
    let built = fs::read(&unit.base_path).ok();
    control.status = Some(
        if first.compiled.object.is_some() && first.compiled.exit_code == 0 {
            Status::Ok
        } else {
            Status::CompileError
        },
    );
    if control.status != Some(Status::Ok) {
        control.console = Some(String::from_utf8_lossy(&first.compiled.console).into_owned());
    }
    // (only the unmodified files can equal the build's object)
    control.identical_to_build = match (&first.compiled.object, &built) {
        (Some(a), Some(b)) if baseline == original => Some(a == b),
        _ => None,
    };
    if plan.keep_objects
        && let Some(object) = &first.compiled.object
    {
        fs::create_dir_all(out.join("control"))?;
        fs::write(out.join("control").join("object.o"), object)?;
    }
    if let Some(score) = &first.score {
        control.fuzzy = score.fuzzy;
        control.scores = symbol_scores(score, &plan.symbols)?;
        if plan.functions {
            control.functions = function_matches(score);
        }
    }
    let results = Mutex::new(
        fs::OpenOptions::new()
            .create(true)
            .append(true)
            .open(out.join("results.jsonl"))?,
    );
    let next = AtomicUsize::new(0);
    let compiles = AtomicUsize::new(0);
    let stop = AtomicBool::new(false);
    let stopped: Mutex<Option<String>> = Mutex::new(None);
    // (the rows' timing, their statuses, how many, how many the compile cache answered, and the duplicates)
    let totals = Mutex::new((
        Timing::default(),
        BTreeMap::<String, usize>::new(),
        0usize,
        0usize,
        0usize,
    ));
    let jobs = plan
        .jobs
        .unwrap_or_else(|| std::thread::available_parallelism().map_or(4, |n| n.get().min(8)))
        .max(1);
    let halt = |why: String| {
        stop.store(true, Ordering::Relaxed);
        stopped.lock().unwrap().get_or_insert(why);
    };
    let write_row = |row: &Row| -> Result<()> {
        {
            let mut file = results.lock().unwrap();
            writeln!(file, "{}", serde_json::to_string(row)?)?;
            file.flush()?;
        }
        {
            let mut totals = totals.lock().unwrap();
            totals.0.add(&row.timing);
            *totals.1.entry(row.status.label().into()).or_default() += 1;
            totals.2 += 1;
            totals.3 += usize::from(row.cached);
            totals.4 += usize::from(row.duplicate_of.is_some());
        }
        on_row(row);
        Ok(())
    };
    // (the rows of the duplicates of candidate INDEX, whose row is ORIGINAL: its own, as it was evaluated once)
    let write_duplicates = |index: usize, original: &Row| -> Result<()> {
        for &other in duplicates.get(&index).into_iter().flatten() {
            let candidate = &candidates[other];
            if done.contains(&candidate.name) {
                continue;
            }
            let directory = candidate_directory(out, other, candidate, Some(&original.name))?;
            write_row(&Row {
                name: candidate.name.clone(),
                directory,
                suggested: candidate.suggested.clone(),
                duplicate_of: Some(original.name.clone()),
                cached: false,
                timing: Timing::default(),
                ..original.clone()
            })?;
        }
        Ok(())
    };
    let worker = || -> Result<()> {
        loop {
            if stop.load(Ordering::Relaxed) {
                return Ok(());
            }
            if (cx.cancel)() {
                halt("cancelled".into());
                return Ok(());
            }
            if plan.budget.cancel_file.as_ref().is_some_and(|f| f.exists()) {
                halt("the cancel file exists".into());
                return Ok(());
            }
            if plan
                .budget
                .seconds
                .is_some_and(|s| started.elapsed().as_secs_f64() >= s)
            {
                halt("the seconds budget is spent".into());
                return Ok(());
            }
            let index = next.fetch_add(1, Ordering::Relaxed);
            let Some(candidate) = candidates.get(index) else {
                return Ok(());
            };
            // (a duplicate's row is written with its original's)
            if done.contains(&candidate.name) || duplicate_of[index].is_some() {
                continue;
            }
            // (a candidate that cannot be rendered is not compiled, and does not count)
            if candidate.error.is_none()
                && plan
                    .budget
                    .compiles
                    .is_some_and(|limit| compiles.fetch_add(1, Ordering::Relaxed) >= limit)
            {
                halt("the compiles budget is spent".into());
                return Ok(());
            }
            let directory = candidate_directory(out, index, candidate, None)?;
            let candidate_started = Instant::now();
            let mut row = Row {
                name: candidate.name.clone(),
                directory: directory.clone(),
                status: Status::Ok,
                suggested: candidate.suggested.clone(),
                error: None,
                duplicate_of: None,
                scores: BTreeMap::new(),
                functions: BTreeMap::new(),
                fuzzy: None,
                pruned: None,
                object_sha1: None,
                cached: false,
                fast: false,
                timing: Timing::default(),
            };
            if let Some(error) = &candidate.error {
                row.status = Status::EvaluationError;
                row.error = Some(error.clone());
            } else {
                match evaluate(
                    cx,
                    plan,
                    &candidate.files,
                    candidate.inline_max_size,
                    plan.diff,
                    false,
                    reference.as_deref(),
                ) {
                    Err(e) => {
                        row.status = Status::EvaluationError;
                        row.error = Some(format!("{e:#}").chars().take(240).collect());
                    }
                    // (a cancelled candidate has no row: a resume evaluates it)
                    Ok(evaluated) if evaluated.compiled.cancelled => {
                        halt("cancelled".into());
                        return Ok(());
                    }
                    Ok(evaluated) => {
                        fs::write(directory.join("build.log"), &evaluated.compiled.console)?;
                        if evaluated.compiled.timed_out {
                            row.error = Some("the compile timed out".into());
                        }
                        row.timing = evaluated.timing;
                        row.fast = evaluated.compiled.fast;
                        row.cached = evaluated.compiled.cached;
                        match &evaluated.compiled.object {
                            Some(object) if evaluated.compiled.exit_code == 0 => {
                                row.object_sha1 = Some(sha1(object));
                                if plan.keep_objects {
                                    fs::write(directory.join("object.o"), object)?;
                                }
                            }
                            _ => row.status = Status::CompileError,
                        }
                        if let Some(score) = &evaluated.score {
                            match symbol_scores(score, &plan.symbols) {
                                Ok(scores) => {
                                    row.fuzzy = score.fuzzy;
                                    if plan.functions {
                                        row.functions = function_matches(score);
                                    }
                                    let stage = plan.stages.iter().find_map(|s| match s {
                                        Stage::Score(stage) => Some(stage),
                                        _ => None,
                                    });
                                    if stage
                                        .is_some_and(|stage| pruned(stage, score, &scores, plan))
                                    {
                                        row.pruned = Some("score".into());
                                    }
                                    row.scores = scores;
                                }
                                Err(e) => {
                                    row.status = Status::EvaluationError;
                                    row.error = Some(format!("{e:#}"));
                                }
                            }
                            if plan.diff {
                                fs::write(
                                    directory.join("diff.json"),
                                    serde_json::to_string(score)?,
                                )?;
                            }
                        }
                    }
                }
            }
            row.timing.other += (ms(candidate_started.elapsed()) - row.timing.total()).max(0.0);
            write_row(&row)?;
            write_duplicates(index, &row)?;
        }
    };
    let outcomes: Vec<Result<()>> = std::thread::scope(|scope| {
        let handles: Vec<_> = (0..jobs).map(|_| scope.spawn(worker)).collect();
        handles
            .into_iter()
            .map(|h| {
                h.join()
                    .unwrap_or_else(|_| Err(anyhow::anyhow!("a worker panicked")))
            })
            .collect()
    });
    for outcome in outcomes {
        outcome?;
    }
    // (the duplicates of the candidates a resumed run had already evaluated)
    for (index, candidate) in candidates.iter().enumerate() {
        if let Some(row) = written.get(&candidate.name)
            && row.duplicate_of.is_none()
        {
            write_duplicates(index, row)?;
        }
    }
    // (the control again: the same object, or the compiler is not deterministic here; not after a cancel)
    if !(cx.cancel)() {
        let last = evaluate(cx, plan, &baseline, None, false, true, None)?;
        timing.add(&last.timing);
        control.deterministic = match (&first.compiled.object, &last.compiled.object) {
            _ if last.compiled.cancelled || last.compiled.timed_out => None,
            (Some(a), Some(b)) => Some(a == b),
            (None, None) => None,
            _ => Some(false),
        };
    }
    let (row_timing, statuses, evaluated, compile_hits, duplicates) = totals.into_inner().unwrap();
    timing.add(&row_timing);
    let wall = ms(started.elapsed());
    let summary = Summary {
        output: out.clone(),
        candidates: candidates.len(),
        evaluated,
        resumed: candidates.iter().filter(|c| done.contains(&c.name)).count(),
        statuses,
        duplicates,
        compile_hits,
        score_hits: cx.target.map_or(0, |t| t.score_hits()) - score_hits_before,
        persistent: cx
            .pool
            .zip(pool_before)
            .map(|(pool, before)| Persistent::between(&before, &pool.stats())),
        control,
        stopped: stopped.into_inner().unwrap(),
        // (the workers' busy share: JOBS of them ran for the wall time)
        productive: if wall > 0.0 {
            (timing.compile + timing.score + timing.model) / (wall * jobs as f64)
        } else {
            0.0
        },
        timing,
        jobs,
        wall_ms: wall,
    };
    fs::write(
        out.join("coverage.json"),
        serde_json::to_string_pretty(&summary)? + "\n",
    )?;
    Ok(summary)
}
