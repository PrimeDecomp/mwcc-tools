//! The provenance and source-guided methods, on traces, captures and run directories on disk: `provenance.inspect`
//! and `provenance.compare`, `source_search.*` (run, replay, state, evaluate), `source_reduce.*` (run, replay),
//! `source_index.*` (ingest, origins, query, verify), `source_repro.*` (export, verify, replay), `source_arenas.*`
//! (inspect, resolve, resolve_many) and `source_nodes.*` (inspect, snapshot, write, clone). Their results are the
//! tools' JSON documents. A cancel of a run writes its cancel file, which the run polls.
use crate::{Call, Method};
use anyhow::{Context, Result, bail, ensure};
use mwcc_oracle_core::source_tree::read_json;
use mwcc_oracle_core::{
    evidence::EvaluationState,
    provenance,
    runner::Compilation,
    source_arenas,
    source_guided::{
        correspondence::{self, Boundary, Selection},
        index::{self, Ingest, Query},
        objective::{self, Contract, Input},
        reduction,
        reproducer::{self, RunKind},
        search::{self, Budget},
        state,
    },
    source_lifetimes::PoolDomain,
    source_nodes,
    version::Version,
};
use schemars::JsonSchema;
use serde::{Deserialize, Serialize};
use serde_json::Value;
use std::{
    fs,
    path::{Path, PathBuf},
    sync::atomic::{AtomicBool, Ordering},
    time::Duration,
};

/// A tool's JSON document, and (for the ones that printed text) its lines.
#[derive(Debug, Serialize, JsonSchema)]
pub struct Document {
    pub document: Value,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub text: Option<Vec<String>>,
}

fn document(value: impl Serialize) -> Result<Document> {
    Ok(Document {
        document: serde_json::to_value(value)?,
        text: None,
    })
}

/// RUN, with CANCEL_FILE written when the call is cancelled (the source-guided runs poll it).
fn cancellable<T>(
    call: &Call<'_>,
    cancel_file: &Path,
    run: impl FnOnce() -> Result<T>,
) -> Result<T> {
    let done = AtomicBool::new(false);
    std::thread::scope(|scope| {
        scope.spawn(|| {
            while !done.load(Ordering::Relaxed) {
                if (call.cancel)() {
                    let _ = fs::write(cancel_file, b"cancelled\n");
                    break;
                }
                std::thread::sleep(Duration::from_millis(100));
            }
        });
        let result = run();
        done.store(true, Ordering::Relaxed);
        result
    })
}

/// A raw trace, or a capture's (`trace`, or `compilation.trace`).
fn load_trace(path: &Path) -> Result<String> {
    let input = fs::read_to_string(path).with_context(|| format!("read {}", path.display()))?;
    if !input.trim_start().starts_with('{') {
        return Ok(input);
    }
    let value: Value = serde_json::from_str(&input)?;
    value["compilation"]["trace"]
        .as_str()
        .or(value["trace"].as_str())
        .map(String::from)
        .context("the capture has no trace")
}

/// A capture's compilation (`compilation`, or the capture itself), with its object read from OBJECT.
fn load_compilation(capture: &Path, object: &Path) -> Result<Compilation> {
    let mut value = read_json(capture)?;
    let selected = value
        .get_mut("compilation")
        .map(Value::take)
        .unwrap_or(value);
    let mut compilation: Compilation = serde_json::from_value(selected)?;
    compilation.object = fs::read(object).with_context(|| format!("read {}", object.display()))?;
    Ok(compilation)
}

#[derive(Debug, Default, Deserialize, JsonSchema)]
#[serde(deny_unknown_fields)]
pub struct InspectParams {
    /// A raw trace, or a compile capture's JSON.
    pub trace_path: PathBuf,
    /// Only this function's (mangled name).
    pub link: Option<String>,
    #[serde(default)]
    pub text: bool,
}

fn inspect_text(report: &provenance::Report) -> Vec<String> {
    let mut out = vec![];
    for function in &report.history.functions {
        let reused = function
            .registers
            .iter()
            .filter(|r| r.values.len() > 1)
            .count();
        out.push(format!(
            "{}: function {}, {} births, {} reused register numbers, direct-counter observations {:?}",
            function.link,
            function.id,
            function.births.len(),
            reused,
            function.counter_observations.state
        ));
    }
    for value in &report.scalarization.backend_values {
        if let Some(element) = &value.array_element {
            out.push(format!(
                "  source {}[{}] -> class {} register {}, frontend pass {:?}/binding {:?}, observations {:?}",
                String::from_utf8_lossy(&element.name),
                element.element_index,
                value.class,
                value.register,
                value.pass,
                value.binding,
                value.observations.state
            ));
        } else if value.observations.state != EvaluationState::Satisfied {
            out.push(format!(
                "  register {}: incomplete frontend scalarization binding",
                value.register
            ));
        }
    }
    for call in &report.call_arguments.calls {
        out.push(format!(
            "  call {} to {}: {} lowered argument contexts, {} episodes, observations {:?}",
            call.id,
            call.callee
                .name_text
                .as_deref()
                .filter(|_| call.callee.name_known && call.callee.name_complete)
                .unwrap_or("<unresolved callee>"),
            call.arguments.len(),
            call.episodes.len(),
            call.observations.state
        ));
    }
    for graph in &report.graphs.graphs {
        out.push(format!(
            "{}: graph {}, class {}, {} nodes, {} absent from current instructions, observations {:?}",
            graph.link,
            graph.id,
            graph.class,
            graph.nodes.len(),
            graph.graph_only.len(),
            graph.observations.state
        ));
    }
    for pass in &report.passes {
        out.push(format!(
            "{}: pass {}, class {}, {} candidates, {}",
            pass.link,
            pass.id,
            pass.class,
            pass.candidates.len(),
            if pass.decisions.state == EvaluationState::Satisfied {
                "decisions complete"
            } else {
                "incomplete evidence"
            }
        ));
        for candidate in &pass.candidates {
            let explanation = match &candidate.decision {
                provenance::Decision::Rejected { condition } => format!("rejected: {condition}"),
                provenance::Decision::AlreadySameRoot { root } => {
                    format!("already the same root ({root}); copy unlinked")
                }
                provenance::Decision::Merged { parent, child } => {
                    format!("merged {child} into {parent}; parent write confirmed")
                }
                provenance::Decision::Indeterminate => "missing deciding observation".into(),
            };
            out.push(format!(
                "  {} @ {}: {}{}",
                candidate.id,
                candidate.instruction,
                explanation,
                if candidate.evidence.state == EvaluationState::Satisfied {
                    ""
                } else {
                    " (incomplete evidence)"
                }
            ));
        }
    }
    out.push("Complete lifecycle/category/leaf-origin coverage: incomplete".into());
    out
}

fn provenance_inspect(_: &mut Call<'_>, params: InspectParams) -> Result<Document> {
    let input = fs::read_to_string(&params.trace_path)
        .with_context(|| format!("read {}", params.trace_path.display()))?;
    let mut report = provenance::inspect(&input)?;
    if let Some(link) = &params.link {
        report.passes.retain(|p| p.link == *link);
        report.history.functions.retain(|f| f.link == *link);
        report.graphs.graphs.retain(|g| g.link == *link);
        report.graphs.functions.retain(|f| f.link == *link);
        report.scalarization.passes.retain(|p| p.link == *link);
        let functions: Vec<_> = report.history.functions.iter().map(|f| f.id).collect();
        report
            .scalarization
            .backend_values
            .retain(|value| functions.contains(&value.value.function));
        report
            .call_arguments
            .calls
            .retain(|call| functions.contains(&call.function));
        report
            .call_arguments
            .functions
            .retain(|summary| functions.contains(&summary.function));
        ensure!(
            !report.passes.is_empty()
                || !report.history.functions.is_empty()
                || !report.scalarization.passes.is_empty(),
            "no provenance for {link}"
        );
        // (a view: the report-wide coverage stays conservative)
    }
    Ok(Document {
        text: params.text.then(|| inspect_text(&report)),
        document: serde_json::to_value(&report)?,
    })
}

#[derive(Debug, Default, Deserialize, JsonSchema)]
#[serde(deny_unknown_fields)]
pub struct CompareParams {
    /// Two raw traces or compile captures.
    pub first: PathBuf,
    pub second: PathBuf,
    /// The function (mangled), and the second's when it differs.
    pub link: String,
    pub second_link: Option<String>,
    /// The register class (default 4) and each side's colouring round (default 0).
    pub class: Option<u8>,
    pub first_round: Option<usize>,
    pub second_round: Option<usize>,
    /// The compiler version, required for raw traces.
    pub version: Option<String>,
    #[serde(default)]
    pub text: bool,
}

fn captured_version(input: &str) -> Result<Option<Version>> {
    if !input.trim_start().starts_with('{') {
        return Ok(None);
    }
    let value: Value = serde_json::from_str(input)?;
    value
        .get("compilation")
        .and_then(|c| c.get("version"))
        .or_else(|| value.get("version"))
        .and_then(Value::as_str)
        .map(Version::parse)
        .transpose()
}

fn provenance_compare(_: &mut Call<'_>, params: CompareParams) -> Result<Document> {
    let first = fs::read_to_string(&params.first)
        .with_context(|| format!("read {}", params.first.display()))?;
    let second = fs::read_to_string(&params.second)
        .with_context(|| format!("read {}", params.second.display()))?;
    let a = captured_version(&first)?;
    let b = captured_version(&second)?;
    ensure!(
        !matches!((a, b), (Some(a), Some(b)) if a != b),
        "cross-version graph comparisons are unsupported"
    );
    let version = match params.version.as_deref().map(Version::parse).transpose()? {
        Some(v) => {
            ensure!(
                a.is_none_or(|a| a == v) && b.is_none_or(|b| b == v),
                "`version` disagrees with the compiler capture"
            );
            v
        }
        None => {
            ensure!(
                a.is_some() && a == b,
                "raw traces, or captures of missing or different compilers, need `version`"
            );
            a.unwrap()
        }
    };
    let class = params.class.unwrap_or(4);
    let selection = |link: &str, round| Selection {
        link: link.to_owned(),
        class,
        round,
        boundary: Boundary::BeforeSimplify,
    };
    let report = correspondence::compare(
        &provenance::inspect(&first)?,
        &selection(&params.link, params.first_round.unwrap_or(0)),
        &provenance::inspect(&second)?,
        &selection(
            params.second_link.as_deref().unwrap_or(&params.link),
            params.second_round.unwrap_or(0),
        ),
        version,
    )?;
    let text = params.text.then(|| {
        vec![
            format!(
                "Observed before_simplify graphs: {} nodes -> {} nodes; capture {:?}",
                report.first.nodes.len(),
                report.second.nodes.len(),
                report.coverage.state
            ),
            format!(
                "{} conditional injective pairs, {} unmatched first, {} unmatched second, {} conflicts",
                report.mapping.len(),
                report.unmatched_first.len(),
                report.unmatched_second.len(),
                report.conflicts.len()
            ),
            format!(
                "{} mapped neighbor multiplicity differences; {} first and {} second unclassified boundary edges",
                report.neighbor_differences.len(),
                report.boundary_first.len(),
                report.boundary_second.len()
            ),
            "Source/symbol anchors and instruction votes are conditional. Semantic lifetime correspondence and elimination remain indeterminate.".into(),
        ]
    });
    Ok(Document {
        document: serde_json::to_value(&report)?,
        text,
    })
}

/// A source-guided run's limits.
#[derive(Debug, Default, Deserialize, JsonSchema)]
#[serde(deny_unknown_fields)]
pub struct RunParams {
    /// The plan's JSON file.
    pub plan: PathBuf,
    /// The run's directory (new, or one to resume).
    pub output: PathBuf,
    #[serde(default)]
    pub resume: bool,
    /// The limits (default 100 candidates, 100 compiles, 600 seconds).
    pub candidates: Option<usize>,
    pub compilations: Option<usize>,
    pub seconds: Option<u64>,
    /// The file whose presence stops the run (default OUTPUT/CANCEL).
    pub cancel_file: Option<PathBuf>,
}

impl RunParams {
    fn budget(&self) -> Budget {
        Budget {
            candidates: self.candidates.unwrap_or(100),
            compilations: self.compilations.unwrap_or(100),
            seconds: self.seconds.unwrap_or(600),
            cancel_file: self
                .cancel_file
                .clone()
                .unwrap_or_else(|| self.output.join("CANCEL")),
        }
    }
}

#[derive(Debug, Default, Deserialize, JsonSchema)]
#[serde(deny_unknown_fields)]
pub struct ReplayParams {
    /// A run's directory, and its candidate's ID.
    pub output: PathBuf,
    pub id: u64,
}

fn replay_budget(output: &Path) -> Budget {
    Budget {
        candidates: 1,
        compilations: 3,
        seconds: 600,
        cancel_file: output.join("CANCEL"),
    }
}

fn search_run(call: &mut Call<'_>, params: RunParams) -> Result<Document> {
    let plan = search::load_plan(&params.plan)?;
    let budget = params.budget();
    let report = cancellable(call, &budget.cancel_file, || {
        search::run(&plan, &params.output, &budget, params.resume)
    })?;
    fs::write(
        params.output.join("last-status.json"),
        serde_json::to_vec_pretty(&report)?,
    )?;
    document(report)
}

fn search_replay(call: &mut Call<'_>, params: ReplayParams) -> Result<Document> {
    let budget = replay_budget(&params.output);
    document(cancellable(call, &budget.cancel_file, || {
        search::replay(&params.output, params.id.try_into()?, &budget)
    })?)
}

#[derive(Debug, Default, Deserialize, JsonSchema)]
#[serde(deny_unknown_fields)]
pub struct StateParams {
    /// A compile capture's JSON and its object.
    pub capture: PathBuf,
    pub object: PathBuf,
    /// The function (mangled), the register class (default 4) and the colouring round (default 0).
    pub link: String,
    pub class: Option<u8>,
    pub round: Option<usize>,
}

fn search_state(_: &mut Call<'_>, params: StateParams) -> Result<Document> {
    let compilation = load_compilation(&params.capture, &params.object)?;
    document(state::inspect(
        &compilation,
        &params.link,
        params.class.unwrap_or(4),
        params.round.unwrap_or(0),
    )?)
}

#[derive(Debug, Default, Deserialize, JsonSchema)]
#[serde(deny_unknown_fields)]
pub struct EvaluateParams {
    /// The contract's JSON, and the candidate: its capture, source and object.
    pub contract: PathBuf,
    pub capture: PathBuf,
    pub source: PathBuf,
    pub object: PathBuf,
    /// The baseline's capture, source and object (all three, or none: the candidate against itself).
    pub baseline_capture: Option<PathBuf>,
    pub baseline_source: Option<PathBuf>,
    pub baseline_object: Option<PathBuf>,
}

fn search_evaluate(_: &mut Call<'_>, params: EvaluateParams) -> Result<Document> {
    let contract: Contract = serde_json::from_value(read_json(&params.contract)?)?;
    let load = |capture: &Path, source: &Path, object: &Path| -> Result<_> {
        let compilation = load_compilation(capture, object)?;
        let report = provenance::parse(&compilation.trace)?;
        Ok((
            compilation,
            fs::read(source).with_context(|| format!("read {}", source.display()))?,
            report,
        ))
    };
    let candidate = load(&params.capture, &params.source, &params.object)?;
    let baseline = match (
        &params.baseline_capture,
        &params.baseline_source,
        &params.baseline_object,
    ) {
        (Some(c), Some(s), Some(o)) => Some(load(c, s, o)?),
        (None, None, None) => None,
        _ => bail!("baseline_capture, baseline_source and baseline_object go together"),
    };
    let original = baseline.as_ref().unwrap_or(&candidate);
    document(objective::evaluate(
        &contract,
        &Input {
            compilation: &original.0,
            source: &original.1,
            provenance: &original.2,
            semantic_check: None,
        },
        &Input {
            compilation: &candidate.0,
            source: &candidate.1,
            provenance: &candidate.2,
            semantic_check: None,
        },
    )?)
}

fn reduce_run(call: &mut Call<'_>, params: RunParams) -> Result<Document> {
    let plan = reduction::load_plan(&params.plan)?;
    let budget = params.budget();
    document(cancellable(call, &budget.cancel_file, || {
        reduction::run(&plan, &params.output, &budget, params.resume)
    })?)
}

fn reduce_replay(call: &mut Call<'_>, params: ReplayParams) -> Result<Document> {
    let budget = replay_budget(&params.output);
    document(cancellable(call, &budget.cancel_file, || {
        reduction::replay(&params.output, params.id.try_into()?, &budget)
    })?)
}

#[derive(Debug, Default, Deserialize, JsonSchema)]
#[serde(deny_unknown_fields)]
pub struct IngestParams {
    /// The ingest specification's JSON (its relative paths are from its directory), and the index's directory.
    pub spec: PathBuf,
    pub index: PathBuf,
}

fn rebase(path: &mut PathBuf, base: &Path) {
    if !path.is_absolute() {
        *path = base.join(&*path);
    }
}

fn index_ingest(_: &mut Call<'_>, params: IngestParams) -> Result<Document> {
    let mut ingest: Ingest = serde_json::from_value(read_json(&params.spec)?)?;
    let spec = params.spec.canonicalize()?;
    let base = spec
        .parent()
        .context("the specification has no directory")?;
    for path in [
        &mut ingest.capture,
        &mut ingest.object,
        &mut ingest.target,
        &mut ingest.source,
        &mut ingest.build.oracle_root,
        &mut ingest.build.working_directory,
    ] {
        rebase(path, base);
    }
    if let Some(path) = &mut ingest.snapshot {
        rebase(path, base);
    }
    if let Some(path) = &mut ingest.build.from_ninja {
        rebase(path, base);
    }
    for function in &mut ingest.functions {
        if let Some(file) = &mut function.source_file {
            rebase(&mut file.path, base);
        }
    }
    document(index::ingest(&params.index, &ingest)?)
}

#[derive(Debug, Default, Deserialize, JsonSchema)]
#[serde(deny_unknown_fields)]
pub struct OriginsParams {
    pub index: PathBuf,
    pub link: Option<String>,
    pub version: Option<String>,
}

fn index_origins(_: &mut Call<'_>, params: OriginsParams) -> Result<Document> {
    let filter = index::OriginFilter {
        link: params.link,
        version: params.version.as_deref().map(Version::parse).transpose()?,
        ..index::OriginFilter::default()
    };
    document(index::origins(&params.index, &filter)?)
}

#[derive(Debug, Default, Deserialize, JsonSchema)]
#[serde(deny_unknown_fields)]
pub struct QueryParams {
    /// The query's JSON file, and the index's directory.
    pub query: PathBuf,
    pub index: PathBuf,
}

fn index_query(_: &mut Call<'_>, params: QueryParams) -> Result<Document> {
    let query: Query = serde_json::from_value(read_json(&params.query)?)?;
    document(index::query(&params.index, &query)?)
}

#[derive(Debug, Default, Deserialize, JsonSchema)]
#[serde(deny_unknown_fields)]
pub struct VerifyParams {
    pub query: PathBuf,
    /// The entry to verify.
    pub id: String,
    pub index: PathBuf,
    /// The verification's directory.
    pub output: PathBuf,
    pub seconds: Option<u64>,
    pub cancel_file: Option<PathBuf>,
}

fn index_verify(call: &mut Call<'_>, params: VerifyParams) -> Result<Document> {
    let query: Query = serde_json::from_value(read_json(&params.query)?)?;
    let cancel = params
        .cancel_file
        .clone()
        .unwrap_or_else(|| params.output.join("CANCEL"));
    document(cancellable(call, &cancel, || {
        index::verify(
            &params.index,
            &params.id,
            &query,
            &params.output,
            params.seconds.unwrap_or(600),
            &cancel,
        )
    })?)
}

#[derive(Debug, Default, Deserialize, JsonSchema)]
#[serde(deny_unknown_fields)]
pub struct ExportParams {
    /// `search` or `reduction`: the kind of run.
    pub kind: String,
    /// The run's directory and its candidate's ID.
    pub run: PathBuf,
    pub id: u64,
    /// The new bundle's directory.
    pub output: PathBuf,
    pub run_analyzer: Option<PathBuf>,
}

fn repro_export(_: &mut Call<'_>, params: ExportParams) -> Result<Document> {
    let kind = match params.kind.as_str() {
        "search" => RunKind::Search,
        "reduction" => RunKind::Reduction,
        other => bail!("kind: search or reduction, not {other}"),
    };
    document(reproducer::export(
        kind,
        &params.run,
        params.id.try_into()?,
        &params.output,
        params.run_analyzer.as_deref(),
    )?)
}

#[derive(Debug, Default, Deserialize, JsonSchema)]
#[serde(deny_unknown_fields)]
pub struct BundleParams {
    pub bundle: PathBuf,
    /// The manifest's SHA-256 the bundle must have.
    pub expected_sha256: Option<String>,
}

fn repro_verify(_: &mut Call<'_>, params: BundleParams) -> Result<Document> {
    let result = reproducer::verify(&params.bundle)?;
    if let Some(expected) = &params.expected_sha256 {
        ensure!(
            *expected == result.manifest_sha256,
            "the bundle's manifest does not match the expected identity"
        );
    }
    document(result)
}

#[derive(Debug, Default, Deserialize, JsonSchema)]
#[serde(deny_unknown_fields)]
pub struct BundleReplayParams {
    pub bundle: PathBuf,
    /// The replay's directory (new).
    pub output: PathBuf,
    pub seconds: Option<u64>,
    pub compilations: Option<usize>,
    pub cancel_file: Option<PathBuf>,
}

fn repro_replay(call: &mut Call<'_>, params: BundleReplayParams) -> Result<Document> {
    let budget = Budget {
        candidates: 1,
        compilations: params.compilations.unwrap_or(3),
        seconds: params.seconds.unwrap_or(600),
        cancel_file: params
            .cancel_file
            .clone()
            .unwrap_or_else(|| params.output.with_extension("CANCEL")),
    };
    document(cancellable(call, &budget.cancel_file, || {
        reproducer::replay(&params.bundle, &params.output, &budget)
    })?)
}

#[derive(Debug, Default, Deserialize, JsonSchema)]
#[serde(deny_unknown_fields)]
pub struct CaptureParams {
    /// A raw trace, or a compile capture's JSON.
    pub capture: PathBuf,
}

fn arenas_inspect(_: &mut Call<'_>, params: CaptureParams) -> Result<Document> {
    document(source_arenas::parse(&load_trace(&params.capture)?)?)
}

#[derive(Debug, Default, Deserialize, JsonSchema)]
#[serde(deny_unknown_fields)]
pub struct ResolveParams {
    pub capture: PathBuf,
    /// The trace line (one-based), the pool (heap, optimizer, block, local or global), the address (hex) and the
    /// extent.
    pub position: usize,
    pub pool: String,
    pub address: String,
    pub extent: u32,
}

fn arenas_resolve(_: &mut Call<'_>, params: ResolveParams) -> Result<Document> {
    let pool = match params.pool.as_str() {
        "heap" => PoolDomain::Heap,
        "optimizer" => PoolDomain::Optimizer,
        "block" => PoolDomain::Block,
        "local" => PoolDomain::Local,
        "global" => PoolDomain::Global,
        other => bail!("unknown arena pool {other}"),
    };
    let history = source_arenas::parse(&load_trace(&params.capture)?)?;
    document(history.resolve_at(
        params.position,
        pool,
        mwcc_oracle_core::executable::hex(&params.address)?,
        params.extent,
    ))
}

#[derive(Debug, Default, Deserialize, JsonSchema)]
#[serde(deny_unknown_fields)]
pub struct ResolveManyParams {
    pub capture: PathBuf,
    /// The queries (source_arenas' `Query`s), or a JSON file of them.
    #[serde(default)]
    pub queries: Vec<Value>,
    pub queries_path: Option<PathBuf>,
}

fn arenas_resolve_many(_: &mut Call<'_>, params: ResolveManyParams) -> Result<Document> {
    let queries: Vec<source_arenas::Query> = match &params.queries_path {
        Some(path) => serde_json::from_value(read_json(path)?)?,
        None => serde_json::from_value(Value::Array(params.queries))?,
    };
    let history = source_arenas::parse(&load_trace(&params.capture)?)?;
    document(history.prepare().resolve_many(&queries))
}

fn nodes_inspect(_: &mut Call<'_>, params: CaptureParams) -> Result<Document> {
    document(source_nodes::parse(&load_trace(&params.capture)?)?)
}

#[derive(Debug, Default, Deserialize, JsonSchema)]
#[serde(deny_unknown_fields)]
pub struct NodeParams {
    pub capture: PathBuf,
    /// The node.
    pub id: u32,
}

fn nodes_snapshot(_: &mut Call<'_>, params: NodeParams) -> Result<Document> {
    let history = source_nodes::parse(&load_trace(&params.capture)?)?;
    document(history.prepare().snapshot(params.id))
}

fn nodes_write(_: &mut Call<'_>, params: NodeParams) -> Result<Document> {
    let history = source_nodes::parse(&load_trace(&params.capture)?)?;
    document(history.prepare().source_write(params.id))
}

fn nodes_clone(_: &mut Call<'_>, params: NodeParams) -> Result<Document> {
    let history = source_nodes::parse(&load_trace(&params.capture)?)?;
    document(history.prepare().clone_edge(params.id))
}

pub(crate) fn methods() -> Vec<Method> {
    use crate::method;
    vec![
        method!(
            "provenance.inspect",
            "the provenance report of a trace or capture: value births and reuse, scalarization, call arguments, interference graphs and coalescing decisions (`link` selects a function)",
            InspectParams,
            Document,
            provenance_inspect
        ),
        method!(
            "provenance.compare",
            "the correspondence of a function's before_simplify interference graphs in two traces or captures",
            CompareParams,
            Document,
            provenance_compare
        ),
        method!(
            "source_search.run",
            "a source-guided search plan run in a directory (or resumed), within its limits",
            RunParams,
            Document,
            search_run
        ),
        method!(
            "source_search.replay",
            "a source-search candidate compiled again from its run directory",
            ReplayParams,
            Document,
            search_replay
        ),
        method!(
            "source_search.state",
            "a capture's colouring state for a function, class and round",
            StateParams,
            Document,
            search_state
        ),
        method!(
            "source_search.evaluate",
            "a candidate capture evaluated against a contract (and a baseline)",
            EvaluateParams,
            Document,
            search_evaluate
        ),
        method!(
            "source_reduce.run",
            "a source reduction plan run in a directory (or resumed), within its limits",
            RunParams,
            Document,
            reduce_run
        ),
        method!(
            "source_reduce.replay",
            "a reduction candidate compiled again from its run directory",
            ReplayParams,
            Document,
            reduce_replay
        ),
        method!(
            "source_index.ingest",
            "a capture's functions added to a source index",
            IngestParams,
            Document,
            index_ingest
        ),
        method!(
            "source_index.origins",
            "the origins a source index holds (by link and version)",
            OriginsParams,
            Document,
            index_origins
        ),
        method!(
            "source_index.query",
            "a source index's entries matching a query",
            QueryParams,
            Document,
            index_query
        ),
        method!(
            "source_index.verify",
            "an index entry verified by compiling it",
            VerifyParams,
            Document,
            index_verify
        ),
        method!(
            "source_repro.export",
            "a search or reduction candidate exported as a reproducer bundle",
            ExportParams,
            Document,
            repro_export
        ),
        method!(
            "source_repro.verify",
            "a reproducer bundle's files checked against its manifest",
            BundleParams,
            Document,
            repro_verify
        ),
        method!(
            "source_repro.replay",
            "a reproducer bundle compiled again",
            BundleReplayParams,
            Document,
            repro_replay
        ),
        method!(
            "source_arenas.inspect",
            "the source arena history of a trace or capture",
            CaptureParams,
            Document,
            arenas_inspect
        ),
        method!(
            "source_arenas.resolve",
            "what a pool address held at a trace position",
            ResolveParams,
            Document,
            arenas_resolve
        ),
        method!(
            "source_arenas.resolve_many",
            "source_arenas.resolve for many queries on one prepared history",
            ResolveManyParams,
            Document,
            arenas_resolve_many
        ),
        method!(
            "source_nodes.inspect",
            "the frontend node history of a trace or capture",
            CaptureParams,
            Document,
            nodes_inspect
        ),
        method!(
            "source_nodes.snapshot",
            "a frontend node as it stood",
            NodeParams,
            Document,
            nodes_snapshot
        ),
        method!(
            "source_nodes.write",
            "the source write that made a frontend node",
            NodeParams,
            Document,
            nodes_write
        ),
        method!(
            "source_nodes.clone",
            "the clone edge of a frontend node",
            NodeParams,
            Document,
            nodes_clone
        ),
    ]
}
