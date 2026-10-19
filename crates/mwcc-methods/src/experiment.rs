//! `experiment.run`: a plan (mwcc-experiment's) run against a unit, one result per finished candidate (`row`) as it
//! is written to `results.jsonl`, then the run's summary.
use crate::{
    Call, Method, Timing,
    unit::{Loaded, UnitRef, resolve, score_options},
};
use anyhow::Result;
use mwcc_experiment::{
    plan::{Plan, Stage},
    run::{Context, Row, Summary, run as run_plan},
};
use schemars::JsonSchema;
use serde::{Deserialize, Serialize};
use serde_json::{Map, Value};
use std::path::PathBuf;

#[derive(Debug, Deserialize, JsonSchema)]
#[serde(deny_unknown_fields)]
pub struct RunParams {
    pub handle: Option<String>,
    pub project: Option<PathBuf>,
    pub unit: Option<String>,
    /// The plan (docs/oracle/experiments.md): `output`, `files`, `baseline`, `space`, `stages`, `strategy`,
    /// `budget`, `symbols`, `jobs`, `keep_objects`, `diff`, `timeout_ms`.
    #[schemars(with = "Value")]
    pub plan: Plan,
}

#[derive(Serialize, JsonSchema)]
pub struct RunResult {
    /// The run: its candidates, statuses, controls, why it stopped, and its timing.
    #[schemars(with = "Value")]
    pub summary: Summary,
}

fn convert(timing: &mwcc_experiment::Timing) -> Timing {
    Timing {
        load: timing.load,
        compile: timing.compile,
        score: timing.score,
        trace: timing.trace,
        model: timing.model,
        queue: timing.queue,
        other: timing.other,
    }
}

fn run(call: &mut Call<'_>, params: RunParams) -> Result<RunResult> {
    let loaded = resolve(
        call,
        &UnitRef {
            handle: params.handle,
            project: params.project,
            unit: params.unit,
        },
    )?;
    let summary = run_loaded(call, &loaded, &params.plan, &|call, row| {
        if let Ok(row) = serde_json::to_value(row) {
            call.emit(Map::from_iter([("row".to_owned(), row)]));
        }
    })?;
    Ok(RunResult { summary })
}

/// PLAN run against LOADED, each row given to ON_ROW; the run's timing added to the call's.
pub(crate) fn run_loaded(
    call: &mut Call<'_>,
    loaded: &Loaded,
    plan: &Plan,
    on_row: &(dyn Fn(&Call<'_>, &Row) + Sync),
) -> Result<Summary> {
    let target = if plan.stages.iter().any(|s| matches!(s, Stage::Score(_))) {
        Some(loaded.target(call.session, &mut call.timing)?)
    } else {
        None
    };
    let summary = {
        let call = &*call;
        let cx = Context {
            unit: &loaded.unit,
            target: target.as_deref(),
            score_options: score_options(&loaded.unit),
            oracle_root: &call.session.oracle_root,
            cancel: call.cancel,
            cache: Some(&call.session.compile_cache),
            pool: call.session.persist_pool.as_ref(),
        };
        run_plan(plan, &cx, &|row| on_row(call, row))?
    };
    // (the rows carry their own timing; the call's is the run's)
    let timing = convert(&summary.timing);
    call.timing.compile += timing.compile;
    call.timing.score += timing.score;
    call.timing.model += timing.model;
    call.timing.trace += timing.trace;
    Ok(summary)
}

pub(crate) fn methods() -> Vec<Method> {
    vec![crate::method!(
        "experiment.run",
        "a plan of candidates (a space, stages, a strategy, a budget) run against a unit with its controls: one `row` per finished candidate, as results.jsonl gets it, then the `summary`",
        RunParams,
        RunResult,
        run
    )]
}
