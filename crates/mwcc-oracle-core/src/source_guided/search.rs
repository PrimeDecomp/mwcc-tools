//! Bounded source exploration. Compilation cache and relevant-state groups have
//! separate identities; every source representative remains available to expand.
use super::{
    inputs::{self, CheckCommand, CheckOutcome},
    objective::{self, Contract, EvaluationReport, Input},
    state,
};
use crate::{
    elf,
    evidence::{Evaluation, EvaluationState, EvidenceGap, Reason},
    ninja, provenance,
    runner::{self, Compilation, CompileRequest, TraceOptions},
    version::Version,
};
use anyhow::{Context, Result, ensure};
use serde::{Deserialize, Serialize};
use serde_json::{Value, json};
use std::{
    cell::Cell,
    collections::{BTreeMap, BTreeSet},
    fs::{self, OpenOptions},
    path::{Path, PathBuf},
    time::{Duration, Instant},
};

fn options() -> TraceOptions {
    TraceOptions {
        backend: true,
        stages: true,
        locals: true,
        cse: true,
        ..TraceOptions::default()
    }
}
#[derive(Clone, Debug, Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct Build {
    pub oracle_root: PathBuf,
    pub working_directory: PathBuf,
    pub source: String,
    pub version: Option<Version>,
    pub flags: Vec<String>,
    pub from_ninja: Option<PathBuf>,
    #[serde(default = "options")]
    pub traces: TraceOptions,
    pub timeout_seconds: u64,
}
#[derive(Clone, Debug, Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct Edit {
    pub find: String,
    pub replacement: String,
}
#[derive(Clone, Debug, Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct Transformation {
    pub id: String,
    pub family: String,
    pub precondition: String,
    pub edits: Vec<Edit>,
}
#[derive(Clone, Debug, Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct Target {
    pub object: PathBuf,
    pub link: String,
}
#[derive(Clone, Debug, Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct Plan {
    pub format_version: u32,
    pub build: Build,
    pub contract: Contract,
    pub transformations: Vec<Transformation>,
    pub maximum_depth: usize,
    pub generation_limit: usize,
    pub semantic_check: Option<CheckCommand>,
    pub target: Option<Target>,
}
#[derive(Clone, Debug)]
pub struct Budget {
    pub candidates: usize,
    pub compilations: usize,
    pub seconds: u64,
    pub cancel_file: PathBuf,
}
#[derive(Clone, Debug, Serialize, Deserialize)]
pub struct Candidate {
    pub id: usize,
    pub parent: Option<usize>,
    pub source_sha256: String,
    pub transformations: Vec<String>,
}
#[derive(Clone, Debug, Serialize, Deserialize)]
pub struct Outcome {
    pub id: usize,
    pub status: String,
    pub compilation_cache_hit: bool,
    pub artifact: Option<String>,
    pub artifact_directory: Option<PathBuf>,
    pub fingerprint: Option<String>,
    #[serde(default)]
    pub fingerprint_error: Option<String>,
    pub duplicate_state_of: Option<usize>,
    pub evaluation: Option<EvaluationReport>,
    pub semantic_check: Option<CheckOutcome>,
    pub independent_verification: Option<Value>,
    pub error: Option<String>,
}
#[derive(Clone, Debug, Serialize, Deserialize)]
pub struct Progress {
    pub format_version: u32,
    pub plan_sha256: String,
    pub baseline_source_sha256: String,
    pub baseline_input_sha256: Option<String>,
    pub original_input_sha256: Option<String>,
    pub analysis_sha256: String,
    pub baseline_artifact: Option<String>,
    pub baseline_calibration: Option<Evaluation>,
    pub queue: Vec<Candidate>,
    pub outcomes: Vec<Outcome>,
    pub next: usize,
    pub groups: BTreeMap<String, Vec<usize>>,
    pub ranking: Vec<usize>,
    pub status: String,
    pub generation_truncated: bool,
    pub compilations: usize,
    pub cache_hits: usize,
    pub error: Option<String>,
}
#[derive(Debug)]
pub(super) struct BudgetReached;
impl std::fmt::Display for BudgetReached {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.write_str("compilation budget reached")
    }
}
impl std::error::Error for BudgetReached {}
pub(super) struct Control<'a> {
    pub(super) budget: &'a Budget,
    pub(super) began: Instant,
    pub(super) compilations: Cell<usize>,
}
impl Control<'_> {
    pub(super) fn cancelled(&self) -> bool {
        self.budget.cancel_file.exists()
            || self.began.elapsed() >= Duration::from_secs(self.budget.seconds)
    }
    fn consume(&self) -> Result<()> {
        if self.compilations.get() >= self.budget.compilations {
            return Err(BudgetReached.into());
        }
        self.compilations.set(self.compilations.get() + 1);
        Ok(())
    }
}
pub(super) fn atomic_json(path: &Path, value: &impl Serialize) -> Result<()> {
    fs::create_dir_all(path.parent().context("output has no parent")?)?;
    let mut temp = tempfile::NamedTempFile::new_in(path.parent().unwrap())?;
    use std::io::Write;
    serde_json::to_writer_pretty(&mut temp, value)?;
    temp.write_all(b"\n")?;
    temp.flush()?;
    temp.persist(path)?;
    Ok(())
}
pub fn load_plan(path: &Path) -> Result<Plan> {
    let plan: Plan = serde_json::from_slice(&fs::read(path)?)?;
    let base = path.canonicalize()?.parent().unwrap().to_owned();
    normalize(plan, &base)
}
pub(super) fn normalize(mut plan: Plan, base: &Path) -> Result<Plan> {
    for path in [
        &mut plan.build.oracle_root,
        &mut plan.build.working_directory,
    ] {
        if !path.is_absolute() {
            *path = base.join(&*path);
        }
        *path = path.canonicalize()?;
    }
    if let Some(path) = &mut plan.build.from_ninja {
        if !path.is_absolute() {
            *path = base.join(&*path);
        }
        *path = path.canonicalize()?;
    }
    if let Some(check) = &mut plan.semantic_check
        && check.program.components().count() > 1
        && !check.program.is_absolute()
    {
        check.program = base.join(&check.program);
    }
    if let Some(target) = &mut plan.target {
        if !target.object.is_absolute() {
            target.object = base.join(&target.object);
        }
        target.object = target.object.canonicalize()?;
    }
    validate(&plan)?;
    Ok(plan)
}
pub fn validate(plan: &Plan) -> Result<()> {
    ensure!(
        plan.format_version == 1,
        "unsupported source-search plan version"
    );
    objective::validate(&plan.contract)?;
    ensure!(
        plan.build.traces.backend && plan.build.timeout_seconds > 0,
        "search requires backend tracing and a positive timeout"
    );
    ensure!(
        plan.maximum_depth <= 16 && plan.generation_limit > 0,
        "invalid source-generation bounds"
    );
    let mut ids = BTreeSet::new();
    for t in &plan.transformations {
        ensure!(
            !t.id.is_empty()
                && ids.insert(&t.id)
                && !t.family.is_empty()
                && !t.precondition.is_empty()
                && !t.edits.is_empty(),
            "invalid or duplicate transformation"
        );
        ensure!(
            t.edits.iter().all(|e| !e.find.is_empty()),
            "empty edit anchor"
        );
    }
    if plan.build.from_ninja.is_some() {
        ensure!(
            plan.build.flags.is_empty(),
            "Ninja flags and explicit flags are mutually exclusive"
        );
    }
    ensure!(
        !plan.build.flags.iter().any(|f| f.starts_with('@')
            || matches!(
                f.as_str(),
                "-E" | "-EP" | "-M" | "-P" | "-preprocess" | "-precompile" | "-o"
            )),
        "search does not accept response/output/preprocessing flags"
    );
    Ok(())
}
pub(super) fn request(plan: &Plan) -> Result<CompileRequest> {
    let (source, flags, directory, version) = if let Some(ninja) = &plan.build.from_ninja {
        let (rule, cwd) = ninja::find_rule(ninja, &plan.build.source)?;
        let inferred = rule.mw_version.as_deref().map(Version::parse).transpose()?;
        if let (Some(explicit), Some(inferred)) = (plan.build.version, inferred) {
            ensure!(
                explicit == inferred,
                "search compiler version disagrees with Ninja"
            );
        }
        (
            rule.source,
            rule.flags,
            cwd,
            plan.build
                .version
                .or(inferred)
                .context("Ninja rule has no compiler version")?,
        )
    } else {
        (
            plan.build.source.clone(),
            plan.build.flags.clone(),
            plan.build.working_directory.clone(),
            plan.build
                .version
                .context("explicit build needs a version")?,
        )
    };
    ensure!(version.has_oracle(), "unsupported source-search compiler");
    ensure!(
        !flags.iter().any(|f| f.starts_with('@')
            || matches!(
                f.as_str(),
                "-E" | "-EP" | "-M" | "-P" | "-preprocess" | "-precompile" | "-o"
            )),
        "unsupported mode in search compiler flags"
    );
    Ok(CompileRequest {
        root: plan.build.oracle_root.clone(),
        working_directory: directory,
        source,
        flags,
        version,
        options: plan.build.traces.clone(),
        overrides: vec![],
        check: true,
        original_fallback: false,
        cut_limit: None,
        merge_limit: None,
        keep_object: None,
        timeout: Duration::from_secs(plan.build.timeout_seconds),
    })
}
/// Exact anchors are checked against one input before any replacement is applied.
/// Every rule is either atomic or rejected; source encodings outside anchors stay bytes.
pub fn apply(source: &[u8], edits: &[Edit]) -> Result<Vec<u8>> {
    let mut ranges = vec![];
    for edit in edits {
        ensure!(!edit.find.is_empty(), "empty edit anchor");
        let needle = edit.find.as_bytes();
        let matches = source
            .windows(needle.len())
            .enumerate()
            .filter_map(|(at, bytes)| (bytes == needle).then_some(at))
            .collect::<Vec<_>>();
        ensure!(matches.len() == 1, "edit anchor must occur exactly once");
        ranges.push((
            matches[0],
            matches[0] + needle.len(),
            edit.replacement.as_bytes(),
        ));
    }
    ranges.sort_by_key(|r| r.0);
    ensure!(
        ranges.windows(2).all(|w| w[0].1 <= w[1].0),
        "edit anchors overlap"
    );
    let mut output = source.to_vec();
    for (start, end, replacement) in ranges.into_iter().rev() {
        output.splice(start..end, replacement.iter().copied());
    }
    Ok(output)
}
pub(super) fn source_path(output: &Path, sha: &str, name: &OsName) -> Result<PathBuf> {
    ensure!(
        sha.len() == 64 && sha.bytes().all(|b| b.is_ascii_hexdigit()),
        "invalid saved source hash"
    );
    Ok(output.join("sources").join(sha).join(&name.0))
}
pub(super) struct OsName(pub(super) std::ffi::OsString);
pub(super) fn store_source(output: &Path, source: &[u8], name: &OsName) -> Result<String> {
    let sha = state::digest(source);
    let path = source_path(output, &sha, name)?;
    fs::create_dir_all(path.parent().unwrap())?;
    if path.exists() {
        ensure!(fs::read(&path)? == source, "saved source was modified");
    } else {
        fs::write(path, source)?;
    }
    Ok(sha)
}
pub(super) fn variant_request(base: &CompileRequest, source: &Path) -> CompileRequest {
    let mut request = base.clone();
    request.overrides = vec![format!("{}={}", base.source, source.display())];
    request
}
#[derive(Serialize, Deserialize)]
pub(super) struct Saved {
    pub(super) inputs: inputs::Snapshot,
    pub(super) compilation: Compilation,
}
pub(super) fn load_artifact(output: &Path, key: &str) -> Result<Saved> {
    ensure!(
        key.len() == 64 && key.bytes().all(|b| b.is_ascii_hexdigit()),
        "invalid artifact key"
    );
    let root = output.join("cache").join(key);
    let bytes = fs::read(root.join("capture.json"))?;
    let completion: Value = serde_json::from_slice(&fs::read(root.join("complete.json"))?)?;
    ensure!(
        completion["capture_sha256"] == state::digest(&bytes),
        "cached capture was modified"
    );
    let mut saved: Saved = serde_json::from_slice(&bytes)?;
    ensure!(
        inputs::key(&saved.inputs)? == key,
        "cached inputs do not match their key"
    );
    saved.compilation.object = fs::read(root.join("object.o"))?;
    ensure!(
        saved.compilation.object_sha256 == state::digest(&saved.compilation.object),
        "cached object was modified"
    );
    for file in saved
        .inputs
        .dependencies
        .iter()
        .chain(&saved.inputs.runtime)
    {
        let bytes = fs::read(output.join("input-files").join(&file.sha256))
            .with_context(|| format!("retained input blob missing for {}", file.path.display()))?;
        ensure!(
            state::digest(&bytes) == file.sha256,
            "retained input blob was modified"
        );
    }
    Ok(saved)
}
pub(super) fn retain_inputs(output: &Path, snapshot: &inputs::Snapshot) -> Result<()> {
    let root = output.join("input-files");
    fs::create_dir_all(&root)?;
    for file in snapshot.dependencies.iter().chain(&snapshot.runtime) {
        let bytes = fs::read(&file.path)?;
        ensure!(
            state::digest(&bytes) == file.sha256,
            "input changed before it could be retained"
        );
        let blob = root.join(&file.sha256);
        if blob.is_file() {
            ensure!(
                state::digest(&fs::read(blob)?) == file.sha256,
                "retained input blob was modified"
            );
        } else {
            fs::write(blob, bytes)?;
        }
    }
    Ok(())
}
pub(super) fn artifact(
    request: &CompileRequest,
    output: &Path,
    control: &Control<'_>,
    fresh: bool,
    tag: &str,
) -> Result<(Saved, String, bool)> {
    fs::create_dir_all(output.join("scans"))?;
    let scans = tempfile::Builder::new()
        .prefix(&format!("{tag}-"))
        .tempdir_in(output.join("scans"))?
        .keep();
    let before = inputs::capture(request, &scans.join("before"), &|| control.cancelled())?;
    let key = inputs::key(&before)?;
    atomic_json(&scans.join("before").join("snapshot.json"), &before)?;
    retain_inputs(output, &before)?;
    if !fresh
        && before.reusable
        && output
            .join("cache")
            .join(&key)
            .join("complete.json")
            .is_file()
    {
        let saved = load_artifact(output, &key)?;
        ensure!(
            saved.inputs == before,
            "cache key does not identify the current inputs"
        );
        return Ok((saved, key, true));
    }
    control.consume()?;
    let compilation = runner::compile_controlled(request, &|| control.cancelled())?;
    let after = inputs::capture(request, &scans.join("after"), &|| control.cancelled())?;
    atomic_json(&scans.join("after").join("snapshot.json"), &after)?;
    ensure!(
        before == after,
        "build inputs changed while the candidate compiled"
    );
    let saved = Saved {
        inputs: before,
        compilation,
    };
    let root = if fresh {
        output.join("verification").join(tag)
    } else {
        output.join("cache").join(&key)
    };
    fs::create_dir_all(&root)?;
    fs::write(root.join("object.o"), &saved.compilation.object)?;
    let bytes = serde_json::to_vec(&saved)?;
    fs::write(root.join("capture.json"), &bytes)?;
    atomic_json(
        &root.join("complete.json"),
        &json!({"capture_sha256":state::digest(&bytes),"input_sha256":key,"independent":fresh}),
    )?;
    Ok((saved, key, false))
}
pub(super) fn checker(
    plan: &Plan,
    source: &Path,
    object: &Path,
    baseline_source: &Path,
    baseline_object: &Path,
    directory: &Path,
    control: &Control<'_>,
) -> Result<Option<CheckOutcome>> {
    let Some(check) = &plan.semantic_check else {
        return Ok(None);
    };
    let mut check = check.clone();
    for argument in &mut check.arguments {
        *argument = argument
            .replace("{baseline_source}", &baseline_source.to_string_lossy())
            .replace("{baseline_object}", &baseline_object.to_string_lossy());
    }
    let before = inputs::checker_inputs(&check, directory)?;
    let outcome =
        inputs::semantic_check(&check, source, object, directory, &|| control.cancelled())?;
    ensure!(
        before == inputs::checker_inputs(&check, directory)?,
        "semantic-check inputs changed during execution"
    );
    Ok(Some(outcome))
}
fn checked(value: &Option<CheckOutcome>) -> Option<Evaluation> {
    value.as_ref().map(|v| {
        if v.exit_code == Some(0) && !v.timed_out {
            known()
        } else {
            Evaluation {
                state: EvaluationState::Unsatisfied,
                reasons: vec![Reason {
                    code: EvidenceGap::MissingSemanticCheck,
                    detail: "configured semantic check failed".into(),
                }],
            }
        }
    })
}
fn known() -> Evaluation {
    Evaluation {
        state: EvaluationState::Satisfied,
        reasons: vec![],
    }
}
pub(super) fn input<'a>(
    saved: &'a Saved,
    report: &'a provenance::Report,
    source: &'a [u8],
    check: &Option<CheckOutcome>,
) -> Input<'a> {
    Input {
        compilation: &saved.compilation,
        provenance: report,
        source,
        semantic_check: checked(check),
    }
}
pub(super) fn final_mismatch(plan: &Plan, compilation: &Compilation) -> Result<Option<Value>> {
    let Some(target) = &plan.target else {
        return Ok(None);
    };
    let target_bytes = fs::read(&target.object)?;
    let extract = |bytes: &[u8], link: &str| -> Result<Vec<u8>> {
        let elf = elf::Elf::parse(bytes)?;
        let matches = elf
            .symbols()?
            .into_iter()
            .filter(|s| s.kind == 2 && s.name == link)
            .collect::<Vec<_>>();
        ensure!(matches.len() == 1, "target link is not unique");
        Ok(elf.function_bytes(&matches[0])?.to_vec())
    };
    let ours = extract(&compilation.object, &plan.contract.selection.link)?;
    let theirs = extract(&target_bytes, &target.link)?;
    let differences = (0..ours.len().max(theirs.len()))
        .filter(|&i| ours.get(i) != theirs.get(i))
        .count();
    Ok(Some(
        json!({"method":"function_bytes_and_symbolic_relocations","candidate_bytes":ours.len(),"target_bytes":theirs.len(),"differing_bytes":differences,
        "relocations_equal":state::relocations(&compilation.object,&plan.contract.selection.link)?==state::relocations(&target_bytes,&target.link)?,"target_sha256":state::digest(&target_bytes)}),
    ))
}
fn expand(
    plan: &Plan,
    progress: &mut Progress,
    parent: &Candidate,
    source: &[u8],
    output: &Path,
    name: &OsName,
) -> Result<()> {
    if parent.transformations.len() >= plan.maximum_depth {
        return Ok(());
    }
    for transformation in &plan.transformations {
        if parent.transformations.iter().any(|id| {
            plan.transformations
                .iter()
                .find(|t| &t.id == id)
                .is_some_and(|t| t.family == transformation.family)
        }) {
            continue;
        }
        let Ok(next) = apply(source, &transformation.edits) else {
            continue;
        };
        if progress.queue.len() >= plan.generation_limit {
            progress.generation_truncated = true;
            break;
        }
        let mut path = parent.transformations.clone();
        path.push(transformation.id.clone());
        progress.queue.push(Candidate {
            id: progress.queue.len(),
            parent: Some(parent.id),
            source_sha256: store_source(output, &next, name)?,
            transformations: path,
        });
    }
    Ok(())
}
/// Resume requires unchanged source/build context. Budgets apply to this call,
/// including setup and independent verification, and are saved before returning.
pub fn run(plan: &Plan, output: &Path, budget: &Budget, resume: bool) -> Result<Progress> {
    let began = Instant::now();
    let normalized = normalize(plan.clone(), &std::env::current_dir()?)?;
    let plan = &normalized;
    ensure!(budget.seconds > 0, "time budget must be positive");
    fs::create_dir_all(output)?;
    let output = output.canonicalize()?;
    let lock = OpenOptions::new()
        .read(true)
        .write(true)
        .create(true)
        .truncate(false)
        .open(output.join("run.lock"))?;
    lock.try_lock()
        .context("another source-search process owns this output")?;
    let base = request(plan)?;
    let original_source = base.working_directory.join(&base.source).canonicalize()?;
    let source = fs::read(&original_source)?;
    let name = OsName(
        original_source
            .file_name()
            .context("source has no filename")?
            .to_owned(),
    );
    let plan_sha = state::digest(&serde_json::to_vec(plan)?);
    let analysis_sha = analysis_identity(plan, &base)?;
    let analysis_inputs_sha = analysis_inputs_identity(plan, &base)?;
    let path = output.join("progress.json");
    let mut progress = if path.is_file() {
        ensure!(resume, "output already has a run; use --resume");
        let saved: Progress = serde_json::from_slice(&fs::read(&path)?)?;
        ensure!(
            saved.plan_sha256 == plan_sha
                && saved.baseline_source_sha256 == state::digest(&source)
                && saved.analysis_sha256 == analysis_sha,
            "plan/source/checker inputs/target changed; use a new run directory"
        );
        saved
    } else {
        ensure!(!resume, "no run to resume");
        atomic_json(&output.join("plan.json"), plan)?;
        let sha = store_source(&output, &source, &name)?;
        Progress {
            format_version: 1,
            plan_sha256: plan_sha,
            baseline_source_sha256: sha.clone(),
            baseline_input_sha256: None,
            original_input_sha256: None,
            analysis_sha256: analysis_sha,
            baseline_artifact: None,
            baseline_calibration: None,
            queue: vec![Candidate {
                id: 0,
                parent: None,
                source_sha256: sha,
                transformations: vec![],
            }],
            outcomes: vec![],
            next: 0,
            groups: BTreeMap::new(),
            ranking: vec![],
            status: "running".into(),
            generation_truncated: false,
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
    ensure!(
        !matches!(
            progress.status.as_str(),
            "verified_success" | "baseline_already_satisfied"
        ),
        "this run already finished successfully; independently replay its candidate or start a new run"
    );
    progress.status = "running".into();
    progress.error = None;
    atomic_json(&path, &progress)?;
    let work = (|| -> Result<()> {
        let baseline_path = source_path(&output, &progress.baseline_source_sha256, &name)?;
        let baseline_request = variant_request(&base, &baseline_path);
        let (baseline, baseline_key, hit) =
            artifact(&baseline_request, &output, &control, false, "baseline")?;
        if hit {
            progress.cache_hits += 1;
        }
        if let Some(old) = &progress.baseline_input_sha256 {
            ensure!(
                old == &baseline_key,
                "baseline build inputs changed; use a new run directory"
            );
        }
        progress.baseline_input_sha256 = Some(baseline_key.clone());
        progress.baseline_artifact = Some(baseline_key.clone());
        {
            let (original, original_key, hit) =
                artifact(&base, &output, &control, false, "original-baseline")?;
            if let Some(old) = &progress.original_input_sha256 {
                ensure!(
                    old == &original_key,
                    "original baseline inputs changed; use a new run directory"
                );
            }
            progress.original_input_sha256 = Some(original_key);
            if hit {
                progress.cache_hits += 1;
            }
            ensure!(
                original.compilation.check_status == runner::CheckStatus::Verified
                    && baseline.compilation.check_status == runner::CheckStatus::Verified,
                "baseline compiler output is not stock-equivalent"
            );
            ensure!(
                elf::comparable_sections(&original.compilation.object)?
                    == elf::comparable_sections(&baseline.compilation.object)?,
                "private source override changes baseline output/include resolution; adjust the build context"
            );
            progress.baseline_calibration = Some(known());
        }
        let baseline_report = provenance::parse(&baseline.compilation.trace)?;
        let baseline_object = output.join("cache").join(&baseline_key).join("object.o");
        let baseline_check = checker(
            plan,
            &baseline_path,
            &baseline_object,
            &baseline_path,
            &baseline_object,
            &base.working_directory,
            &control,
        )?;
        let mut evaluated = 0;
        while progress.next < progress.queue.len()
            && evaluated < budget.candidates
            && !control.cancelled()
        {
            let candidate = progress.queue[progress.next].clone();
            let candidate_source = source_path(&output, &candidate.source_sha256, &name)?;
            let raw = fs::read(&candidate_source)?;
            ensure!(
                state::digest(&raw) == candidate.source_sha256,
                "candidate source was modified"
            );
            let process = (|| -> Result<Outcome> {
                let (saved, key, cache_hit) = if candidate.id == 0 {
                    (
                        load_artifact(&output, &baseline_key)?,
                        baseline_key.clone(),
                        false,
                    )
                } else {
                    artifact(
                        &variant_request(&base, &candidate_source),
                        &output,
                        &control,
                        false,
                        &format!("candidate-{}", candidate.id),
                    )?
                };
                if cache_hit {
                    progress.cache_hits += 1;
                }
                let report = provenance::parse(&saved.compilation.trace)?;
                let object = output.join("cache").join(&key).join("object.o");
                let check = checker(
                    plan,
                    &candidate_source,
                    &object,
                    &baseline_path,
                    &baseline_object,
                    &base.working_directory,
                    &control,
                )?;
                let mut evaluation = objective::evaluate(
                    &plan.contract,
                    &input(&baseline, &baseline_report, &source, &baseline_check),
                    &input(&saved, &report, &raw, &check),
                )?;
                evaluation.final_target_mismatch = final_mismatch(plan, &saved.compilation)?;
                let fingerprint = state::graph(
                    &report,
                    &plan.contract.selection.link,
                    plan.contract.selection.class,
                    plan.contract.selection.round,
                )
                .and_then(|g| {
                    state::fingerprint(
                        g,
                        &report,
                        &saved.compilation.object,
                        saved.compilation.version,
                    )
                });
                let fingerprint_error = fingerprint.as_ref().err().map(|e| format!("{e:#}"));
                let fingerprint = fingerprint.ok();
                let duplicate = fingerprint
                    .as_ref()
                    .and_then(|f| progress.groups.get(f).and_then(|ids| ids.first().copied()));
                let mut independent = None;
                if candidate.id != 0 && evaluation.overall.state == EvaluationState::Satisfied {
                    fs::create_dir_all(output.join("verification"))?;
                    let directory = tempfile::Builder::new()
                        .prefix(&format!("candidate-{}-", candidate.id))
                        .tempdir_in(output.join("verification"))?
                        .keep();
                    let tag = directory.file_name().unwrap().to_string_lossy();
                    let (fresh, _, _) = artifact(
                        &variant_request(&base, &candidate_source),
                        &output,
                        &control,
                        true,
                        &tag,
                    )?;
                    let fresh_report = provenance::parse(&fresh.compilation.trace)?;
                    let fresh_object = directory.join("object.o");
                    let fresh_check = checker(
                        plan,
                        &candidate_source,
                        &fresh_object,
                        &baseline_path,
                        &baseline_object,
                        &base.working_directory,
                        &control,
                    )?;
                    let fresh_evaluation = objective::evaluate(
                        &plan.contract,
                        &input(&baseline, &baseline_report, &source, &baseline_check),
                        &input(&fresh, &fresh_report, &raw, &fresh_check),
                    )?;
                    let verified = fresh_evaluation.overall.state == EvaluationState::Satisfied
                        && fresh.compilation.object_sha256 == saved.compilation.object_sha256
                        && fresh.inputs == saved.inputs;
                    independent = Some(
                        json!({"satisfied":verified,"directory":directory,"object_sha256":fresh.compilation.object_sha256,"evaluation":fresh_evaluation,"semantic_check":fresh_check}),
                    );
                    if !verified {
                        let failed = Evaluation {
                            state: EvaluationState::Unsatisfied,
                            reasons: vec![Reason {
                                code: EvidenceGap::ObjectivePredicate,
                                detail: "candidate failed independent recompile verification"
                                    .into(),
                            }],
                        };
                        evaluation
                            .constraints
                            .insert("independent_recompile".into(), failed.clone());
                        evaluation.overall = failed;
                        evaluation.score.eligible = false;
                    }
                }
                Ok(Outcome {
                    id: candidate.id,
                    status: if candidate.id == 0
                        && evaluation.overall.state == EvaluationState::Satisfied
                    {
                        "baseline_already_satisfied"
                    } else if evaluation.overall.state == EvaluationState::Satisfied {
                        "verified_success"
                    } else {
                        "evaluated"
                    }
                    .into(),
                    compilation_cache_hit: cache_hit,
                    artifact: Some(key.clone()),
                    artifact_directory: Some(output.join("cache").join(&key)),
                    fingerprint,
                    fingerprint_error,
                    duplicate_state_of: duplicate,
                    evaluation: Some(evaluation),
                    semantic_check: check,
                    independent_verification: independent,
                    error: None,
                })
            })();
            let outcome = match process {
                Ok(outcome) => outcome,
                Err(e) if e.is::<BudgetReached>() || e.is::<runner::Cancelled>() => return Err(e),
                Err(e) => Outcome {
                    id: candidate.id,
                    status: "rejected".into(),
                    compilation_cache_hit: false,
                    artifact: None,
                    artifact_directory: None,
                    fingerprint: None,
                    fingerprint_error: None,
                    duplicate_state_of: None,
                    evaluation: None,
                    semantic_check: None,
                    independent_verification: None,
                    error: Some(format!("{e:#}")),
                },
            };
            ensure!(
                analysis_inputs_identity(plan, &base)? == analysis_inputs_sha,
                "checker inputs/target changed during source search"
            );
            if let Some(fingerprint) = &outcome.fingerprint {
                progress
                    .groups
                    .entry(fingerprint.clone())
                    .or_default()
                    .push(candidate.id);
            }
            atomic_json(
                &output
                    .join("candidates")
                    .join(format!("{}.json", candidate.id)),
                &outcome,
            )?;
            let success = outcome.status == "verified_success";
            let already = outcome.status == "baseline_already_satisfied";
            progress.outcomes.push(outcome);
            let mut ranked = progress
                .outcomes
                .iter()
                .filter_map(|o| o.evaluation.as_ref().map(|e| (o.id, &e.score)))
                .collect::<Vec<_>>();
            ranked.sort_by_key(|(id, score)| {
                (
                    !score.eligible,
                    score.objective_cost.unwrap_or(u32::MAX),
                    *id,
                )
            });
            progress.ranking = ranked.into_iter().map(|(id, _)| id).collect();
            progress.next += 1;
            evaluated += 1;
            // Compiler-state equivalence does not imply identical future syntax
            // transformations: expand every retained source representative.
            expand(plan, &mut progress, &candidate, &raw, &output, &name)?;
            atomic_json(&path, &progress)?;
            if success || already {
                progress.status = if success {
                    "verified_success"
                } else {
                    "baseline_already_satisfied"
                }
                .into();
                return Ok(());
            }
        }
        progress.status = if control.cancelled() {
            if budget.cancel_file.exists() {
                "cancelled"
            } else {
                "time_budget"
            }
        } else if progress.next >= progress.queue.len() {
            "bounded_search_exhausted"
        } else {
            "candidate_budget"
        }
        .into();
        Ok(())
    })();
    progress.compilations += control.compilations.get();
    match work {
        Ok(()) => {}
        Err(e) if e.is::<BudgetReached>() => progress.status = "compilation_budget".into(),
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
            atomic_json(&path, &progress)?;
            return Err(e);
        }
    }
    atomic_json(&path, &progress)?;
    Ok(progress)
}

pub(super) fn analysis_identity(plan: &Plan, base: &CompileRequest) -> Result<String> {
    Ok(state::digest(&serde_json::to_vec(
        &json!({"analyzer_sha256":inputs::analyzer_sha256()?,"check_inputs":plan.semantic_check.as_ref().map(|check|inputs::checker_inputs(check,&base.working_directory)).transpose()?,
        "target":plan.target.as_ref().map(|target|fs::read(&target.object).map(|bytes|state::digest(&bytes))).transpose()?}),
    )?))
}

/// Mutable analysis inputs are rechecked during a run without repeatedly
/// hashing the already-running analyzer executable.
pub(super) fn analysis_inputs_identity(plan: &Plan, base: &CompileRequest) -> Result<String> {
    Ok(state::digest(&serde_json::to_vec(
        &json!({"check_inputs":plan.semantic_check.as_ref().map(|check|inputs::checker_inputs(check,&base.working_directory)).transpose()?,
        "target":plan.target.as_ref().map(|target|fs::read(&target.object).map(|bytes|state::digest(&bytes))).transpose()?}),
    )?))
}

/// Replay uses the retained source bytes and performs fresh baseline, original
/// context calibration and candidate compiles. It never changes saved outcomes.
pub fn replay(output: &Path, id: usize, budget: &Budget) -> Result<Outcome> {
    let began = Instant::now();
    ensure!(budget.seconds > 0, "time budget must be positive");
    let output = output.canonicalize()?;
    let lock = OpenOptions::new()
        .read(true)
        .write(true)
        .open(output.join("run.lock"))?;
    lock.try_lock().context("another process owns this run")?;
    let plan = load_plan(&output.join("plan.json"))?;
    let progress: Progress = serde_json::from_slice(&fs::read(output.join("progress.json"))?)?;
    ensure!(
        progress.plan_sha256 == state::digest(&serde_json::to_vec(&plan)?),
        "saved plan was modified"
    );
    let candidate = progress
        .queue
        .get(id)
        .context("candidate ID is unavailable")?;
    ensure!(candidate.id == id, "saved candidate ID mismatch");
    let base = request(&plan)?;
    ensure!(
        analysis_identity(&plan, &base)? == progress.analysis_sha256,
        "analyzer/checker inputs/target changed; use a new run directory"
    );
    let analysis_inputs_sha = analysis_inputs_identity(&plan, &base)?;
    let old_candidate_key = progress
        .outcomes
        .iter()
        .find(|o| o.id == id)
        .and_then(|o| o.artifact.as_ref())
        .context("candidate has no completed capture to replay")?;
    let old_candidate = load_artifact(&output, old_candidate_key)?;
    let original = base.working_directory.join(&base.source).canonicalize()?;
    ensure!(
        state::digest(&fs::read(&original)?) == progress.baseline_source_sha256,
        "original source changed; restore inputs in an isolated checkout before replay"
    );
    let name = OsName(original.file_name().unwrap().to_owned());
    let baseline_source = source_path(&output, &progress.baseline_source_sha256, &name)?;
    let candidate_source = source_path(&output, &candidate.source_sha256, &name)?;
    let baseline_bytes = fs::read(&baseline_source)?;
    let candidate_bytes = fs::read(&candidate_source)?;
    ensure!(
        state::digest(&baseline_bytes) == progress.baseline_source_sha256
            && state::digest(&candidate_bytes) == candidate.source_sha256,
        "saved source was modified"
    );
    let old_key = progress
        .baseline_artifact
        .as_ref()
        .context("baseline was never captured")?;
    let old = load_artifact(&output, old_key)?;
    for file in old
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
    let (baseline, _, _) = artifact(
        &variant_request(&base, &baseline_source),
        &output,
        &control,
        true,
        &format!("{tag}-baseline"),
    )?;
    let (original, original_key, _) =
        artifact(&base, &output, &control, true, &format!("{tag}-original"))?;
    ensure!(
        baseline.inputs == old.inputs
            && Some(&original_key) == progress.original_input_sha256.as_ref(),
        "baseline replay build inputs changed"
    );
    ensure!(
        original.compilation.check_status == runner::CheckStatus::Verified
            && baseline.compilation.check_status == runner::CheckStatus::Verified
            && elf::comparable_sections(&original.compilation.object)?
                == elf::comparable_sections(&baseline.compilation.object)?,
        "replayed baseline context failed calibration"
    );
    let (saved, key, _) = artifact(
        &variant_request(&base, &candidate_source),
        &output,
        &control,
        true,
        &format!("{tag}-candidate"),
    )?;
    ensure!(
        saved.inputs == old_candidate.inputs,
        "candidate replay build inputs changed"
    );
    let baseline_object = output
        .join("verification")
        .join(format!("{tag}-baseline"))
        .join("object.o");
    let object = output
        .join("verification")
        .join(format!("{tag}-candidate"))
        .join("object.o");
    let baseline_check = checker(
        &plan,
        &baseline_source,
        &baseline_object,
        &baseline_source,
        &baseline_object,
        &base.working_directory,
        &control,
    )?;
    let check = checker(
        &plan,
        &candidate_source,
        &object,
        &baseline_source,
        &baseline_object,
        &base.working_directory,
        &control,
    )?;
    let a = provenance::parse(&baseline.compilation.trace)?;
    let b = provenance::parse(&saved.compilation.trace)?;
    let mut evaluation = objective::evaluate(
        &plan.contract,
        &input(&baseline, &a, &baseline_bytes, &baseline_check),
        &input(&saved, &b, &candidate_bytes, &check),
    )?;
    evaluation.final_target_mismatch = final_mismatch(&plan, &saved.compilation)?;
    let fingerprint = state::graph(
        &b,
        &plan.contract.selection.link,
        plan.contract.selection.class,
        plan.contract.selection.round,
    )
    .and_then(|g| state::fingerprint(g, &b, &saved.compilation.object, saved.compilation.version));
    let fingerprint_error = fingerprint.as_ref().err().map(|e| format!("{e:#}"));
    let fingerprint = fingerprint.ok();
    let outcome = Outcome {
        id,
        status: if evaluation.overall.state == EvaluationState::Satisfied {
            "replay_satisfied"
        } else {
            "replay_not_satisfied"
        }
        .into(),
        compilation_cache_hit: false,
        artifact: Some(key),
        artifact_directory: Some(object.parent().unwrap().to_owned()),
        fingerprint,
        fingerprint_error,
        duplicate_state_of: None,
        evaluation: Some(evaluation),
        semantic_check: check,
        independent_verification: Some(
            json!({"fresh_compilations":control.compilations.get(),"directory":directory}),
        ),
        error: None,
    };
    ensure!(
        analysis_inputs_identity(&plan, &base)? == analysis_inputs_sha,
        "checker inputs/target changed during source replay"
    );
    atomic_json(&directory.join("outcome.json"), &outcome)?;
    Ok(outcome)
}

#[cfg(test)]
mod checker_tests {
    use super::*;

    #[test]
    fn checker_mutation_is_rejected_and_analysis_inputs_detect_drift() {
        let scratch = tempfile::tempdir().unwrap();
        let directory = scratch.path().canonicalize().unwrap();
        let proof = directory.join("proof.txt");
        let target = directory.join("target.o");
        fs::write(&proof, b"original proof").unwrap();
        fs::write(&target, b"target bytes").unwrap();
        let script = directory.join(if cfg!(windows) {
            "checker.ps1"
        } else {
            "checker.sh"
        });
        let (program, mut arguments) = if cfg!(windows) {
            fs::write(
                &script,
                b"param([string]$Proof)\n[System.IO.File]::WriteAllText($Proof, 'changed proof')\n",
            )
            .unwrap();
            (
                "powershell.exe",
                vec![
                    "-NoProfile",
                    "-NonInteractive",
                    "-ExecutionPolicy",
                    "Bypass",
                    "-File",
                ],
            )
        } else {
            fs::write(&script, b"printf 'changed proof' > \"$1\"\n").unwrap();
            ("/bin/sh", vec![])
        };
        let script_name = script.to_str().unwrap();
        let proof_name = proof.to_str().unwrap();
        arguments.extend([script_name, proof_name]);
        let mut plan: Plan = serde_json::from_value(json!({
            "format_version":1,
            "build":{"oracle_root":directory,"working_directory":directory,"source":"source.cpp","version":"GC_2_7","flags":[],"from_ninja":null,"timeout_seconds":30},
            "contract":{"format_version":1,"selection":{"link":"example","class":4,"round":0},"objective":{"kind":"symbol_address_pairs","symbol":"symbol","minimum":0,"maximum":0},"relocations":[],"purpose":"mechanism_reproducer"},
            "transformations":[],"maximum_depth":0,"generation_limit":1,
            "semantic_check":{"program":program,"arguments":arguments,"inputs":[proof],"timeout_seconds":30},
            "target":{"object":target,"link":"example"}
        })).unwrap();
        let base = request(&plan).unwrap();
        let before = analysis_inputs_identity(&plan, &base).unwrap();
        let budget = Budget {
            candidates: 1,
            compilations: 0,
            seconds: 60,
            cancel_file: directory.join("CANCEL"),
        };
        let control = Control {
            budget: &budget,
            began: Instant::now(),
            compilations: Cell::new(0),
        };
        let error = checker(
            &plan, &proof, &target, &proof, &target, &directory, &control,
        )
        .unwrap_err();
        assert!(format!("{error:#}").contains("inputs changed during execution"));
        assert_ne!(analysis_inputs_identity(&plan, &base).unwrap(), before);
        fs::write(&proof, b"original proof").unwrap();
        assert_eq!(analysis_inputs_identity(&plan, &base).unwrap(), before);
        fs::write(&target, b"different target").unwrap();
        assert_ne!(analysis_inputs_identity(&plan, &base).unwrap(), before);
        plan.semantic_check = None;
        assert!(
            checker(
                &plan, &proof, &target, &proof, &target, &directory, &control
            )
            .unwrap()
            .is_none()
        );
    }
}
