//! The methods on a unit's inlining and its translation unit's layout, from traced compiles: `inline.calibrate`
//! (the oracle's inline costs of a callee's instances against which the target inlined), `inline.diff` (the inline
//! decisions of two compiles, or the mispredicted ones against the target), `inline.window` (the inline_max_size
//! bounds the target's calls imply), `tu.data_diff` (the data sections' layout against the target's) and
//! `tu.header_impact` (what a header change does to the units that include it).
use crate::{
    Call, Method, Timing,
    oracle::{Traced, trace_unit},
    unit::{Loaded, UnitRef, resolve},
};
use anyhow::{Context, Result, bail, ensure};
use mwcc_oracle_core::{
    SCHEMA_VERSION, data_diff, document, header_impact, inline_calibrate, inline_diff,
    inline_window, project, runner::TraceOptions, trace,
};
use schemars::JsonSchema;
use serde::{Deserialize, Serialize};
use serde_json::{Value, json};
use std::{
    collections::{BTreeMap, BTreeSet},
    path::{Path, PathBuf},
    process::Command,
    sync::{
        Arc, Mutex,
        atomic::{AtomicUsize, Ordering},
    },
    time::Duration,
};

const TIMEOUT: Duration = Duration::from_secs(600);

/// A unit and the files in place of the disk's.
#[derive(Clone, Debug, Default, Deserialize, JsonSchema)]
#[serde(deny_unknown_fields)]
pub struct Side {
    pub handle: Option<String>,
    pub project: Option<PathBuf>,
    pub unit: Option<String>,
    #[serde(default)]
    pub files: BTreeMap<String, Option<String>>,
    pub inline_max_size: Option<u32>,
}

impl Side {
    fn which(&self) -> UnitRef {
        UnitRef {
            handle: self.handle.clone(),
            project: self.project.clone(),
            unit: self.unit.clone(),
        }
    }
}

/// A unit that could not be compiled, and why.
#[derive(Debug, Serialize, JsonSchema)]
pub struct Failure {
    pub unit: String,
    pub error: String,
}

/// LOADED's traced compile through FILES with OPTIONS, and its document.
fn documented(
    call: &Call<'_>,
    timing: &mut Timing,
    loaded: &Loaded,
    files: &BTreeMap<String, Option<String>>,
    inline_max_size: Option<u32>,
    options: &TraceOptions,
) -> Result<(Traced, Value)> {
    let traced = trace_unit(
        call,
        timing,
        loaded,
        files,
        inline_max_size,
        options,
        TIMEOUT,
    )?;
    let parsed = trace::parse(&traced.trace)?;
    let document = document::build(
        &parsed,
        traced.version,
        &traced.source,
        &traced.oracle,
        traced.identical,
        &traced.differences,
        &Default::default(),
    )?;
    Ok((traced, document))
}

/// The project's target build directory (`build/<version>`), or TARGET.
fn target_directory(loaded: &Loaded, target: Option<&Path>) -> Result<PathBuf> {
    match target {
        Some(target) => Ok(target.to_owned()),
        None => project::target_directory(&loaded.project.root.join("build.ninja")),
    }
}

/// LOADED's target asm: beside its target object, else found from its source under DIRECTORY.
fn own_asm(loaded: &Loaded, directory: &Path) -> Result<PathBuf> {
    match loaded.unit.target_asm_or_disassembled() {
        Some(asm) => Ok(asm),
        None => project::target_asm(directory, &loaded.unit.source)?.with_context(|| {
            format!(
                "no target asm for {} under {}",
                loaded.unit.source,
                directory.display()
            )
        }),
    }
}

/// RUN on each of ITEMS, JOBS at once, in order; stops at a cancel.
fn each<T: Sync, R: Send>(
    call: &Call<'_>,
    items: &[T],
    jobs: usize,
    run: impl Fn(&T, &mut Timing) -> R + Sync,
) -> Result<(Vec<R>, Timing)> {
    let next = AtomicUsize::new(0);
    let results: Mutex<Vec<(usize, R)>> = Mutex::default();
    let timing = Mutex::new(Timing::default());
    std::thread::scope(|scope| {
        for _ in 0..jobs.clamp(1, items.len().max(1)) {
            scope.spawn(|| {
                loop {
                    let index = next.fetch_add(1, Ordering::Relaxed);
                    let Some(item) = items.get(index) else { break };
                    if (call.cancel)() {
                        break;
                    }
                    let mut own = Timing::default();
                    let result = run(item, &mut own);
                    timing.lock().unwrap().add(&own);
                    results.lock().unwrap().push((index, result));
                }
            });
        }
    });
    if (call.cancel)() {
        bail!("cancelled");
    }
    let mut results = results.into_inner().unwrap();
    results.sort_by_key(|(index, _)| *index);
    Ok((
        results.into_iter().map(|(_, r)| r).collect(),
        timing.into_inner().unwrap(),
    ))
}

#[derive(Debug, Default, Deserialize, JsonSchema)]
#[serde(deny_unknown_fields)]
pub struct CalibrateParams {
    pub project: Option<PathBuf>,
    /// The callee (its name before the template arguments, e.g. `rstl::vector`).
    pub callee: String,
    /// The units to compile (default: those with an instance of the callee the target has and we lack; with
    /// `binary`, every unit).
    #[serde(default)]
    pub units: Vec<String>,
    /// Files in place of the disk's, in every unit.
    #[serde(default)]
    pub files: BTreeMap<String, Option<String>>,
    /// How close to the limit a decision counts as near (default 3).
    pub near: Option<i64>,
    /// Across the binary: each instance, whether the target has it, and our costs of it (config/<version>'s
    /// symbols).
    #[serde(default)]
    pub binary: bool,
    /// Each row's cost terms in the text.
    #[serde(default)]
    pub breakdown: bool,
    /// Units compiled at once (default 6).
    pub jobs: Option<usize>,
    /// The report's lines.
    #[serde(default)]
    pub text: bool,
}

#[derive(Debug, Serialize, JsonSchema)]
pub struct CalibrateResult {
    pub units: Vec<String>,
    /// The rows (or, with `binary`, the table by instance).
    pub rows: Value,
    /// The units whose oracle object differs from the build's.
    pub differing: Vec<String>,
    pub failed: Vec<Failure>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub text: Option<Vec<String>>,
}

/// The symbols of config/VERSION's symbols.txt and its modules'.
fn symbols(root: &Path, version: &str) -> Result<BTreeSet<String>> {
    let directory = root.join("config").join(version);
    let mut paths = vec![directory.join("symbols.txt")];
    let rels = directory.join("rels");
    if rels.is_dir() {
        let mut more = vec![];
        for entry in std::fs::read_dir(rels)? {
            let path = entry?.path().join("symbols.txt");
            if path.is_file() {
                more.push(path);
            }
        }
        more.sort();
        paths.extend(more);
    }
    let mut out = BTreeSet::new();
    for path in paths {
        let text =
            std::fs::read_to_string(&path).with_context(|| format!("read {}", path.display()))?;
        out.extend(
            text.lines()
                .map(|l| l.split(" = ").next().unwrap().trim().to_owned()),
        );
    }
    Ok(out)
}

fn calibrate(call: &mut Call<'_>, params: CalibrateParams) -> Result<CalibrateResult> {
    let project = call.session.project(params.project.as_deref())?;
    let report = project
        .report()?
        .context("inline.calibrate reads report.json, which the build has not written")?;
    let prefix = format!("{}<", params.callee);
    let mut wanted: Vec<Value> = report["units"]
        .as_array()
        .context("report.json has no units")?
        .iter()
        .filter(|u| {
            u["metadata"]["source_path"].is_string()
                && (params.units.is_empty() || params.units.iter().any(|n| u["name"] == *n))
                && (params.binary
                    || !params.units.is_empty()
                    || u["functions"].as_array().into_iter().flatten().any(|f| {
                        f.get("fuzzy_match_percent").is_none()
                            && f["name"].as_str().is_some_and(|s| s.starts_with(&prefix))
                    }))
        })
        .cloned()
        .collect();
    wanted.sort_by(|a, b| a["name"].as_str().cmp(&b["name"].as_str()));
    ensure!(
        params.binary || !wanted.is_empty(),
        "no unit has a target-only {} instance",
        params.callee
    );
    let mut loaded = vec![];
    for unit in &wanted {
        loaded.push(resolve(
            call,
            &UnitRef {
                project: params.project.clone(),
                unit: unit["name"].as_str().map(String::from),
                ..UnitRef::default()
            },
        )?);
    }
    let options = TraceOptions {
        inline_cost: true,
        ..TraceOptions::default()
    };
    let shared = &*call;
    let (results, timing) = each(shared, &loaded, params.jobs.unwrap_or(6), |l, timing| {
        documented(shared, timing, l, &params.files, None, &options).map(|(_, d)| d)
    })?;
    call.timing.add(&timing);
    let near = params.near.unwrap_or(3);
    let mut text = vec![];
    let mut documents = vec![];
    let mut differing = vec![];
    let mut failed = vec![];
    for (unit, result) in wanted.iter().zip(results) {
        let name = unit["name"].as_str().unwrap().to_owned();
        match result {
            Ok(document) => {
                if document["identical"] == false && !params.binary {
                    text.push(format!(
                        "{name}: WARNING the oracle object differs from the original compiler's"
                    ));
                    differing.push(name.clone());
                }
                documents.push((name, document));
            }
            Err(error) => {
                let error = format!("{error:#}");
                text.push(format!("{name}: oracle failed: {error}"));
                failed.push(Failure { unit: name, error });
            }
        }
    }
    let rows = if params.binary {
        let version = project::target_directory(&project.root.join("build.ninja"))?
            .file_name()
            .context("the target directory has no build identifier")?
            .to_string_lossy()
            .into_owned();
        let table = inline_calibrate::binary_table(
            &documents,
            &symbols(&project.root, &version)?,
            &params.callee,
        )?;
        text.extend(inline_calibrate::binary_report(&table, near));
        serde_json::to_value(table)?
    } else {
        let mut rows = vec![];
        for (name, document) in &documents {
            let target = wanted.iter().find(|u| u["name"] == *name).unwrap();
            rows.extend(inline_calibrate::rows(
                name,
                target,
                document,
                &params.callee,
                near,
            )?);
        }
        text.extend(inline_calibrate::report(&rows, near, params.breakdown));
        Value::Array(rows)
    };
    Ok(CalibrateResult {
        units: wanted
            .iter()
            .filter_map(|u| u["name"].as_str().map(String::from))
            .collect(),
        rows,
        differing,
        failed,
        text: params.text.then(|| {
            text.iter()
                .flat_map(|l| l.split('\n').map(String::from).collect::<Vec<_>>())
                .collect()
        }),
    })
}

#[derive(Debug, Default, Deserialize, JsonSchema)]
#[serde(deny_unknown_fields)]
pub struct InlineDiffParams {
    /// The first compile.
    pub a: Side,
    /// The second (default: A's unit as on disk). Without it and with `target`, A's decisions against the target.
    pub b: Option<Side>,
    /// Against the target's asm (its build directory: default the project's): the decisions it contradicts.
    #[serde(default)]
    pub target: bool,
    pub target_directory: Option<PathBuf>,
    /// Only this callee's decisions.
    pub callee: Option<String>,
    /// Every callee, not only the ones decided differently.
    #[serde(default)]
    pub all: bool,
    #[serde(default)]
    pub text: bool,
}

#[derive(Debug, Serialize, JsonSchema)]
pub struct InlineDiffResult {
    /// The comparison: `a`, `b`, and `callees` (each with its link and both sides' summaries).
    pub diff: Value,
    /// The target asm a side has none of.
    pub warnings: Vec<String>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub text: Option<Vec<String>>,
}

fn inline_diff_method(call: &mut Call<'_>, params: InlineDiffParams) -> Result<InlineDiffResult> {
    let sides = [
        params.a.clone(),
        params.b.clone().unwrap_or_else(|| Side {
            files: BTreeMap::new(),
            inline_max_size: None,
            ..params.a.clone()
        }),
    ];
    let loaded = [
        resolve(call, &sides[0].which())?,
        resolve(call, &sides[1].which())?,
    ];
    let options = TraceOptions {
        full_cost: true,
        instantiation: params.target,
        ..TraceOptions::default()
    };
    let mut timing = Timing::default();
    let mut documents = vec![];
    for (side, loaded) in sides.iter().zip(&loaded) {
        documents.push(
            documented(
                call,
                &mut timing,
                loaded,
                &side.files,
                side.inline_max_size,
                &options,
            )?
            .1,
        );
    }
    call.timing.add(&timing);
    let documents = [documents.remove(0), documents.remove(0)];
    let mut targets = [None, None];
    let mut warnings = vec![];
    if params.target {
        for i in 0..2 {
            let directory = target_directory(&loaded[i], params.target_directory.as_deref())?;
            match own_asm(&loaded[i], &directory) {
                Ok(asm) => {
                    targets[i] = Some(inline_diff::target_functions_at(&directory, &asm)?);
                }
                Err(error) => warnings.push(format!("{error:#}")),
            }
        }
    }
    let out = inline_diff::compare(
        [&documents[0], &documents[1]],
        [targets[0].as_ref(), targets[1].as_ref()],
        params.callee.as_deref(),
        params.all,
        params.target,
    );
    let text = params.text.then(|| {
        let decisions = documents.each_ref().map(inline_diff::decisions);
        let shared = decisions[0]
            .keys()
            .filter(|l| decisions[1].contains_key(*l))
            .count();
        let rows = out["callees"].as_array().cloned().unwrap_or_default();
        let which = if params.all {
            "all"
        } else if params.target {
            "mispredicted against the target"
        } else {
            "decided differently"
        };
        let mut text: Vec<String> = format!(
            "A: {}\nB: {}\n{shared} callees decided in both; {which}: {}",
            out["a"].as_str().unwrap_or_default(),
            out["b"].as_str().unwrap_or_default(),
            rows.len()
        )
        .lines()
        .map(String::from)
        .collect();
        for row in &rows {
            text.push(format!(
                "== {}  [{}]",
                row["callee"].as_str().unwrap_or_default(),
                row["link"].as_str().unwrap_or_default()
            ));
            text.extend(inline_diff::summary_report("A", &row["a"]));
            text.extend(inline_diff::summary_report("B", &row["b"]));
        }
        for i in 0..2 {
            if let Some(target) = &targets[i] {
                text.extend(inline_diff::emission_report(
                    if i == 0 { "A" } else { "B" },
                    &documents[i],
                    target,
                    &inline_diff::emission(&documents[i]),
                    params.callee.as_deref(),
                ));
            }
        }
        text.into_iter()
            .flat_map(|l| l.split('\n').map(String::from).collect::<Vec<_>>())
            .collect()
    });
    Ok(InlineDiffResult {
        diff: out,
        warnings,
        text,
    })
}

#[derive(Debug, Default, Deserialize, JsonSchema)]
#[serde(deny_unknown_fields)]
pub struct WindowParams {
    pub project: Option<PathBuf>,
    /// A unit (`handle` or `unit`), or several (`units`).
    pub handle: Option<String>,
    pub unit: Option<String>,
    #[serde(default)]
    pub units: Vec<String>,
    /// Files in place of the disk's, in every unit.
    #[serde(default)]
    pub files: BTreeMap<String, Option<String>>,
    pub inline_max_size: Option<u32>,
    pub target_directory: Option<PathBuf>,
    /// Every decision's bound in the text, not only the ones that bound the window.
    #[serde(default)]
    pub verbose: bool,
    pub jobs: Option<usize>,
    #[serde(default)]
    pub text: bool,
}

#[derive(Debug, Serialize, JsonSchema)]
pub struct WindowUnit {
    pub unit: String,
    pub source: String,
    /// The window: the least and the greatest inline_max_size the target's calls allow.
    pub low: Option<i64>,
    pub high: Option<i64>,
    pub bounds: Vec<Value>,
}

#[derive(Debug, Serialize, JsonSchema)]
pub struct WindowResult {
    pub schema_version: u32,
    pub units: Vec<WindowUnit>,
    /// The units with no target asm, or that failed.
    pub failed: Vec<Failure>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub text: Option<Vec<String>>,
}

/// The units a call names: `handle`, `unit` and `units`.
fn named_units(
    call: &mut Call<'_>,
    project: &Option<PathBuf>,
    handle: &Option<String>,
    unit: &Option<String>,
    units: &[String],
) -> Result<Vec<Arc<Loaded>>> {
    let mut loaded = vec![];
    if handle.is_some() {
        loaded.push(resolve(
            call,
            &UnitRef {
                handle: handle.clone(),
                ..UnitRef::default()
            },
        )?);
    }
    for name in unit.iter().chain(units) {
        loaded.push(resolve(
            call,
            &UnitRef {
                project: project.clone(),
                unit: Some(name.clone()),
                ..UnitRef::default()
            },
        )?);
    }
    ensure!(!loaded.is_empty(), "give `handle`, `unit` or `units`");
    Ok(loaded)
}

fn window(call: &mut Call<'_>, params: WindowParams) -> Result<WindowResult> {
    let loaded = named_units(
        call,
        &params.project,
        &params.handle,
        &params.unit,
        &params.units,
    )?;
    let options = TraceOptions {
        full_cost: true,
        ..TraceOptions::default()
    };
    let shared = &*call;
    let (results, timing) = each(shared, &loaded, params.jobs.unwrap_or(6), |l, timing| {
        let (traced, document) = documented(
            shared,
            timing,
            l,
            &params.files,
            params.inline_max_size,
            &options,
        )?;
        let directory = target_directory(l, params.target_directory.as_deref())?;
        let text = std::fs::read(own_asm(l, &directory)?)?;
        let rows = inline_window::bounds(
            &document,
            &inline_window::object_calls(&traced.object)?,
            &inline_window::asm_calls(&String::from_utf8_lossy(&text)),
        );
        let (low, high) = inline_window::window(&rows);
        anyhow::Ok(WindowUnit {
            unit: l.unit.name.clone(),
            source: traced.source,
            low,
            high,
            bounds: rows,
        })
    })?;
    call.timing.add(&timing);
    let mut units = vec![];
    let mut failed = vec![];
    let mut text = vec![];
    for (l, result) in loaded.iter().zip(results) {
        match result {
            Ok(unit) => {
                text.extend(inline_window::report(
                    &unit.source,
                    &unit.bounds,
                    params.verbose,
                ));
                units.push(unit);
            }
            Err(error) => {
                text.push(format!("{}: {error:#}", l.unit.name));
                failed.push(Failure {
                    unit: l.unit.name.clone(),
                    error: format!("{error:#}"),
                });
            }
        }
    }
    Ok(WindowResult {
        schema_version: SCHEMA_VERSION,
        units,
        failed,
        text: params.text.then(|| {
            text.iter()
                .flat_map(|l| l.split('\n').map(String::from).collect::<Vec<_>>())
                .collect()
        }),
    })
}

#[derive(Debug, Default, Deserialize, JsonSchema)]
#[serde(deny_unknown_fields)]
pub struct DataDiffParams {
    pub handle: Option<String>,
    pub project: Option<PathBuf>,
    pub unit: Option<String>,
    #[serde(default)]
    pub files: BTreeMap<String, Option<String>>,
    pub inline_max_size: Option<u32>,
    pub target_directory: Option<PathBuf>,
    /// Only this section (e.g. `.data`).
    pub section: Option<String>,
    /// Every item in the text, not only the differing ones.
    #[serde(default)]
    pub all: bool,
    #[serde(default)]
    pub text: bool,
}

#[derive(Debug, Serialize, JsonSchema)]
pub struct DataDiffResult {
    pub schema_version: u32,
    pub source: String,
    /// The target's asm.
    pub target: String,
    /// By section: our layout (`oracle`), the target's, and their differences.
    pub sections: serde_json::Map<String, Value>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub text: Option<Vec<String>>,
}

fn data_diff_method(call: &mut Call<'_>, params: DataDiffParams) -> Result<DataDiffResult> {
    let loaded = resolve(
        call,
        &UnitRef {
            handle: params.handle.clone(),
            project: params.project.clone(),
            unit: params.unit.clone(),
        },
    )?;
    let mut timing = Timing::default();
    let traced = trace_unit(
        call,
        &mut timing,
        &loaded,
        &params.files,
        params.inline_max_size,
        &TraceOptions {
            full_cost: true,
            ..TraceOptions::default()
        },
        TIMEOUT,
    )?;
    call.timing.add(&timing);
    let directory = target_directory(&loaded, params.target_directory.as_deref())?;
    let mut mine = data_diff::object_layout(&traced.object)?;
    let path = own_asm(&loaded, &directory)?;
    let bytes = std::fs::read(&path)?;
    let asm = String::from_utf8_lossy(&bytes);
    let mut theirs = data_diff::asm_layout(&asm)?;
    let parsed = trace::parse(&traced.trace)?;
    let module = path
        .ancestors()
        .find(|p| p.file_name().is_some_and(|n| n == "asm"))
        .context("the target's path has no asm directory")?;
    data_diff::prepare(
        &mut mine,
        &mut theirs,
        &traced.object,
        &parsed,
        &asm,
        module,
        &path,
    )?;
    let names: Vec<String> = mine
        .keys()
        .chain(theirs.keys().filter(|k| !mine.contains_key(*k)))
        .filter(|k| params.section.as_ref().is_none_or(|n| n == *k))
        .cloned()
        .collect();
    let sections = names
        .iter()
        .map(|n| {
            let a = mine.get(n);
            let b = theirs.get(n);
            let differences = a.zip(b).map(|(a, b)| {
                data_diff::compare(
                    &data_diff::linked(a["items"].as_array().unwrap()),
                    &data_diff::linked(b["items"].as_array().unwrap()),
                )
            });
            (
                n.clone(),
                json!({"oracle": a, "target": b, "differences": differences}),
            )
        })
        .collect();
    let target = project::display_path(&path.canonicalize().unwrap_or_else(|_| path.clone()));
    let text = params.text.then(|| {
        let lines: Vec<String> = names
            .iter()
            .flat_map(|n| data_diff::report(n, mine.get(n), theirs.get(n), params.all))
            .collect();
        let mut text = vec![format!("{} against {target}", traced.source)];
        if lines.is_empty() {
            text.push("the data is laid out as in the target".into());
        } else {
            text.extend(lines);
        }
        text.iter()
            .flat_map(|l| l.split('\n').map(String::from).collect::<Vec<_>>())
            .collect()
    });
    Ok(DataDiffResult {
        schema_version: SCHEMA_VERSION,
        source: traced.source.clone(),
        target,
        sections,
        text,
    })
}

#[derive(Debug, Default, Deserialize, JsonSchema)]
#[serde(deny_unknown_fields)]
pub struct HeaderImpactParams {
    pub project: Option<PathBuf>,
    /// The change: the files as they were at this git revision against the disk's, or `files` (the edited files
    /// against the disk's).
    pub git: Option<String>,
    #[serde(default)]
    pub files: BTreeMap<String, Option<String>>,
    /// Only the units whose source path has one of these in it.
    #[serde(default)]
    pub units: Vec<String>,
    pub target_directory: Option<PathBuf>,
    /// Units compiled at once (default 4).
    pub jobs: Option<usize>,
    /// Every unit in the text, not only the changed ones.
    #[serde(default)]
    pub all: bool,
    #[serde(default)]
    pub text: bool,
}

#[derive(Debug, Serialize, JsonSchema)]
pub struct ImpactUnit {
    /// The unit's source, as ninja's deps spell it.
    pub unit: String,
    /// The changed files it includes.
    pub files: Vec<String>,
    /// The functions whose code, frame or inline decisions changed (with the similarity to the target before and
    /// after), or none when it failed.
    pub functions: Option<Vec<Value>>,
    pub error: Option<String>,
}

#[derive(Debug, Serialize, JsonSchema)]
pub struct HeaderImpactResult {
    pub schema_version: u32,
    pub changed: Vec<String>,
    pub units: Vec<ImpactUnit>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub text: Option<Vec<String>>,
}

fn run_text(command: &mut Command, what: &str) -> Result<Vec<u8>> {
    let output = command.output().with_context(|| format!("run {what}"))?;
    ensure!(
        output.status.success(),
        "{what} failed: {}",
        String::from_utf8_lossy(&output.stderr)
    );
    Ok(output.stdout)
}

fn header_impact_method(
    call: &mut Call<'_>,
    params: HeaderImpactParams,
) -> Result<HeaderImpactResult> {
    ensure!(
        params.git.is_some() == params.files.is_empty(),
        "give the change: `git` (a revision), or `files`"
    );
    let project = call.session.project(params.project.as_deref())?;
    let root = project.root.clone();
    let mut before = BTreeMap::new();
    let mut changed = vec![];
    if let Some(revision) = &params.git {
        let names = run_text(
            Command::new("git")
                .args(["diff", "--name-only", "-z", revision, "--"])
                .current_dir(&root),
            "git diff",
        )?;
        for name in names.split(|b| *b == 0).filter(|s| !s.is_empty()) {
            let name = String::from_utf8(name.to_vec()).context("a git path is not UTF-8")?;
            let old = Command::new("git")
                .args(["show", &format!("{revision}:{name}")])
                .current_dir(&root)
                .output()?;
            if !old.status.success() {
                continue;
            }
            before.insert(
                name.clone(),
                Some(String::from_utf8_lossy(&old.stdout).into_owned()),
            );
            changed.push(name);
        }
    } else {
        changed.extend(params.files.keys().cloned());
    }
    let deps = run_text(
        Command::new("ninja")
            .args(["-t", "deps"])
            .current_dir(&root),
        "ninja -t deps",
    )?;
    let mut units: Vec<(String, Vec<String>)> =
        header_impact::dependents(&root, &changed, &String::from_utf8_lossy(&deps))
            .into_iter()
            .filter(|(unit, _)| {
                params.units.is_empty() || params.units.iter().any(|f| unit.contains(f))
            })
            .collect();
    units.sort_by(|a, b| a.0.cmp(&b.0));
    // Each source's unit in objdiff.json.
    let key = |p: &str| p.replace('\\', "/").trim_start_matches("./").to_lowercase();
    let mut by_source = BTreeMap::new();
    for name in project.unit_names()? {
        if let Ok(unit) = project.unit(&name) {
            by_source.insert(key(&unit.source), name);
        }
    }
    let mut loaded = vec![];
    for (source, _) in &units {
        loaded.push(match by_source.get(&key(source)) {
            Some(name) => Some(resolve(
                call,
                &UnitRef {
                    project: params.project.clone(),
                    unit: Some(name.clone()),
                    ..UnitRef::default()
                },
            )?),
            None => None,
        });
    }
    let options = TraceOptions {
        full_cost: true,
        ..TraceOptions::default()
    };
    let items: Vec<_> = units.iter().zip(&loaded).collect();
    let shared = &*call;
    let (results, timing) = each(
        shared,
        &items,
        params.jobs.unwrap_or(4),
        |((source, _), l), timing| {
            let l = l
                .as_ref()
                .with_context(|| format!("objdiff.json has no unit of {source}"))?;
            let (a, ad) = documented(shared, timing, l, &before, None, &options)?;
            let (b, bd) = documented(shared, timing, l, &params.files, None, &options)?;
            let directory = target_directory(l, params.target_directory.as_deref())?;
            let asm = own_asm(l, &directory)
                .ok()
                .map(std::fs::read)
                .transpose()?
                .map(|b| String::from_utf8_lossy(&b).into_owned());
            header_impact::compare([&a.object, &b.object], [&ad, &bd], asm.as_deref())
        },
    )?;
    call.timing.add(&timing);
    let results: Vec<ImpactUnit> = units
        .into_iter()
        .zip(results)
        .map(|((unit, files), result)| match result {
            Ok(functions) => ImpactUnit {
                unit,
                files,
                functions: Some(functions),
                error: None,
            },
            Err(error) => ImpactUnit {
                unit,
                files,
                functions: None,
                error: Some(format!("{error:#}")),
            },
        })
        .collect();
    let text = params.text.then(|| {
        let mut text = vec![format!(
            "{} changed file(s); {} unit(s) include them",
            changed.len(),
            results.len()
        )];
        let mut worse = vec![];
        let mut touched = 0;
        for result in &results {
            match (&result.error, &result.functions) {
                (Some(error), _) => text.push(format!("== {}: ERROR {error}", result.unit)),
                (None, Some(rows)) => {
                    if params.all || !rows.is_empty() {
                        text.extend(header_impact::report(&result.unit, &result.files, rows));
                    }
                    touched += usize::from(!rows.is_empty());
                    for row in rows {
                        if let (Some(a), Some(b)) =
                            (row["similarity"][0].as_f64(), row["similarity"][1].as_f64())
                            && b < a
                        {
                            worse.push(format!(
                                "   {}: {}",
                                result.unit,
                                row["function"].as_str().unwrap_or_default()
                            ));
                        }
                    }
                }
                (None, None) => {}
            }
        }
        text.push(format!(
            "{touched} of {} unit(s) changed; {} function(s) further from the target",
            results.len(),
            worse.len()
        ));
        text.extend(worse);
        text.iter()
            .flat_map(|l| l.split('\n').map(String::from).collect::<Vec<_>>())
            .collect()
    });
    Ok(HeaderImpactResult {
        schema_version: SCHEMA_VERSION,
        changed,
        units: results,
        text,
    })
}

pub(crate) fn methods() -> Vec<Method> {
    vec![
        crate::method!(
            "inline.calibrate",
            "the oracle's inline costs of a callee's instances against which of them the target inlined (the units with an instance only the target has, or `units`; with `binary`, each instance across the binary)",
            CalibrateParams,
            CalibrateResult,
            calibrate
        ),
        crate::method!(
            "inline.diff",
            "the inline decisions of two compiles (`a` and `b`, each a unit and its files), the ones decided differently, or (`target`) the ones the target's asm contradicts, with why each was decided",
            InlineDiffParams,
            InlineDiffResult,
            inline_diff_method
        ),
        crate::method!(
            "inline.window",
            "the inline_max_size window the target's calls imply for each unit: the decisions that bound it, from a full-cost trace",
            WindowParams,
            WindowResult,
            window
        ),
        crate::method!(
            "tu.data_diff",
            "the unit's data sections laid out as we compile them against the target's asm: each section's items, their order and the differences",
            DataDiffParams,
            DataDiffResult,
            data_diff_method
        ),
        crate::method!(
            "tu.header_impact",
            "what a change to headers does to the units that include them (ninja's deps): each unit compiled before and after, its functions whose code, frame or inline decisions changed, and their similarity to the target",
            HeaderImpactParams,
            HeaderImpactResult,
            header_impact_method
        ),
    ]
}
