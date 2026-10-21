//! The colouring methods, on a loaded trace (`trace.load`: a unit's traced compile, or a saved trace, kept by the
//! session and named by its handle): `trace.verify` (each colouring replayed against the trace), `trace.target`
//! (each vreg's register in the target), `trace.force` (the claims ordered by their targets), `trace.replay` and
//! `trace.replay_many` (the colouring with edits of its graph: order, move, drop, degree, neighbours, link,
//! unlink), `trace.search` (edits that give the target's colours) and `trace.remap` (two traces' graphs paired).
use crate::{
    Call, Method, Timing,
    oracle::trace_unit,
    unit::{UnitRef, resolve},
};
use anyhow::{Context, Result, bail, ensure};
use mwcc_oracle_core::{
    colouring::{
        self, Edit,
        correspondence::{self, Selection, SnapshotPhase},
        search::{self, Options, Problem},
        target,
    },
    cse, ppc,
    release::sha256,
    runner::TraceOptions,
    trace::{self, Allocation, Round, Snapshot, Trace},
    version::{Version, class_name},
};
use schemars::JsonSchema;
use serde::{Deserialize, Serialize};
use serde_json::{Value, json};
use std::{
    collections::{BTreeMap, BTreeSet},
    path::PathBuf,
    sync::Arc,
};

/// A trace the session keeps: its records, its object, the target's asm, and its compiler's version.
pub(crate) struct LoadedTrace {
    pub trace: Trace,
    pub object: Option<Vec<u8>>,
    pub version: Version,
    pub source: Option<String>,
    /// The source's text as it was traced (the overlay's, or the disk's then; a saved trace's PREFIX.source).
    pub text: Option<String>,
    pub target_asm: Option<String>,
}

#[derive(Debug, Default, Deserialize, JsonSchema)]
#[serde(deny_unknown_fields)]
pub struct LoadParams {
    /// A unit, traced through `files` (default: its whole trace; `function` keeps one function's records)...
    pub handle: Option<String>,
    pub project: Option<PathBuf>,
    pub unit: Option<String>,
    #[serde(default)]
    pub files: BTreeMap<String, Option<String>>,
    pub inline_max_size: Option<u32>,
    pub function: Option<String>,
    /// ... or a saved trace: PREFIX.trace (with PREFIX.o and PREFIX.manifest.json when there), and its version.
    pub load: Option<String>,
    pub version: Option<String>,
    /// The target's asm (default: the unit's, beside its target object).
    pub target_asm: Option<PathBuf>,
    /// Save the unit's trace as PREFIX.trace, PREFIX.o, PREFIX.source (the text traced) and PREFIX.manifest.json.
    pub save: Option<String>,
    pub timeout_ms: Option<u64>,
}

#[derive(Debug, Serialize, JsonSchema)]
pub struct TracedFunction {
    pub function: String,
    pub link: String,
}

#[derive(Debug, Serialize, JsonSchema)]
pub struct LoadResult {
    /// The handle the colouring methods name it by.
    pub trace: String,
    pub version: String,
    pub source: Option<String>,
    /// The functions allocated, in order.
    pub functions: Vec<TracedFunction>,
    pub has_target_asm: bool,
}

/// PREFIX.trace, PREFIX.o, PREFIX.source and PREFIX.manifest.json, as `save` wrote them.
fn load_saved(prefix: &str, version: Option<Version>) -> Result<LoadedTrace> {
    let text = std::fs::read_to_string(format!("{prefix}.trace"))
        .with_context(|| format!("read {prefix}.trace"))?;
    let path = PathBuf::from(format!("{prefix}.o"));
    let object = path.is_file().then(|| std::fs::read(&path)).transpose()?;
    let manifest = PathBuf::from(format!("{prefix}.manifest.json"));
    let (mut inferred, mut source, mut target_asm) = (None, None, None);
    let traced = PathBuf::from(format!("{prefix}.source"));
    let traced = traced
        .is_file()
        .then(|| std::fs::read_to_string(&traced))
        .transpose()?;
    if manifest.is_file() {
        let meta: Value = serde_json::from_slice(&std::fs::read(&manifest)?)?;
        ensure!(
            meta["format_version"] == 1,
            "unsupported saved trace manifest format"
        );
        let compilation = &meta["compilation"];
        inferred = compilation["version"]
            .as_str()
            .map(Version::parse)
            .transpose()?;
        source = compilation["source"].as_str().map(str::to_owned);
        // (the unit's target asm, as it was when the trace was saved, when it is still there)
        target_asm = compilation["target_asm"]
            .as_str()
            .and_then(|p| std::fs::read(p).ok())
            .map(|b| String::from_utf8_lossy(&b).into_owned());
        if let Some(expected) = compilation["object_sha256"].as_str() {
            let object = object
                .as_ref()
                .context("the saved object its manifest names is missing")?;
            ensure!(
                sha256(object) == expected,
                "the saved object differs from its manifest"
            );
        }
        if let (Some(expected), Some(text)) = (compilation["source_sha256"].as_str(), &traced) {
            ensure!(
                sha256(text.as_bytes()) == expected,
                "the saved source differs from its manifest"
            );
        }
        if let Some(expected) = meta["trace_sha256"].as_str() {
            ensure!(
                sha256(text.as_bytes()) == expected,
                "the saved trace differs from its manifest"
            );
        }
    }
    ensure!(
        version.is_none() || inferred.is_none() || version == inferred,
        "`version` disagrees with the saved trace's manifest"
    );
    Ok(LoadedTrace {
        trace: trace::parse(&text)?,
        object,
        version: version.or(inferred).unwrap_or(Version::Gc27),
        source,
        text: traced,
        target_asm,
    })
}

fn load(call: &mut Call<'_>, params: LoadParams) -> Result<LoadResult> {
    let version = params.version.as_deref().map(Version::parse).transpose()?;
    let mut loaded = if let Some(prefix) = &params.load {
        ensure!(
            params.save.is_none(),
            "`load` and `save` cannot be combined"
        );
        load_saved(prefix, version)?
    } else {
        let unit = resolve(
            call,
            &UnitRef {
                handle: params.handle.clone(),
                project: params.project.clone(),
                unit: params.unit.clone(),
            },
        )?;
        let options = TraceOptions {
            cse: true,
            // (one function's records, unless the trace is saved for later)
            functions: if params.save.is_none() {
                params.function.iter().cloned().collect()
            } else {
                vec![]
            },
            ..TraceOptions::default()
        };
        let mut timing = Timing::default();
        let traced = trace_unit(
            call,
            &mut timing,
            &unit,
            &params.files,
            params.inline_max_size,
            &options,
            crate::timeout(params.timeout_ms),
        )?;
        call.timing.add(&timing);
        // (the overlay's text when it has the source: its paths are the project's, as trace_unit reads them)
        let key = |path: &std::path::Path| {
            let text = path
                .to_string_lossy()
                .replace('\\', "/")
                .replace("/./", "/");
            if cfg!(windows) {
                text.to_lowercase()
            } else {
                text
            }
        };
        let wanted = key(&unit.project.root.join(&traced.source));
        let text = match params
            .files
            .iter()
            .find(|(path, _)| key(&unit.project.root.join(path)) == wanted)
        {
            Some((_, text)) => text.clone(),
            None => std::fs::read_to_string(unit.project.root.join(&traced.source)).ok(),
        };
        if let Some(prefix) = &params.save {
            if let Some(text) = &text {
                std::fs::write(format!("{prefix}.source"), text)?;
            }
            std::fs::write(format!("{prefix}.o"), &traced.object)?;
            std::fs::write(format!("{prefix}.trace"), &traced.trace)?;
            let manifest = json!({
                "format_version": 1,
                "trace_sha256": sha256(traced.trace.as_bytes()),
                "compilation": {
                    "version": traced.version.name(),
                    "source": traced.source,
                    "object_sha256": sha256(&traced.object),
                    "source_sha256": text.as_ref().map(|t| sha256(t.as_bytes())),
                    "unit": unit.unit.name,
                    "target_asm": traced.target_asm.as_ref().map(|p| p.display().to_string()),
                },
            });
            std::fs::write(
                format!("{prefix}.manifest.json"),
                serde_json::to_vec_pretty(&manifest)?,
            )?;
        }
        let asm = traced
            .target_asm
            .as_ref()
            .and_then(|p| std::fs::read(p).ok());
        LoadedTrace {
            trace: trace::parse(&traced.trace)?,
            object: Some(traced.object),
            version: traced.version,
            source: Some(traced.source),
            text,
            target_asm: asm.map(|b| String::from_utf8_lossy(&b).into_owned()),
        }
    };
    if let Some(path) = &params.target_asm {
        let bytes = std::fs::read(path).with_context(|| format!("read {}", path.display()))?;
        loaded.target_asm = Some(String::from_utf8_lossy(&bytes).into_owned());
    }
    let result = LoadResult {
        trace: String::new(),
        version: loaded.version.name().into(),
        source: loaded.source.clone(),
        functions: loaded
            .trace
            .allocations
            .iter()
            .map(|a| TracedFunction {
                function: a.before.function.clone(),
                link: a.before.link.clone(),
            })
            .collect(),
        has_target_asm: loaded.target_asm.is_some(),
    };
    let handle = call.session.keep_trace(Arc::new(loaded));
    Ok(LoadResult {
        trace: handle,
        ..result
    })
}

pub(crate) fn loaded(call: &Call<'_>, handle: &str) -> Result<Arc<LoadedTrace>> {
    call.session
        .trace(handle)
        .with_context(|| format!("unknown trace handle {handle} (trace.load gives one)"))
}

/// The one allocation of FUNCTION (a part of its name or link name; an exact one wins).
pub(crate) fn one<'a>(input: &'a LoadedTrace, function: Option<&str>) -> Result<&'a Allocation> {
    let mut found: Vec<&Allocation> = input
        .trace
        .allocations
        .iter()
        .filter(|a| {
            function.is_none_or(|f| a.before.function.contains(f) || a.before.link.contains(f))
        })
        .collect();
    if found.len() > 1
        && let Some(f) = function
    {
        let exact: Vec<_> = found
            .iter()
            .copied()
            .filter(|a| a.before.function == f || a.before.link == f)
            .collect();
        if !exact.is_empty() {
            found = exact;
        }
    }
    ensure!(
        found.len() == 1,
        "{function:?} matches {} functions{}",
        found.len(),
        if found.is_empty() {
            String::new()
        } else {
            format!(
                ": {}",
                found
                    .iter()
                    .take(8)
                    .map(|a| a.before.link.as_str())
                    .collect::<Vec<_>>()
                    .join(", ")
            )
        }
    );
    Ok(found[0])
}

pub(crate) fn names(
    input: &LoadedTrace,
    allocation: &Allocation,
) -> Result<BTreeMap<(String, u32), String>> {
    let parsed = cse::parse(&input.trace.records)?;
    parsed
        .get(&allocation.before.link)
        .map(cse::names)
        .transpose()
        .map(Option::unwrap_or_default)
}

fn reg(class: &str, number: i32) -> String {
    let prefix = match class {
        "FPR" => "f",
        "VR" => "v",
        "CR" => "cr",
        _ => "r",
    };
    format!("{prefix}{number}")
}

fn label(
    class: &str,
    v: u32,
    names: &BTreeMap<(String, u32), String>,
    round: Option<&Round>,
) -> String {
    if v < 32 {
        return reg(class, v as i32);
    }
    let extra = names
        .get(&(class.into(), v))
        .map(String::as_str)
        .or_else(|| {
            round
                .and_then(|r| r.nodes.get(&v))
                .filter(|n| n.flags & 4 != 0)
                .map(|_| "coalesced")
        });
    format!(
        "%{v}{}",
        extra.map_or(String::new(), |name| format!(" ({name})"))
    )
}

pub(crate) fn describe(
    edit: &Edit,
    class: &str,
    names: &BTreeMap<(String, u32), String>,
    round: &Round,
) -> String {
    let label = |v| label(class, v, names, Some(round));
    match edit {
        Edit::Order(order) => {
            let moved = colouring::renumbered(order);
            format!(
                "numbered, lowest first: {} (* moved: the others keep their order)",
                order
                    .iter()
                    .map(|v| label(*v) + if moved.contains(v) { "*" } else { "" })
                    .collect::<Vec<_>>()
                    .join(" < ")
            )
        }
        Edit::Move {
            node,
            after,
            anchor,
        } => format!(
            "number {} just {} {}",
            label(*node),
            if *after { "after" } else { "before" },
            label(*anchor)
        ),
        Edit::Drop(v) => format!("remove {}", label(*v)),
        Edit::Degree(v, d) => format!("degree of {} {d:+}", label(*v)),
        Edit::Neighbours(v, d) => format!("degree of each neighbour of {} {d:+}", label(*v)),
        Edit::Unlink(a, b) | Edit::Link(a, b) => format!(
            "{} the edge {} - {}",
            if matches!(edit, Edit::Unlink(..)) {
                "remove"
            } else {
                "add"
            },
            label(*a),
            label(*b)
        ),
    }
}

/// VALUE as Python's `repr` writes it (the colouring reports' text came from Python).
fn repr(value: &Value, numeric_keys: bool) -> String {
    match value {
        Value::Null => "None".into(),
        Value::Bool(v) => if *v { "True" } else { "False" }.into(),
        Value::String(s) => {
            let quote = if s.contains('\'') && !s.contains('"') {
                '"'
            } else {
                '\''
            };
            let escaped = s
                .replace('\\', "\\\\")
                .replace(quote, &format!("\\{quote}"))
                .replace('\n', "\\n")
                .replace('\r', "\\r")
                .replace('\t', "\\t");
            format!("{quote}{escaped}{quote}")
        }
        Value::Array(a) => format!(
            "[{}]",
            a.iter()
                .map(|v| repr(v, numeric_keys))
                .collect::<Vec<_>>()
                .join(", ")
        ),
        Value::Object(o) => format!(
            "{{{}}}",
            o.iter()
                .map(|(k, v)| format!(
                    "{}: {}",
                    if numeric_keys && k.parse::<i64>().is_ok() {
                        k.clone()
                    } else {
                        repr(&json!(k), false)
                    },
                    repr(v, numeric_keys)
                ))
                .collect::<Vec<_>>()
                .join(", ")
        ),
        _ => value.to_string(),
    }
}

/// A register or vreg: `%35`, `r5`, `35`.
fn number(value: &Value) -> Result<u32> {
    match value {
        Value::Number(n) => n
            .as_u64()
            .and_then(|n| u32::try_from(n).ok())
            .context("a register is a number"),
        Value::String(s) => s
            .trim()
            .trim_start_matches('%')
            .trim_start_matches(['r', 'f', 'v'])
            .parse()
            .with_context(|| format!("invalid register {s}")),
        _ => bail!("invalid register {value}"),
    }
}

/// An edit as `Edit::value` writes it: `["move", V, "before"|"after", W]`, `["order", [V, ...]]`, `["drop", V]`,
/// `["degree", V, D]`, `["neighbours", V, D]`, `["unlink", A, B]`, `["link", A, B]`.
fn edit(value: &Value) -> Result<Edit> {
    let parts = value.as_array().context("an edit is a list: [kind, ...]")?;
    let kind = parts
        .first()
        .and_then(Value::as_str)
        .context("an edit begins with its kind")?;
    let at = |i: usize| {
        parts
            .get(i)
            .with_context(|| format!("{kind} needs more fields"))
    };
    let delta = |i: usize| -> Result<i32> {
        at(i)?
            .as_i64()
            .and_then(|d| i32::try_from(d).ok())
            .with_context(|| format!("{kind}: a whole number"))
    };
    Ok(match kind {
        "move" => Edit::Move {
            node: number(at(1)?)?,
            after: match at(2)?.as_str() {
                Some("after") => true,
                Some("before") => false,
                _ => bail!("move: before or after"),
            },
            anchor: number(at(3)?)?,
        },
        "order" => Edit::Order(
            at(1)?
                .as_array()
                .context("order: a list of vregs")?
                .iter()
                .map(number)
                .collect::<Result<_>>()?,
        ),
        "drop" => Edit::Drop(number(at(1)?)?),
        "degree" => Edit::Degree(number(at(1)?)?, delta(2)?),
        "neighbours" => Edit::Neighbours(number(at(1)?)?, delta(2)?),
        "unlink" => Edit::Unlink(number(at(1)?)?, number(at(2)?)?),
        "link" => Edit::Link(number(at(1)?)?, number(at(2)?)?),
        other => bail!("unknown edit {other}"),
    })
}

pub(crate) fn edits(values: &[Value]) -> Result<Vec<Edit>> {
    values.iter().map(edit).collect()
}

#[derive(Debug, Default, Deserialize, JsonSchema)]
#[serde(deny_unknown_fields)]
pub struct FunctionParams {
    /// The loaded trace.
    pub trace: String,
    pub function: Option<String>,
    /// GPR (default), FPR, VR or CR.
    pub class: Option<String>,
    /// Registers the target has, by vreg (`{"35": "r31"}`), over or in place of the target asm's.
    #[serde(default)]
    #[schemars(with = "BTreeMap<String, Value>")]
    pub wants: BTreeMap<String, Value>,
    /// Give the report's lines (`text`).
    #[serde(default)]
    pub text: bool,
}

#[derive(Debug, Serialize, JsonSchema)]
pub struct Report {
    #[schemars(with = "Value")]
    pub result: Value,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub text: Option<Vec<String>>,
}

pub(crate) fn class_of(text: &Option<String>) -> Result<String> {
    let class = text.as_deref().unwrap_or("GPR").to_uppercase();
    ensure!(
        ["GPR", "FPR", "VR", "CR", "SPR"].contains(&class.as_str()),
        "unknown register class {class}"
    );
    Ok(class)
}

/// What a colouring method works on: the allocation, its last round of the class, the targets, the names.
struct Graph<'a> {
    allocation: &'a Allocation,
    round: &'a Round,
    first_count: u32,
    class: String,
    derived: target::Derived,
    names: BTreeMap<(String, u32), String>,
}

fn context<'a>(input: &'a LoadedTrace, params: &FunctionParams) -> Result<Graph<'a>> {
    let class = class_of(&params.class)?;
    let allocation = one(input, params.function.as_deref())?;
    let mut wants = BTreeMap::new();
    for (v, register) in &params.wants {
        wants.insert(number(&json!(v))?, i32::try_from(number(register)?)?);
    }
    let derived = if let Some(asm) = &input.target_asm {
        let words = ppc::target_words(asm, &allocation.before.link).with_context(|| {
            format!("the target asm has no function {}", allocation.before.link)
        })?;
        let ours = input
            .object
            .as_ref()
            .map(|o| ppc::function_words(o, &allocation.before.link))
            .transpose()?
            .flatten()
            .unwrap_or_default();
        target::derive(allocation, &ours, &words, &class, &wants)?
    } else {
        ensure!(
            !wants.is_empty(),
            "no target asm (trace.load's `target_asm`, or a unit's) and no `wants`"
        );
        target::Derived {
            targets: wants,
            stats: json!({}),
            ..Default::default()
        }
    };
    let rounds: Vec<&Round> = allocation
        .before
        .rounds
        .iter()
        .filter(|r| class_name(r.class) == class)
        .collect();
    let round = *rounds.last().with_context(|| {
        format!(
            "{}: no {class} colouring traced",
            allocation.before.function
        )
    })?;
    Ok(Graph {
        allocation,
        round,
        first_count: rounds[0].count,
        class,
        derived,
        names: names(input, allocation)?,
    })
}

impl Graph<'_> {
    fn problem(&self) -> Result<Problem<'_>> {
        Problem::new(
            self.round,
            &self.class,
            Some(self.first_count),
            self.derived.targets.clone(),
        )
    }
    fn label(&self, v: u32) -> String {
        label(&self.class, v, &self.names, Some(self.round))
    }
    fn describe(&self, e: &Edit) -> String {
        describe(e, &self.class, &self.names, self.round)
    }
    fn head(&self, problem: &Problem<'_>) -> String {
        format!(
            "== {}: {}, {} registers wrong",
            self.allocation.before.function,
            self.class,
            problem.base.wrong.len()
        )
    }
    /// Whether register COLOUR is non-volatile (a claim).
    fn claims(&self, colour: i32) -> bool {
        colour >= if self.class == "VR" { 20 } else { 14 }
    }
}

#[derive(Debug, Default, Deserialize, JsonSchema)]
#[serde(deny_unknown_fields)]
pub struct VerifyParams {
    pub trace: String,
    pub function: Option<String>,
    /// One class (default GPR, FPR and VR).
    pub class: Option<String>,
    #[serde(default)]
    pub text: bool,
}

fn verify(call: &mut Call<'_>, params: VerifyParams) -> Result<Report> {
    let input = loaded(call, &params.trace)?;
    let classes = match &params.class {
        Some(_) => vec![class_of(&params.class)?],
        None => vec!["GPR".into(), "FPR".into(), "VR".into()],
    };
    let mut results = vec![];
    for a in &input.trace.allocations {
        if params
            .function
            .as_deref()
            .is_some_and(|f| !a.before.function.contains(f) && !a.before.link.contains(f))
        {
            continue;
        }
        for c in &classes {
            if let Some(result) = colouring::verify(a, c)? {
                results.push(result);
            }
        }
    }
    let ok = results.iter().all(|r| r["ok"] == true);
    let text = params.text.then(|| {
        let mut lines = vec![];
        for r in &results {
            let mut head = format!(
                "{} {}: {} nodes",
                r["function"].as_str().unwrap_or("?"),
                r["class"].as_str().unwrap_or("?"),
                r["nodes"]
            );
            if r["rounds"].as_u64().unwrap_or(0) > 1 {
                head += &format!(", round {} (earlier ones spilled)", r["rounds"]);
            }
            let mut details = vec![];
            if r["order_agrees"] != true {
                details.push("the simplification's replay gives another order".to_owned());
            }
            let disagreements = r["colour_disagreements"]
                .as_array()
                .map_or(&[][..], Vec::as_slice);
            if !disagreements.is_empty() {
                details.push(format!(
                    "colours differ: {}",
                    disagreements
                        .iter()
                        .take(8)
                        .map(|d| format!("%{} {} (traced {})", d[0], repr(&d[1], false), d[2]))
                        .collect::<Vec<_>>()
                        .join(", ")
                ));
            }
            if r["spilled"] != r["traced_spilled"] {
                details.push(format!(
                    "spilled {} (traced {})",
                    repr(&r["spilled"], false),
                    repr(&r["traced_spilled"], false)
                ));
            }
            let claims = r["claims"].as_array().map_or(&[][..], Vec::as_slice);
            let claimed = if claims.is_empty() {
                String::new()
            } else {
                format!(
                    " claims {}",
                    claims
                        .iter()
                        .map(Value::to_string)
                        .collect::<Vec<_>>()
                        .join(",")
                )
            };
            let guess = if r["inferred"] == true && !claims.is_empty() {
                " (the registers used before the colouring are a guess: no claim traced)"
            } else {
                ""
            };
            lines.push(format!(
                "{:9} {head}{claimed}{guess}{}",
                if r["ok"] == true { "OK" } else { "DISAGREES" },
                if details.is_empty() {
                    String::new()
                } else {
                    format!("; {}", details.join("; "))
                }
            ));
        }
        lines.push(format!(
            "{} of {} colourings replayed exactly",
            results.iter().filter(|r| r["ok"] == true).count(),
            results.len()
        ));
        lines
    });
    Ok(Report {
        result: json!({"functions": results, "ok": ok}),
        text,
    })
}

fn target_method(call: &mut Call<'_>, params: FunctionParams) -> Result<Report> {
    let input = loaded(call, &params.trace)?;
    let cx = context(&input, &params)?;
    let problem = cx.problem()?;
    let a = cx.allocation;
    let derived = &cx.derived;
    let mut lines = vec![
        format!("== {}: {} targets ==", a.before.function, cx.class),
        format!("   pairing: {}", repr(&derived.stats, false)),
    ];
    if !derived.renumbered.is_empty() {
        lines.push(format!(
            "   WARNING: {} registers hold other colours in the PCode than in the last colouring round (a spill round renumbered them): these targets cannot be trusted",
            derived.renumbered.len()
        ));
    }
    for (i, j) in &derived.conflicts {
        lines.push(format!(
            "   CONFLICT: {} and {} interfere but both get {}: a wrong pairing, or a want to check",
            cx.label(*i),
            cx.label(*j),
            reg(&cx.class, derived.targets[i])
        ));
    }
    lines.push(format!(
        "   {} registers with a target, {} wrong:",
        problem.targets.len(),
        problem.base.wrong.len()
    ));
    for v in &problem.base.wrong {
        let votes = derived.votes.get(v);
        lines.push(format!(
            "      {}: ours {}, target {}{}",
            cx.label(*v),
            reg(
                &cx.class,
                problem.base.replay.colour.get(v).copied().unwrap_or(-1)
            ),
            reg(&cx.class, problem.targets[v]),
            match votes {
                Some(votes) if votes.as_object().is_some_and(|o| o.len() > 1) =>
                    format!(" (the fields disagree: {})", repr(votes, true)),
                _ => String::new(),
            }
        ));
    }
    let untargeted: Vec<String> = problem
        .base
        .replay
        .order
        .iter()
        .filter(|v| {
            !problem.targets.contains_key(v)
                && cx.claims(problem.base.replay.colour.get(v).copied().unwrap_or(-1))
        })
        .map(|v| cx.label(*v))
        .collect();
    if !untargeted.is_empty() {
        lines.push(format!(
            "   non-volatile registers with no target (no paired field): {}",
            untargeted.join(", ")
        ));
    }
    Ok(Report {
        result: json!({"function": a.before.function, "class": cx.class, "targets": problem.targets, "wrong": problem.base.wrong, "votes": derived.votes, "conflicts": derived.conflicts, "renumbered": derived.renumbered, "stats": derived.stats}),
        text: params.text.then_some(lines),
    })
}

fn force(call: &mut Call<'_>, params: FunctionParams) -> Result<Report> {
    let input = loaded(call, &params.trace)?;
    let cx = context(&input, &params)?;
    let problem = cx.problem()?;
    let forced = search::force(&problem)?;
    let lines = vec![
        cx.head(&problem) + " ==",
        format!(
            "   the claiming nodes ordered by their target: {}",
            forced
                .order
                .iter()
                .filter(|v| cx.claims(problem.targets.get(v).copied().unwrap_or(-1)))
                .map(|v| cx.label(*v))
                .collect::<Vec<_>>()
                .join(" ")
        ),
        if forced.exact {
            "   EXACT: the graph can give the target's colours; only the order is wrong (the numbering or the degrees: search them)".into()
        } else {
            format!(
                "   still wrong: {}{}: no order of the claims gives the target; the graph must change (its edges or nodes)",
                forced
                    .wrong
                    .iter()
                    .map(|v| cx.label(*v))
                    .collect::<Vec<_>>()
                    .join(", "),
                if forced.spilled.is_empty() {
                    String::new()
                } else {
                    format!("; uncoloured: {}", repr(&json!(forced.spilled), false))
                }
            )
        },
    ];
    let mut value = serde_json::to_value(&forced)?;
    value["function"] = json!(cx.allocation.before.function);
    value["class"] = json!(cx.class);
    Ok(Report {
        result: value,
        text: params.text.then_some(lines),
    })
}

#[derive(Debug, Default, Deserialize, JsonSchema)]
#[serde(deny_unknown_fields)]
pub struct ReplayParams {
    pub trace: String,
    pub function: Option<String>,
    pub class: Option<String>,
    #[serde(default)]
    #[schemars(with = "BTreeMap<String, Value>")]
    pub wants: BTreeMap<String, Value>,
    /// The edits, each as `Edit::value` writes it (`["degree", 35, 1]`, `["move", 40, "after", 37]`, ...).
    #[serde(default)]
    pub edits: Vec<Value>,
    #[serde(default)]
    pub text: bool,
}

fn as_function(params: &ReplayParams) -> FunctionParams {
    FunctionParams {
        trace: params.trace.clone(),
        function: params.function.clone(),
        class: params.class.clone(),
        wants: params.wants.clone(),
        text: params.text,
    }
}

fn replay(call: &mut Call<'_>, params: ReplayParams) -> Result<Report> {
    let input = loaded(call, &params.trace)?;
    let cx = context(&input, &as_function(&params))?;
    let problem = cx.problem()?;
    let edits = edits(&params.edits)?;
    let result = problem.run(&edits)?;
    let text = params.text.then(|| {
        let mut lines = vec![
            format!(
                "== {}: {} with {} ==",
                cx.allocation.before.function,
                cx.class,
                if edits.is_empty() {
                    "no edit".into()
                } else {
                    edits
                        .iter()
                        .map(|e| cx.describe(e))
                        .collect::<Vec<_>>()
                        .join("; ")
                }
            ),
            format!(
                "   wrong: {} -> {}{}",
                problem.base.wrong.len(),
                result.wrong.len(),
                if result.replay.spilled.is_empty() {
                    String::new()
                } else {
                    format!(
                        "; UNCOLOURED (a spill, not modelled): {}",
                        repr(&json!(result.replay.spilled), false)
                    )
                }
            ),
        ];
        let all: BTreeSet<u32> = problem
            .base
            .replay
            .colour
            .keys()
            .chain(result.replay.colour.keys())
            .copied()
            .collect();
        for v in all {
            let (before, after) = (
                problem.base.replay.colour.get(&v),
                result.replay.colour.get(&v),
            );
            if before != after {
                lines.push(format!(
                    "   {}: {} -> {}{}",
                    cx.label(v),
                    before.map_or("-".into(), |c| reg(&cx.class, *c)),
                    after.map_or("-".into(), |c| reg(&cx.class, *c)),
                    problem.targets.get(&v).map_or(String::new(), |c| format!(
                        " (target {})",
                        reg(&cx.class, *c)
                    ))
                ));
            }
        }
        lines
    });
    Ok(Report {
        result: json!({"function": cx.allocation.before.function, "class": cx.class, "edits": edits, "wrong": result.wrong, "spilled": result.replay.spilled, "colour": result.replay.colour, "order": result.replay.order}),
        text,
    })
}

#[derive(Debug, Default, Deserialize, JsonSchema)]
#[serde(deny_unknown_fields)]
pub struct ReplayManyParams {
    pub trace: String,
    pub function: Option<String>,
    pub class: Option<String>,
    #[serde(default)]
    #[schemars(with = "BTreeMap<String, Value>")]
    pub wants: BTreeMap<String, Value>,
    /// Edit lists, each replayed on the same graph.
    pub candidates: Vec<Vec<Value>>,
    /// Each result's colours and order too (they are large).
    #[serde(default)]
    pub colours: bool,
}

#[derive(Debug, Serialize, JsonSchema)]
pub struct Replayed {
    pub wrong: Vec<u32>,
    pub spilled: Vec<u32>,
    #[serde(skip_serializing_if = "Option::is_none")]
    #[schemars(with = "Option<Value>")]
    pub colour: Option<BTreeMap<u32, i32>>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub order: Option<Vec<u32>>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub error: Option<String>,
}

#[derive(Debug, Serialize, JsonSchema)]
pub struct ReplayManyResult {
    pub function: String,
    pub class: String,
    /// The vregs wrong with no edit.
    pub base_wrong: Vec<u32>,
    pub results: Vec<Replayed>,
}

fn replay_many(call: &mut Call<'_>, params: ReplayManyParams) -> Result<ReplayManyResult> {
    let input = loaded(call, &params.trace)?;
    let cx = context(
        &input,
        &FunctionParams {
            trace: params.trace.clone(),
            function: params.function.clone(),
            class: params.class.clone(),
            wants: params.wants.clone(),
            text: false,
        },
    )?;
    let problem = cx.problem()?;
    let started = std::time::Instant::now();
    let mut results = vec![];
    for (index, candidate) in params.candidates.iter().enumerate() {
        if index % 64 == 0 && (call.cancel)() {
            bail!("cancelled");
        }
        results.push(match edits(candidate).and_then(|e| problem.run(&e)) {
            Ok(scored) => Replayed {
                wrong: scored.wrong,
                spilled: scored.replay.spilled,
                colour: params.colours.then_some(scored.replay.colour),
                order: params.colours.then_some(scored.replay.order),
                error: None,
            },
            Err(e) => Replayed {
                wrong: vec![],
                spilled: vec![],
                colour: None,
                order: None,
                error: Some(format!("{e:#}")),
            },
        });
    }
    call.timing.model += crate::ms(started.elapsed());
    Ok(ReplayManyResult {
        function: cx.allocation.before.function.clone(),
        class: cx.class.clone(),
        base_wrong: problem.base.wrong.clone(),
        results,
    })
}

#[derive(Debug, Default, Deserialize, JsonSchema)]
#[serde(deny_unknown_fields)]
pub struct SearchParams {
    pub trace: String,
    pub function: Option<String>,
    pub class: Option<String>,
    #[serde(default)]
    #[schemars(with = "BTreeMap<String, Value>")]
    pub wants: BTreeMap<String, Value>,
    pub iterations: Option<usize>,
    pub seed: Option<String>,
    pub restarts: Option<usize>,
    /// The solutions kept.
    pub top: Option<usize>,
    /// Vregs whose number stays; the vregs renumbered (default: those the search chooses).
    #[serde(default)]
    pub fixed: Vec<u32>,
    pub nodes: Option<Vec<u32>>,
    /// The edit kinds tried (default drop, degree, unlink).
    pub kinds: Option<Vec<String>>,
    #[serde(default)]
    pub text: bool,
}

fn search_method(call: &mut Call<'_>, params: SearchParams) -> Result<Report> {
    let input = loaded(call, &params.trace)?;
    let cx = context(
        &input,
        &FunctionParams {
            trace: params.trace.clone(),
            function: params.function.clone(),
            class: params.class.clone(),
            wants: params.wants.clone(),
            text: false,
        },
    )?;
    let problem = cx.problem()?;
    let defaults = Options::default();
    let options = Options {
        kinds: params.kinds.clone().unwrap_or(defaults.kinds),
        iterations: params.iterations.unwrap_or(defaults.iterations),
        seed: params.seed.clone().unwrap_or(defaults.seed),
        nodes: params.nodes.clone().filter(|n| !n.is_empty()),
        fixed: params.fixed.iter().copied().collect(),
        keep: params.top.unwrap_or(defaults.keep),
        restarts: params.restarts.unwrap_or(defaults.restarts),
    };
    let started = std::time::Instant::now();
    let found = search::search(&problem, &options)?;
    call.timing.model += crate::ms(started.elapsed());
    let text = params.text.then(|| {
        let mut lines = vec![format!(
            "{}: {} ==",
            cx.head(&problem),
            problem
                .base
                .wrong
                .iter()
                .map(|v| cx.label(*v))
                .collect::<Vec<_>>()
                .join(", ")
        )];
        if !found.singles.is_empty() {
            lines.push("   single changes that leave fewer wrong:".into());
            lines.extend(
                found
                    .singles
                    .iter()
                    .map(|(wrong, e)| format!("      {wrong} wrong: {}", cx.describe(e))),
            );
        }
        lines.push(format!(
            "   renumbering {}",
            found
                .nodes
                .iter()
                .map(|v| cx.label(*v))
                .collect::<Vec<_>>()
                .join(" ")
        ));
        for (change, edits) in &found.exact {
            lines.push(format!("   EXACT (change {change}):"));
            lines.extend(edits.iter().map(|e| format!("      {}", cx.describe(e))));
        }
        if let Some((wrong, edits)) = &found.best {
            lines.push(format!(
                "   no exact solution found; the best leaves {wrong} wrong:"
            ));
            lines.extend(edits.iter().map(|e| format!("      {}", cx.describe(e))));
        }
        lines
    });
    Ok(Report {
        result: json!({"function": cx.allocation.before.function, "class": cx.class, "wrong": problem.base.wrong, "nodes": found.nodes, "exact": found.exact.iter().map(|(change, edits)| json!({"change": change, "edits": edits})).collect::<Vec<_>>(), "best": found.best, "singles": found.singles}),
        text,
    })
}

#[derive(Debug, Default, Deserialize, JsonSchema)]
#[serde(deny_unknown_fields)]
pub struct RemapParams {
    /// The two loaded traces (the same compiler).
    pub first: String,
    pub second: String,
    pub function: Option<String>,
    pub class: Option<String>,
    /// The class's round in each (zero-based; default its last), and its snapshot (before or after).
    pub first_round: Option<usize>,
    pub second_round: Option<usize>,
    pub first_snapshot: Option<String>,
    pub second_snapshot: Option<String>,
    #[serde(default)]
    pub text: bool,
}

fn phase(text: &Option<String>) -> Result<SnapshotPhase> {
    Ok(match text.as_deref() {
        None | Some("before") => SnapshotPhase::Before,
        Some("after") => SnapshotPhase::After,
        Some(other) => bail!("snapshot must be before or after, not {other}"),
    })
}

fn remap(call: &mut Call<'_>, params: RemapParams) -> Result<Report> {
    let class = class_of(&params.class)?;
    let (a, b) = (loaded(call, &params.first)?, loaded(call, &params.second)?);
    ensure!(
        a.version == b.version,
        "remap needs the same compiler version on both traces"
    );
    let first = one(&a, params.function.as_deref())?;
    let second = one(&b, params.function.as_deref())?;
    let first_graph = Selection {
        round: params.first_round,
        phase: phase(&params.first_snapshot)?,
    };
    let second_graph = Selection {
        round: params.second_round,
        phase: phase(&params.second_snapshot)?,
    };
    fn snapshot(allocation: &Allocation, selection: Selection) -> &Snapshot {
        match selection.phase {
            SnapshotPhase::Before => &allocation.before,
            SnapshotPhase::After => &allocation.after,
        }
    }
    let inferred = target::remap_snapshots(
        snapshot(first, first_graph),
        snapshot(second, second_graph),
        a.version,
    );
    let comparison =
        correspondence::compare(first, second, &inferred, &class, first_graph, second_graph)?;
    let text = params.text.then(|| {
        let mut lines = vec![
            format!(
                "== {}: {} of {} / {} instructions paired ==",
                first.before.function,
                inferred.paired,
                inferred.instructions.0,
                inferred.instructions.1
            ),
            format!(
                "   {} unambiguous instruction hypotheses; elimination indeterminate",
                comparison.inferred_mapping.len()
            ),
        ];
        for (side, graph) in [("first", &comparison.first), ("second", &comparison.second)] {
            lines.push(match graph {
                Some(graph) => format!(
                    "   {side}: {:?} class round {}, {} graph nodes, {} graph-only nodes",
                    graph.phase,
                    graph.class_round,
                    graph.nodes.len(),
                    graph.graph_only.len()
                ),
                None => format!("   {side}: requested graph snapshot unavailable"),
            });
        }
        if let Some(delta) = &comparison.delta {
            lines.push(format!(
                "   unmatched graph nodes: {} first / {} second (not proven eliminated)",
                delta.unmatched_first.len(),
                delta.unmatched_second.len()
            ));
            lines.push(format!(
                "   unclassified boundary edges: {} first / {} second",
                delta.boundary_first.len(),
                delta.boundary_second.len()
            ));
            lines.push(format!(
                "   inferred edge changes: +{} / -{}",
                delta.inferred_edges_added.len(),
                delta.inferred_edges_removed.len()
            ));
        }
        lines.extend(comparison.limitations.iter().map(|l| format!("   {l}")));
        lines
    });
    Ok(Report {
        result: serde_json::to_value(&comparison)?,
        text,
    })
}

pub(crate) fn methods() -> Vec<Method> {
    vec![
        crate::method!(
            "trace.load",
            "a trace kept by the session: a unit's traced compile (through files given in place of the disk's) or a saved one; its handle names it in the colouring methods",
            LoadParams,
            LoadResult,
            load
        ),
        crate::method!(
            "trace.verify",
            "each colouring of the trace replayed against what the trace recorded",
            VerifyParams,
            Report,
            verify
        ),
        crate::method!(
            "trace.target",
            "each vreg's register in the target (from the target's asm, or `wants`), and the vregs ours colours otherwise",
            FunctionParams,
            Report,
            target_method
        ),
        crate::method!(
            "trace.force",
            "the claiming nodes ordered by their targets: whether only the order is wrong, or the graph must change",
            FunctionParams,
            Report,
            force
        ),
        crate::method!(
            "trace.replay",
            "a function's colouring replayed with edits of its graph (order, move, drop, degree, neighbours, link, unlink): the registers that change",
            ReplayParams,
            Report,
            replay
        ),
        crate::method!(
            "trace.replay_many",
            "many edit lists replayed on one function's graph, each scored against the target (the model stage of a search)",
            ReplayManyParams,
            ReplayManyResult,
            replay_many
        ),
        crate::method!(
            "trace.search",
            "a search for edits of a function's graph that give the target's colours",
            SearchParams,
            Report,
            search_method
        ),
        crate::method!(
            "trace.remap",
            "two traces' colouring graphs of a function paired: the instructions, the nodes, and the edges that differ",
            RemapParams,
            Report,
            remap
        ),
    ]
}
