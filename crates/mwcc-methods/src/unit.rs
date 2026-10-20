//! The unit methods: a project's unit loaded once (`unit.load`), compiled through an overlay of its files
//! (`unit.compile`), and scored against its target (`unit.score`).
use crate::{Call, Method, Session, Timing, ms};
use anyhow::{Context, Result, bail};
use mwcc_compare::{ScoreOptions, UnitScore, score::Target};
use mwcc_project::{CompileOptions, Compiled, Overlay, Project, Unit, compile_unit};
use mwcc_trace::options::TraceOptions;
use schemars::JsonSchema;
use serde::{Deserialize, Serialize};
use serde_json::Value;
use sha1::{Digest, Sha1};
use std::{
    collections::BTreeMap,
    path::PathBuf,
    sync::{Arc, Mutex},
    time::{Duration, Instant},
};

/// A unit a session has loaded, and its target once read.
pub(crate) struct Loaded {
    pub(crate) handle: String,
    pub(crate) project: Arc<Project>,
    pub(crate) unit: Unit,
    target: Mutex<Option<Arc<Target>>>,
}

/// The unit a call names: a handle `unit.load` gave, or a project (else the session's) and a unit's name.
#[derive(Clone, Debug, Default, Deserialize, JsonSchema)]
#[serde(deny_unknown_fields)]
pub struct UnitRef {
    pub handle: Option<String>,
    pub project: Option<PathBuf>,
    pub unit: Option<String>,
}

pub(crate) fn resolve(call: &mut Call<'_>, which: &UnitRef) -> Result<Arc<Loaded>> {
    let session = call.session;
    if let Some(handle) = &which.handle {
        return session
            .units
            .lock()
            .unwrap()
            .get(handle)
            .cloned()
            .with_context(|| format!("unknown unit handle {handle}"));
    }
    let name = which
        .unit
        .as_deref()
        .context("give `handle`, or `unit` (and `project`)")?;
    let started = Instant::now();
    let project = session.project(which.project.as_deref())?;
    let handle = format!("{}|{name}", project.root.display());
    if let Some(loaded) = session.units.lock().unwrap().get(&handle) {
        return Ok(loaded.clone());
    }
    let loaded = Arc::new(Loaded {
        unit: project.unit(name)?,
        handle: handle.clone(),
        project,
        target: Mutex::new(None),
    });
    session.units.lock().unwrap().insert(handle, loaded.clone());
    call.timing.load += ms(started.elapsed());
    Ok(loaded)
}

/// The scoring options objdiff.json gives the unit (its `options` and `symbol_mappings`), with the strict level.
pub(crate) fn score_options(unit: &Unit) -> ScoreOptions {
    ScoreOptions {
        strict: true,
        ..ScoreOptions::from_objdiff_unit(&unit.metadata)
    }
}

impl Loaded {
    /// The unit's target, read once, with its module's extern indexes (the strict level's).
    pub(crate) fn target(&self, session: &Session, timing: &mut Timing) -> Result<Arc<Target>> {
        let mut target = self.target.lock().unwrap();
        if let Some(target) = &*target {
            return Ok(target.clone());
        }
        let started = Instant::now();
        let path = self
            .unit
            .target_path
            .as_ref()
            .context("the unit has no target")?;
        let bytes =
            std::fs::read(path).with_context(|| format!("read the target {}", path.display()))?;
        let config = mwcc_compare::score::diff_config(&score_options(&self.unit))?;
        let externs = session.externs(&self.project, Project::module_of(&self.unit.name))?;
        let loaded = Arc::new(Target::new(&bytes, config)?.with_externs(externs));
        *target = Some(loaded.clone());
        timing.load += ms(started.elapsed());
        Ok(loaded)
    }
}

#[derive(Debug, Deserialize, JsonSchema)]
#[serde(deny_unknown_fields)]
pub struct LoadParams {
    pub handle: Option<String>,
    pub project: Option<PathBuf>,
    pub unit: Option<String>,
}

#[derive(Serialize, JsonSchema)]
pub struct LoadResult {
    /// The handle later calls name the unit by.
    pub handle: String,
    /// The unit: its source, flags, compiler, target and build object (mwcc-project's `Unit`).
    #[schemars(with = "Value")]
    pub unit: Unit,
    /// report.json's entry of the unit, if the build has written it.
    #[schemars(with = "Option<Value>")]
    pub report: Option<Value>,
}

fn load(call: &mut Call<'_>, params: LoadParams) -> Result<LoadResult> {
    let loaded = resolve(
        call,
        &UnitRef {
            handle: params.handle,
            project: params.project,
            unit: params.unit,
        },
    )?;
    Ok(LoadResult {
        handle: loaded.handle.clone(),
        unit: loaded.unit.clone(),
        report: loaded.project.report_unit(&loaded.unit.name)?,
    })
}

#[derive(Clone, Debug, Default, Deserialize, JsonSchema)]
#[serde(deny_unknown_fields)]
pub struct CompileParams {
    pub handle: Option<String>,
    pub project: Option<PathBuf>,
    pub unit: Option<String>,
    /// Files read in place of the disk's: a path (relative to the project) and its text, or null (absent).
    #[serde(default)]
    pub files: BTreeMap<String, Option<String>>,
    /// In place of the flags' last `inline_max_size(N)`.
    pub inline_max_size: Option<u32>,
    /// A traced compile, with these streams (mwcc-trace's `TraceOptions`); its trace goes to `trace_path`.
    #[schemars(with = "Option<Value>")]
    pub trace: Option<TraceOptions>,
    pub trace_path: Option<PathBuf>,
    /// Where to write the object.
    pub object_path: Option<PathBuf>,
    pub timeout_ms: Option<u64>,
    /// Whether the session's compile cache may answer (default true; never for a traced compile).
    pub cache: Option<bool>,
}

#[derive(Clone, Copy, Debug, Serialize, JsonSchema, PartialEq, Eq)]
#[serde(rename_all = "snake_case")]
pub enum Status {
    Ok,
    CompileError,
}

#[derive(Debug, Serialize, JsonSchema)]
pub struct ObjectInfo {
    pub size: usize,
    pub sha1: String,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub path: Option<PathBuf>,
}

#[derive(Debug, Serialize, JsonSchema)]
pub struct CompileResult {
    pub status: Status,
    pub exit_code: i32,
    /// The compiler's messages.
    pub console: String,
    pub in_process: bool,
    /// The session's compile cache answered it.
    pub cached: bool,
    pub timed_out: bool,
    pub cancelled: bool,
    pub object: Option<ObjectInfo>,
    /// The trace's size (a traced compile).
    pub trace_bytes: usize,
}

/// The unit's compile with PARAMS: the result, and the object.
pub(crate) fn compile_with(
    session: &Session,
    cancel: &(dyn Fn() -> bool + Sync),
    timing: &mut Timing,
    loaded: &Loaded,
    params: &CompileParams,
) -> Result<(CompileResult, Option<Vec<u8>>)> {
    let mut overlay = Overlay::new();
    for (path, text) in &params.files {
        let path = loaded.project.root.join(path);
        match text {
            Some(text) => overlay.insert(path, text.as_bytes()),
            None => overlay.insert_absent(path),
        }
    }

    let compiled: Compiled = compile_unit(
        &loaded.unit,
        &overlay,
        &CompileOptions {
            oracle_root: &session.oracle_root,
            inline_max_size: params.inline_max_size,
            trace: params.trace.as_ref(),
            timeout: Duration::from_millis(params.timeout_ms.unwrap_or(600_000)),
            cancel,
            cache: params
                .cache
                .unwrap_or(true)
                .then_some(&session.compile_cache),
            persist: None,
        },
    )?;
    timing.compile += ms(compiled.time);
    if params.trace.is_some()
        && let Some(path) = &params.trace_path
    {
        std::fs::write(path, &compiled.trace)
            .with_context(|| format!("write {}", path.display()))?;
    }
    if let (Some(path), Some(object)) = (&params.object_path, &compiled.object) {
        std::fs::write(path, object).with_context(|| format!("write {}", path.display()))?;
    }
    let object = compiled.object.as_ref().map(|bytes| ObjectInfo {
        size: bytes.len(),
        sha1: format!("{:x}", Sha1::digest(bytes)),
        path: params.object_path.clone(),
    });
    let status = if compiled.exit_code == 0 && object.is_some() {
        Status::Ok
    } else {
        Status::CompileError
    };
    Ok((
        CompileResult {
            status,
            exit_code: compiled.exit_code,
            console: String::from_utf8_lossy(&compiled.console).into_owned(),
            in_process: compiled.in_process,
            cached: compiled.cached,
            timed_out: compiled.timed_out,
            cancelled: compiled.cancelled,
            object,
            trace_bytes: compiled.trace.len(),
        },
        compiled.object,
    ))
}

fn compile(call: &mut Call<'_>, params: CompileParams) -> Result<CompileResult> {
    let loaded = resolve(
        call,
        &UnitRef {
            handle: params.handle.clone(),
            project: params.project.clone(),
            unit: params.unit.clone(),
        },
    )?;
    Ok(compile_with(
        call.session,
        call.cancel,
        &mut call.timing,
        &loaded,
        &params,
    )?
    .0)
}

#[derive(Debug, Default, Deserialize, JsonSchema)]
#[serde(deny_unknown_fields)]
pub struct ScoreParams {
    pub handle: Option<String>,
    pub project: Option<PathBuf>,
    pub unit: Option<String>,
    /// Score this object; else, with `built`, the build's; else the unit compiled (with the compile parameters).
    pub object: Option<PathBuf>,
    #[serde(default)]
    pub built: bool,
    #[serde(default)]
    pub files: BTreeMap<String, Option<String>>,
    pub inline_max_size: Option<u32>,
    pub object_path: Option<PathBuf>,
    pub timeout_ms: Option<u64>,
    /// Only these target functions (by name or demangled name).
    pub symbols: Option<Vec<String>>,
    /// Every function's rows.
    #[serde(default)]
    pub rows: bool,
    pub cache: Option<bool>,
    /// decomp_experiment's `score_symbols` entries of these functions (each by its name, demangled name or short
    /// method name, then by a unique substring), as an experiment's rows give them.
    #[serde(default)]
    pub scores: Vec<String>,
}

#[derive(Serialize, JsonSchema)]
pub struct ScoreResult {
    pub status: Status,
    /// The compile, when the unit was compiled.
    pub compile: Option<CompileResult>,
    /// mwcc-compare's `UnitScore` (absent when the compile failed).
    #[schemars(with = "Option<Value>")]
    pub score: Option<UnitScore>,
    /// The entries of `scores`, by query.
    #[serde(skip_serializing_if = "BTreeMap::is_empty")]
    #[schemars(with = "BTreeMap<String, Value>")]
    pub scores: BTreeMap<String, mwcc_experiment::run::SymbolScore>,
}

fn score(call: &mut Call<'_>, params: ScoreParams) -> Result<ScoreResult> {
    let loaded = resolve(
        call,
        &UnitRef {
            handle: params.handle.clone(),
            project: params.project.clone(),
            unit: params.unit.clone(),
        },
    )?;
    let (compile, object) = if let Some(path) = &params.object {
        (
            None,
            std::fs::read(path).with_context(|| format!("read {}", path.display()))?,
        )
    } else if params.built {
        let path = &loaded.unit.base_path;
        (
            None,
            std::fs::read(path)
                .with_context(|| format!("read the build's object {}", path.display()))?,
        )
    } else {
        let (result, object) = compile_with(
            call.session,
            call.cancel,
            &mut call.timing,
            &loaded,
            &CompileParams {
                files: params.files.clone(),
                inline_max_size: params.inline_max_size,
                object_path: params.object_path.clone(),
                timeout_ms: params.timeout_ms,
                cache: params.cache,
                ..CompileParams::default()
            },
        )?;
        match object {
            Some(object) if result.status == Status::Ok => (Some(result), object),
            _ => {
                return Ok(ScoreResult {
                    status: Status::CompileError,
                    compile: Some(result),
                    score: None,
                    scores: BTreeMap::new(),
                });
            }
        }
    };
    let target = loaded.target(call.session, &mut call.timing)?;
    let started = Instant::now();
    let scores = if params.scores.is_empty() {
        BTreeMap::new()
    } else {
        let all = target.score(&object, &score_options(&loaded.unit))?;
        mwcc_experiment::run::symbol_scores(&all, &params.scores)?
    };
    let score = target.score(
        &object,
        &ScoreOptions {
            symbols: params.symbols,
            rows: params.rows,
            ..score_options(&loaded.unit)
        },
    )?;
    call.timing.score += ms(started.elapsed());
    if (call.cancel)() {
        bail!("cancelled");
    }
    Ok(ScoreResult {
        status: Status::Ok,
        compile,
        score: Some(score),
        scores,
    })
}

pub(crate) fn methods() -> Vec<Method> {
    vec![
        crate::method!(
            "unit.load",
            "a project's unit (objdiff.json, build.ninja, report.json), loaded once: its handle, edge and report",
            LoadParams,
            LoadResult,
            load
        ),
        crate::method!(
            "unit.compile",
            "a unit compiled as its build compiles it, through files given in place of the disk's",
            CompileParams,
            CompileResult,
            compile
        ),
        crate::method!(
            "unit.score",
            "a unit's object (compiled, built or given) scored against its target: objdiff's match and rows, the kit's statistics, residual-scan's verdicts, mwdec's profile",
            ScoreParams,
            ScoreResult,
            score
        ),
    ]
}
