//! `unit.residuals`: residual-scan's triage of the functions that do not match, with its row shape: each unit's
//! source compiled in memory (or, with `built`, the object as built) and scored under the kit's policy, each
//! nonmatching function's differing rows counted by kind (register, stack, other) with its verdict, and the target
//! functions we lack (`missing`).
use crate::{
    Call, Method, Timing,
    unit::{CompileParams, Loaded, Status, UnitRef, compile_with, resolve, score_options},
};
use anyhow::{Context, Result, bail};
use schemars::JsonSchema;
use serde::{Deserialize, Serialize};
use std::{collections::BTreeMap, path::PathBuf, sync::Arc};

#[derive(Debug, Default, Deserialize, JsonSchema)]
#[serde(deny_unknown_fields)]
pub struct ResidualParams {
    pub handle: Option<String>,
    pub project: Option<PathBuf>,
    /// A unit, or several (`units`), or every unit whose fuzzy match in report.json is below `below`.
    pub unit: Option<String>,
    #[serde(default)]
    pub units: Vec<String>,
    pub below: Option<f64>,
    /// Score the objects as built (residual-scan's) in place of the sources compiled in memory.
    #[serde(default)]
    pub built: bool,
    /// Files in place of the disk's (one unit only), and its `inline_max_size`.
    #[serde(default)]
    pub files: BTreeMap<String, Option<String>>,
    pub inline_max_size: Option<u32>,
    /// Only rows of this verdict (register-only, stack-only, register+stack, structural, missing).
    pub only: Option<String>,
    /// Units scanned at once (default 8).
    pub jobs: Option<usize>,
}

/// residual-scan's row.
#[derive(Clone, Debug, Serialize, JsonSchema)]
pub struct ResidualRow {
    pub unit: String,
    pub function: String,
    pub demangled: Option<String>,
    #[serde(rename = "match")]
    pub percent: f64,
    pub size: u64,
    pub register: u32,
    pub stack: u32,
    pub other: u32,
    pub verdict: String,
}

#[derive(Debug, Serialize, JsonSchema)]
pub struct UnitFailure {
    pub unit: String,
    /// compile_error, or the error.
    pub error: String,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub console: Option<String>,
}

#[derive(Debug, Serialize, JsonSchema)]
pub struct ResidualResult {
    pub units: Vec<String>,
    /// By unit, then by match (highest first).
    pub rows: Vec<ResidualRow>,
    /// The rows of each verdict.
    pub summary: BTreeMap<String, usize>,
    /// The units that could not be scanned.
    pub failed: Vec<UnitFailure>,
}

/// A unit's rows, or why it has none.
type Scanned = Result<Vec<ResidualRow>, UnitFailure>;

const VERDICTS: &[&str] = &[
    "register-only",
    "stack-only",
    "register+stack",
    "structural",
    "missing",
];

/// PERCENT to two places as residual-scan rounds it: objdiff's JSON text of the f32, read as a double, rounded
/// correctly (Python's `round`).
fn round2(percent: f32) -> f64 {
    let read: f64 = percent.to_string().parse().unwrap_or(0.0);
    format!("{read:.2}").parse().unwrap_or(read)
}

/// The rows of one unit.
fn scan(
    call: &Call<'_>,
    loaded: &Loaded,
    params: &ResidualParams,
    timing: &mut Timing,
) -> Result<Scanned> {
    let unit = &loaded.unit;
    let object = if params.built {
        std::fs::read(&unit.base_path)
            .with_context(|| format!("read the build's object {}", unit.base_path.display()))?
    } else {
        let (result, object) = compile_with(
            call.session,
            call.cancel,
            timing,
            loaded,
            &CompileParams {
                files: params.files.clone(),
                inline_max_size: params.inline_max_size,
                ..CompileParams::default()
            },
        )?;
        match object {
            Some(object) if result.status == Status::Ok => object,
            _ => {
                return Ok(Err(UnitFailure {
                    unit: unit.name.clone(),
                    error: "compile_error".into(),
                    console: Some(result.console),
                }));
            }
        }
    };
    let target = loaded.target(call.session, timing)?;
    let started = std::time::Instant::now();
    let score = target.score(&object, &score_options(unit))?;
    timing.score += crate::ms(started.elapsed());
    Ok(Ok(score
        .functions
        .iter()
        .filter_map(|f| {
            let verdict = f.residual.verdict?;
            Some(ResidualRow {
                unit: unit.name.clone(),
                function: f.name.clone(),
                demangled: f.demangled.clone(),
                percent: round2(f.match_percent.unwrap_or(0.0)),
                size: f.size,
                register: f.residual.register,
                stack: f.residual.stack,
                other: f.residual.other,
                verdict: verdict.label().into(),
            })
        })
        .collect()))
}

fn residuals(call: &mut Call<'_>, params: ResidualParams) -> Result<ResidualResult> {
    if let Some(only) = &params.only
        && !VERDICTS.contains(&only.as_str())
    {
        bail!("only: one of {}", VERDICTS.join(", "));
    }
    let mut names: Vec<String> = params.unit.iter().chain(&params.units).cloned().collect();
    if let Some(below) = params.below {
        let project = call.session.project(params.project.as_deref())?;
        let report = project
            .report()?
            .context("--below reads report.json, which the build has not written")?;
        for unit in report["units"].as_array().into_iter().flatten() {
            let measures = &unit["measures"];
            let code = measures["total_code"]
                .as_str()
                .and_then(|s| s.parse::<u64>().ok())
                .or_else(|| measures["total_code"].as_u64())
                .unwrap_or(0);
            if code > 0 && measures["fuzzy_match_percent"].as_f64().unwrap_or(0.0) < below {
                names.extend(unit["name"].as_str().map(String::from));
            }
        }
    }
    let mut seen = std::collections::HashSet::new();
    names.retain(|n| seen.insert(n.clone()));
    let mut loaded: Vec<Arc<Loaded>> = vec![];
    if names.is_empty() && params.below.is_none() {
        if params.handle.is_none() {
            bail!("give `unit`, `units`, `below` or `handle`");
        }
        loaded.push(resolve(
            call,
            &UnitRef {
                handle: params.handle.clone(),
                ..UnitRef::default()
            },
        )?);
    }
    for name in &names {
        loaded.push(resolve(
            call,
            &UnitRef {
                project: params.project.clone(),
                unit: Some(name.clone()),
                ..UnitRef::default()
            },
        )?);
    }
    if loaded.len() > 1 && !params.files.is_empty() {
        bail!("`files` replace one unit's: give one unit");
    }
    let shared = &*call;
    let (results, timing) =
        crate::each(shared, &loaded, params.jobs.unwrap_or(8), |unit, timing| {
            scan(shared, unit, &params, timing).unwrap_or_else(|e| {
                Err(UnitFailure {
                    unit: unit.unit.name.clone(),
                    error: format!("{e:#}"),
                    console: None,
                })
            })
        })?;
    call.timing.add(&timing);
    let mut rows = vec![];
    let mut failed = vec![];
    for outcome in results {
        match outcome {
            Ok(mut unit_rows) => {
                unit_rows.sort_by(|a, b| b.percent.total_cmp(&a.percent));
                rows.extend(unit_rows);
            }
            Err(failure) => failed.push(failure),
        }
    }
    if let Some(only) = &params.only {
        rows.retain(|r| r.verdict == *only);
    }
    let mut summary = BTreeMap::new();
    for row in &rows {
        *summary.entry(row.verdict.clone()).or_default() += 1;
    }
    Ok(ResidualResult {
        units: loaded.iter().map(|l| l.unit.name.clone()).collect(),
        rows,
        summary,
        failed,
    })
}

pub(crate) fn methods() -> Vec<Method> {
    vec![crate::method!(
        "unit.residuals",
        "residual-scan's triage of the functions that do not match (rows by kind: register, stack, other, and a verdict), on the sources compiled in memory or the objects as built; `below` takes every unit under a fuzzy match in report.json",
        ResidualParams,
        ResidualResult,
        residuals
    )]
}
