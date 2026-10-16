//! Deterministic source shrinking with the same evidence predicate as search.
//! Every accepted step is smaller and independently recompiled. No particular
//! virtual number or physical register is retained as the reduction predicate.
use super::{
    inputs::CheckCommand,
    objective::{self, Contract, EvaluationReport, Objective},
    search::{self, Budget, Build, Control, OsName, Saved, Target, Transformation},
    state,
};
use crate::{elf, evidence::EvaluationState, provenance, runner};
use anyhow::{Context, Result, ensure};
use serde::{Deserialize, Serialize};
use serde_json::{Value, json};
use std::{
    cell::Cell,
    fs::{self, OpenOptions},
    path::Path,
    time::Instant,
};

#[derive(Clone, Debug, Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct Plan {
    pub format_version: u32,
    pub build: Build,
    pub contract: Contract,
    /// Guarded replacements for whole declarations, expressions and helpers.
    /// Only replacements that reduce the byte count can be proposed.
    pub reductions: Vec<Transformation>,
    /// Coarse-to-fine contiguous whole-line deletion after guarded recipes.
    pub delete_lines: bool,
    pub semantic_check: Option<CheckCommand>,
    pub target: Option<Target>,
}
impl Plan {
    fn execution(&self) -> search::Plan {
        search::Plan {
            format_version: 1,
            build: self.build.clone(),
            contract: self.contract.clone(),
            transformations: self.reductions.clone(),
            maximum_depth: 0,
            generation_limit: 1,
            semantic_check: self.semantic_check.clone(),
            target: self.target.clone(),
        }
    }
}
pub fn validate(plan: &Plan) -> Result<()> {
    ensure!(
        plan.format_version == 1,
        "unsupported source-reduction plan version"
    );
    search::validate(&plan.execution())?;
    ensure!(
        plan.delete_lines || !plan.reductions.is_empty(),
        "no reduction passes configured"
    );
    ensure!(
        !matches!(
            plan.contract.objective,
            Objective::ChangeRangeCategory { .. }
        ),
        "a category-transition objective requires a separate reference case; use a current-state mechanism predicate for reduction"
    );
    Ok(())
}
fn normalize(plan: Plan, base: &Path) -> Result<Plan> {
    validate(&plan)?;
    let execution = search::normalize(plan.execution(), base)?;
    Ok(Plan {
        build: execution.build,
        semantic_check: execution.semantic_check,
        target: execution.target,
        ..plan
    })
}
pub fn load_plan(path: &Path) -> Result<Plan> {
    normalize(
        serde_json::from_slice(&fs::read(path)?)?,
        path.canonicalize()?
            .parent()
            .context("plan has no parent")?,
    )
}

#[derive(Clone, Debug, Serialize, Deserialize)]
pub struct Candidate {
    pub id: usize,
    pub parent: Option<usize>,
    pub source_sha256: String,
    pub bytes: usize,
    pub recipe: String,
}
#[derive(Clone, Debug, Default, Serialize, Deserialize)]
pub struct Cursor {
    pub recipe: usize,
    pub chunk_lines: usize,
    pub start_line: usize,
}
fn lines(source: &[u8]) -> Vec<usize> {
    let mut offsets = vec![0];
    offsets.extend(
        source
            .iter()
            .enumerate()
            .filter_map(|(i, b)| (*b == b'\n' && i + 1 < source.len()).then_some(i + 1)),
    );
    if !source.is_empty() {
        offsets.push(source.len());
    }
    offsets
}
impl Cursor {
    fn reset(&mut self, source: &[u8]) {
        *self = Self {
            chunk_lines: lines(source).len().saturating_sub(1).next_power_of_two(),
            ..Self::default()
        };
    }
}
/// Proposes only strictly smaller byte strings. Invalid C++ is a normal rejected
/// candidate; the compiler and evidence evaluator decide which lines are needed.
pub fn propose(plan: &Plan, source: &[u8], cursor: &mut Cursor) -> Option<(String, Vec<u8>)> {
    while let Some(recipe) = plan.reductions.get(cursor.recipe) {
        cursor.recipe += 1;
        if let Ok(next) = search::apply(source, &recipe.edits)
            && next.len() < source.len()
        {
            return Some((recipe.id.clone(), next));
        }
    }
    if !plan.delete_lines {
        return None;
    }
    let offsets = lines(source);
    let count = offsets.len().saturating_sub(1);
    while cursor.chunk_lines > 0 {
        if cursor.start_line >= count {
            cursor.chunk_lines /= 2;
            cursor.start_line = 0;
            continue;
        }
        let first = cursor.start_line;
        let last = first.saturating_add(cursor.chunk_lines).min(count);
        cursor.start_line = last;
        let (start, end) = (offsets[first], offsets[last]);
        let mut next = source.to_vec();
        next.drain(start..end);
        return Some((format!("delete-lines:{}..{}", first + 1, last), next));
    }
    None
}

#[derive(Clone, Debug, Serialize, Deserialize)]
pub struct Outcome {
    pub candidate: Candidate,
    pub status: String,
    pub artifact: Option<String>,
    pub compilation_cache_hit: bool,
    pub evaluation: Option<EvaluationReport>,
    pub verification: Option<Value>,
    pub error: Option<String>,
}
#[derive(Clone, Debug, Serialize, Deserialize)]
pub struct Progress {
    pub format_version: u32,
    pub plan_sha256: String,
    pub analysis_sha256: String,
    pub baseline_source_sha256: String,
    pub baseline_artifact: Option<String>,
    pub original_input_sha256: Option<String>,
    pub baseline_evaluation: Option<EvaluationReport>,
    pub best: Candidate,
    pub best_artifact: Option<String>,
    pub cursor: Cursor,
    pub pending: Option<Candidate>,
    pub outcomes: Vec<Outcome>,
    pub status: String,
    pub compilations: usize,
    pub cache_hits: usize,
    pub error: Option<String>,
}
fn is_satisfied(report: &EvaluationReport) -> bool {
    report.overall.state == EvaluationState::Satisfied
}
fn terminal(status: &str) -> bool {
    matches!(
        status,
        "verified_reduction" | "no_reduction" | "baseline_not_satisfied"
    )
}
fn read_source(output: &Path, candidate: &Candidate, name: &OsName) -> Result<Vec<u8>> {
    let source = fs::read(search::source_path(output, &candidate.source_sha256, name)?)?;
    ensure!(
        state::digest(&source) == candidate.source_sha256 && source.len() == candidate.bytes,
        "retained reduction source was modified"
    );
    Ok(source)
}
struct CapturedSource<'a> {
    saved: &'a Saved,
    source: &'a [u8],
    path: &'a Path,
    object: &'a Path,
}
fn evaluate(
    plan: &search::Plan,
    baseline_input: CapturedSource<'_>,
    candidate_input: CapturedSource<'_>,
    control: &Control<'_>,
) -> Result<(EvaluationReport, Value)> {
    let CapturedSource {
        saved: baseline,
        source: baseline_source,
        path: baseline_path,
        object: baseline_object,
    } = baseline_input;
    let CapturedSource {
        saved: candidate,
        source,
        path,
        object,
    } = candidate_input;
    let directory = &baseline.inputs.working_directory;
    let baseline_check = search::checker(
        plan,
        baseline_path,
        baseline_object,
        baseline_path,
        baseline_object,
        directory,
        control,
    )?;
    let check = search::checker(
        plan,
        path,
        object,
        baseline_path,
        baseline_object,
        directory,
        control,
    )?;
    let a = provenance::parse(&baseline.compilation.trace)?;
    let b = provenance::parse(&candidate.compilation.trace)?;
    let mut report = objective::evaluate(
        &plan.contract,
        &search::input(baseline, &a, baseline_source, &baseline_check),
        &search::input(candidate, &b, source, &check),
    )?;
    report.final_target_mismatch = search::final_mismatch(plan, &candidate.compilation)?;
    Ok((report, json!({"baseline":baseline_check,"candidate":check})))
}

/// Greedy fixed point over configured recipes and coarse-to-fine line deletion.
/// Budgets apply per invocation. A pending candidate is not skipped when its
/// independent verification is interrupted; resume reuses its exact-input cache.
pub fn run(plan: &Plan, output: &Path, budget: &Budget, resume: bool) -> Result<Progress> {
    let began = Instant::now();
    ensure!(budget.seconds > 0, "time budget must be positive");
    let plan = normalize(plan.clone(), &std::env::current_dir()?)?;
    let execution = plan.execution();
    fs::create_dir_all(output)?;
    let output = output.canonicalize()?;
    let lock = OpenOptions::new()
        .read(true)
        .write(true)
        .create(true)
        .truncate(false)
        .open(output.join("run.lock"))?;
    lock.try_lock()
        .context("another process owns this reduction")?;
    let base = search::request(&execution)?;
    let original_path = base.working_directory.join(&base.source).canonicalize()?;
    let source = fs::read(&original_path)?;
    let name = OsName(
        original_path
            .file_name()
            .context("source has no filename")?
            .into(),
    );
    let plan_sha256 = state::digest(&serde_json::to_vec(&plan)?);
    let analysis_sha256 = search::analysis_identity(&execution, &base)?;
    let analysis_inputs_sha256 = search::analysis_inputs_identity(&execution, &base)?;
    let checkpoint = output.join("progress.json");
    let mut progress: Progress = if checkpoint.exists() {
        ensure!(resume, "output already has a reduction; use --resume");
        let saved: Progress = serde_json::from_slice(&fs::read(&checkpoint)?)?;
        ensure!(
            saved.format_version == 1
                && saved.plan_sha256 == plan_sha256
                && saved.analysis_sha256 == analysis_sha256
                && saved.baseline_source_sha256 == state::digest(&source),
            "plan/source/analyzer/checker inputs/target changed; use a new run directory"
        );
        ensure!(
            !terminal(&saved.status),
            "reduction already finished; replay its result or use a new run directory"
        );
        saved
    } else {
        ensure!(!resume, "no reduction to resume");
        search::atomic_json(&output.join("plan.json"), &plan)?;
        let sha = search::store_source(&output, &source, &name)?;
        let mut cursor = Cursor::default();
        cursor.reset(&source);
        Progress {
            format_version: 1,
            plan_sha256,
            analysis_sha256,
            baseline_source_sha256: sha.clone(),
            baseline_artifact: None,
            original_input_sha256: None,
            baseline_evaluation: None,
            best: Candidate {
                id: 0,
                parent: None,
                source_sha256: sha,
                bytes: source.len(),
                recipe: "baseline".into(),
            },
            best_artifact: None,
            cursor,
            pending: None,
            outcomes: vec![],
            status: "running".into(),
            compilations: 0,
            cache_hits: 0,
            error: None,
        }
    };
    let control = Control {
        budget,
        began,
        compilations: Cell::new(0),
    };
    progress.status = "running".into();
    progress.error = None;
    search::atomic_json(&checkpoint, &progress)?;
    let work = (|| -> Result<()> {
        let baseline_path = search::source_path(&output, &progress.baseline_source_sha256, &name)?;
        ensure!(
            state::digest(&fs::read(&baseline_path)?) == progress.baseline_source_sha256,
            "baseline source was modified"
        );
        let (baseline, baseline_key, hit) = search::artifact(
            &search::variant_request(&base, &baseline_path),
            &output,
            &control,
            false,
            "baseline",
        )?;
        progress.cache_hits += usize::from(hit);
        if let Some(old) = &progress.baseline_artifact {
            ensure!(old == &baseline_key, "baseline build inputs changed");
        }
        progress.baseline_artifact = Some(baseline_key.clone());
        let (original, original_key, hit) =
            search::artifact(&base, &output, &control, false, "original")?;
        progress.cache_hits += usize::from(hit);
        if let Some(old) = &progress.original_input_sha256 {
            ensure!(old == &original_key, "original build inputs changed");
        }
        progress.original_input_sha256 = Some(original_key);
        ensure!(
            baseline.compilation.check_status == runner::CheckStatus::Verified
                && original.compilation.check_status == runner::CheckStatus::Verified
                && elf::comparable_sections(&baseline.compilation.object)?
                    == elf::comparable_sections(&original.compilation.object)?,
            "private reduction source failed stock-equivalent baseline calibration"
        );
        let baseline_object = output.join("cache").join(&baseline_key).join("object.o");
        let (baseline_evaluation, _) = evaluate(
            &execution,
            CapturedSource {
                saved: &baseline,
                source: &source,
                path: &baseline_path,
                object: &baseline_object,
            },
            CapturedSource {
                saved: &baseline,
                source: &source,
                path: &baseline_path,
                object: &baseline_object,
            },
            &control,
        )?;
        let satisfied = is_satisfied(&baseline_evaluation);
        ensure!(
            search::analysis_inputs_identity(&execution, &base)? == analysis_inputs_sha256,
            "checker inputs/target changed during baseline evaluation"
        );
        progress.baseline_evaluation = Some(baseline_evaluation);
        if !satisfied {
            progress.status = "baseline_not_satisfied".into();
            return Ok(());
        }
        if progress.best.id == 0 {
            progress.best_artifact = Some(baseline_key.clone());
        } else {
            // Re-scan the current best too: its candidate-only headers may not
            // occur in the original baseline dependency closure.
            let path = search::source_path(&output, &progress.best.source_sha256, &name)?;
            read_source(&output, &progress.best, &name)?;
            let (_, key, hit) = search::artifact(
                &search::variant_request(&base, &path),
                &output,
                &control,
                false,
                "resume-best",
            )?;
            progress.cache_hits += usize::from(hit);
            ensure!(
                Some(&key) == progress.best_artifact.as_ref(),
                "accepted candidate build inputs changed"
            );
        }
        let mut evaluated = 0;
        while evaluated < budget.candidates && !control.cancelled() {
            if progress.pending.is_none() {
                let best = read_source(&output, &progress.best, &name)?;
                let Some((recipe, raw)) = propose(&plan, &best, &mut progress.cursor) else {
                    ensure!(
                        search::analysis_inputs_identity(&execution, &base)?
                            == analysis_inputs_sha256,
                        "checker inputs/target changed during reduction"
                    );
                    progress.status = if progress.best.id == 0 {
                        "no_reduction"
                    } else {
                        "verified_reduction"
                    }
                    .into();
                    let path = search::source_path(&output, &progress.best.source_sha256, &name)?;
                    search::atomic_json(
                        &output.join("result.json"),
                        &json!({
                        "format_version":1,"status":progress.status,"source":path,
                        "candidate":progress.best,"artifact":progress.best_artifact,
                        "original_bytes":source.len(),"reduced_bytes":progress.best.bytes,
                        "minimality":"fixed point of configured passes; not a global minimum",
                        "purpose":plan.contract.purpose,"transplantation_validated":false}),
                    )?;
                    return Ok(());
                };
                progress.pending = Some(Candidate {
                    id: progress.outcomes.len() + 1,
                    parent: Some(progress.best.id),
                    source_sha256: search::store_source(&output, &raw, &name)?,
                    bytes: raw.len(),
                    recipe,
                });
                search::atomic_json(&checkpoint, &progress)?;
            }
            let candidate = progress.pending.clone().unwrap();
            let raw = read_source(&output, &candidate, &name)?;
            ensure!(
                candidate.parent == Some(progress.best.id) && raw.len() < progress.best.bytes,
                "pending candidate is not a smaller child of the accepted case"
            );
            let path = search::source_path(&output, &candidate.source_sha256, &name)?;
            let process = (|| -> Result<Outcome> {
                let (saved, key, hit) = search::artifact(
                    &search::variant_request(&base, &path),
                    &output,
                    &control,
                    false,
                    &format!("candidate-{}", candidate.id),
                )?;
                progress.cache_hits += usize::from(hit);
                let object = output.join("cache").join(&key).join("object.o");
                let (evaluation, check) = evaluate(
                    &execution,
                    CapturedSource {
                        saved: &baseline,
                        source: &source,
                        path: &baseline_path,
                        object: &baseline_object,
                    },
                    CapturedSource {
                        saved: &saved,
                        source: &raw,
                        path: &path,
                        object: &object,
                    },
                    &control,
                )?;
                let mut verification = None;
                let mut accepted = false;
                if is_satisfied(&evaluation) {
                    fs::create_dir_all(output.join("verification"))?;
                    let directory = tempfile::Builder::new()
                        .prefix(&format!("candidate-{}-", candidate.id))
                        .tempdir_in(output.join("verification"))?
                        .keep();
                    let tag = directory.file_name().unwrap().to_string_lossy();
                    let (fresh, _, _) = search::artifact(
                        &search::variant_request(&base, &path),
                        &output,
                        &control,
                        true,
                        &tag,
                    )?;
                    let (fresh_evaluation, fresh_check) = evaluate(
                        &execution,
                        CapturedSource {
                            saved: &baseline,
                            source: &source,
                            path: &baseline_path,
                            object: &baseline_object,
                        },
                        CapturedSource {
                            saved: &fresh,
                            source: &raw,
                            path: &path,
                            object: &directory.join("object.o"),
                        },
                        &control,
                    )?;
                    accepted = is_satisfied(&fresh_evaluation)
                        && fresh.inputs == saved.inputs
                        && fresh.compilation.object_sha256 == saved.compilation.object_sha256;
                    verification = Some(json!({"satisfied":accepted,"directory":directory,
                        "evaluation":fresh_evaluation,"semantic_check":fresh_check,
                        "object_sha256":fresh.compilation.object_sha256}));
                }
                Ok(Outcome {
                    candidate: candidate.clone(),
                    status: if accepted { "accepted" } else { "rejected" }.into(),
                    artifact: Some(key),
                    compilation_cache_hit: hit,
                    evaluation: Some(evaluation),
                    verification: Some(json!({"independent":verification,"semantic_check":check})),
                    error: None,
                })
            })();
            let outcome = match process {
                Ok(value) => value,
                Err(e) if e.is::<search::BudgetReached>() || e.is::<runner::Cancelled>() => {
                    return Err(e);
                }
                Err(e) => Outcome {
                    candidate: candidate.clone(),
                    status: "rejected".into(),
                    artifact: None,
                    compilation_cache_hit: false,
                    evaluation: None,
                    verification: None,
                    error: Some(format!("{e:#}")),
                },
            };
            ensure!(
                search::analysis_inputs_identity(&execution, &base)? == analysis_inputs_sha256,
                "checker inputs/target changed during candidate evaluation"
            );
            if outcome.status == "accepted" {
                progress.best = candidate;
                progress.best_artifact = outcome.artifact.clone();
                progress.cursor.reset(&raw);
            }
            search::atomic_json(
                &output
                    .join("candidates")
                    .join(format!("{}.json", outcome.candidate.id)),
                &outcome,
            )?;
            progress.outcomes.push(outcome);
            progress.pending = None;
            evaluated += 1;
            search::atomic_json(&checkpoint, &progress)?;
        }
        progress.status = if control.cancelled() {
            if budget.cancel_file.exists() {
                "cancelled"
            } else {
                "time_budget"
            }
        } else {
            "candidate_budget"
        }
        .into();
        Ok(())
    })();
    progress.compilations += control.compilations.get();
    match work {
        Ok(()) => {}
        Err(e) if e.is::<search::BudgetReached>() => progress.status = "compilation_budget".into(),
        Err(e) if e.is::<runner::Cancelled>() => {
            progress.status = if budget.cancel_file.exists() {
                "cancelled"
            } else {
                "time_budget"
            }
            .into()
        }
        Err(e) => {
            progress.status = "failed".into();
            progress.error = Some(format!("{e:#}"));
            search::atomic_json(&checkpoint, &progress)?;
            return Err(e);
        }
    }
    search::atomic_json(&checkpoint, &progress)?;
    Ok(progress)
}

/// Fresh original-context calibration, baseline and selected reduced case; no
/// saved outcome is changed. Dependency or analyzer drift invalidates replay.
pub fn replay(output: &Path, id: usize, budget: &Budget) -> Result<Outcome> {
    let began = Instant::now();
    ensure!(budget.seconds > 0, "time budget must be positive");
    let output = output.canonicalize()?;
    let lock = OpenOptions::new()
        .read(true)
        .write(true)
        .open(output.join("run.lock"))?;
    lock.try_lock()
        .context("another process owns this reduction")?;
    let plan = load_plan(&output.join("plan.json"))?;
    let execution = plan.execution();
    let progress: Progress = serde_json::from_slice(&fs::read(output.join("progress.json"))?)?;
    ensure!(
        progress.format_version == 1
            && progress.plan_sha256 == state::digest(&serde_json::to_vec(&plan)?),
        "saved reduction plan changed"
    );
    let outcome = progress
        .outcomes
        .iter()
        .find(|o| o.candidate.id == id)
        .context("candidate ID unavailable")?;
    let old_candidate = search::load_artifact(
        &output,
        outcome
            .artifact
            .as_ref()
            .context("candidate has no capture")?,
    )?;
    let old_baseline = search::load_artifact(
        &output,
        progress
            .baseline_artifact
            .as_ref()
            .context("baseline has no capture")?,
    )?;
    let base = search::request(&execution)?;
    ensure!(
        search::analysis_identity(&execution, &base)? == progress.analysis_sha256,
        "analyzer/checker inputs/target changed"
    );
    let analysis_inputs_sha256 = search::analysis_inputs_identity(&execution, &base)?;
    let original = base.working_directory.join(&base.source).canonicalize()?;
    let name = OsName(
        original
            .file_name()
            .context("source has no filename")?
            .into(),
    );
    let source = fs::read(&original)?;
    ensure!(
        state::digest(&source) == progress.baseline_source_sha256,
        "original source changed"
    );
    let baseline_path = search::source_path(&output, &progress.baseline_source_sha256, &name)?;
    ensure!(
        fs::read(&baseline_path)? == source,
        "saved baseline source changed"
    );
    let candidate = &outcome.candidate;
    let raw = read_source(&output, candidate, &name)?;
    let path = search::source_path(&output, &candidate.source_sha256, &name)?;
    for file in old_baseline
        .inputs
        .dependencies
        .iter()
        .chain(&old_candidate.inputs.dependencies)
    {
        ensure!(
            state::digest(&fs::read(&file.path)?) == file.sha256,
            "dependency changed: {}; retained bytes are in input-files/{}",
            file.path.display(),
            file.sha256
        );
    }
    let control = Control {
        budget,
        began,
        compilations: Cell::new(0),
    };
    fs::create_dir_all(output.join("replays"))?;
    let directory = tempfile::Builder::new()
        .prefix(&format!("candidate-{id}-"))
        .tempdir_in(output.join("replays"))?
        .keep();
    let tag = directory.file_name().unwrap().to_string_lossy();
    let baseline_tag = format!("{tag}-baseline");
    let candidate_tag = format!("{tag}-candidate");
    let (baseline, _, _) = search::artifact(
        &search::variant_request(&base, &baseline_path),
        &output,
        &control,
        true,
        &baseline_tag,
    )?;
    let (original, original_key, _) =
        search::artifact(&base, &output, &control, true, &format!("{tag}-original"))?;
    ensure!(
        baseline.inputs == old_baseline.inputs
            && Some(&original_key) == progress.original_input_sha256.as_ref(),
        "baseline replay inputs changed"
    );
    ensure!(
        baseline.compilation.check_status == runner::CheckStatus::Verified
            && original.compilation.check_status == runner::CheckStatus::Verified
            && elf::comparable_sections(&baseline.compilation.object)?
                == elf::comparable_sections(&original.compilation.object)?,
        "baseline replay calibration failed"
    );
    let (saved, key, _) = search::artifact(
        &search::variant_request(&base, &path),
        &output,
        &control,
        true,
        &candidate_tag,
    )?;
    ensure!(
        saved.inputs == old_candidate.inputs,
        "candidate replay inputs changed"
    );
    let (evaluation, check) = evaluate(
        &execution,
        CapturedSource {
            saved: &baseline,
            source: &source,
            path: &baseline_path,
            object: &output
                .join("verification")
                .join(&baseline_tag)
                .join("object.o"),
        },
        CapturedSource {
            saved: &saved,
            source: &raw,
            path: &path,
            object: &output
                .join("verification")
                .join(&candidate_tag)
                .join("object.o"),
        },
        &control,
    )?;
    let same_object = saved.compilation.object_sha256 == old_candidate.compilation.object_sha256;
    ensure!(
        search::analysis_inputs_identity(&execution, &base)? == analysis_inputs_sha256,
        "checker inputs/target changed during replay"
    );
    let outcome = Outcome {
        candidate: candidate.clone(),
        status: if is_satisfied(&evaluation) && same_object {
            "replay_satisfied"
        } else {
            "replay_not_satisfied"
        }
        .into(),
        artifact: Some(key),
        compilation_cache_hit: false,
        evaluation: Some(evaluation),
        verification: Some(
            json!({"fresh_compilations":control.compilations.get(),"directory":directory,
            "object_reproduced":same_object,"semantic_check":check}),
        ),
        error: None,
    };
    search::atomic_json(&directory.join("outcome.json"), &outcome)?;
    Ok(outcome)
}
