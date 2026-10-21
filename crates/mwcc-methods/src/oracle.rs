//! The oracle's methods: `oracle.trace` (a traced compile of a unit, through an overlay of its files, or of a source
//! and flags, with its schema-6 document and text views), `oracle.regdiff` (the register differences of a function
//! against the target, explained from the trace) and `oracle.analyze` (the document of a saved trace).
use crate::{
    Call, Method, Timing, ms,
    unit::{Loaded, UnitRef, resolve, score_options},
};
use anyhow::{Context, Result, bail, ensure};
use mwcc_oracle_core::{
    allocation, document, inlining, regdiff, report,
    runner::{self, BlockLimit, CompileRequest, TraceOptions},
    trace::{self, Trace},
    version::{self, Version},
    views,
};
use mwcc_project::{CompileOptions, Overlay, compile_unit};
use schemars::JsonSchema;
use serde::{Deserialize, Serialize};
use serde_json::{Value, json};
use std::{
    collections::BTreeMap,
    path::PathBuf,
    time::{Duration, Instant},
};

/// The streams a trace records (mwcc-trace's `TraceOptions`; each adds records and time).
#[derive(Clone, Debug, Default, Deserialize, JsonSchema)]
#[serde(deny_unknown_fields)]
pub struct Streams {
    #[serde(default)]
    pub schedule: bool,
    #[serde(default)]
    pub locals: bool,
    #[serde(default)]
    pub blocks: bool,
    #[serde(default)]
    pub cse: bool,
    #[serde(default)]
    pub stages: bool,
    #[serde(default)]
    pub backend: bool,
    #[serde(default)]
    pub inline_cost: bool,
    #[serde(default)]
    pub full_cost: bool,
    #[serde(default)]
    pub instantiation: bool,
    #[serde(default)]
    pub source_arenas: bool,
    #[serde(default)]
    pub source_nodes: bool,
    #[serde(default)]
    pub names: bool,
    #[serde(default)]
    pub copy_propagation: bool,
    #[serde(default)]
    pub peephole: bool,
    #[serde(default)]
    pub alias: bool,
    #[serde(default)]
    pub alias_queries: bool,
    #[serde(default)]
    pub value_numbering: bool,
}

/// The text views to give (`text`): one of near, instantiation, cse/ir, blocks and locals, else each function's
/// inlining (or `inline_cost`'s, or `exchange`'s), allocation and frame, with pcode and schedule on request.
#[derive(Clone, Debug, Default, Deserialize, JsonSchema)]
#[serde(deny_unknown_fields)]
pub struct ViewParams {
    /// Give the views' lines (`text`).
    #[serde(default)]
    pub text: bool,
    /// Inlining decisions within this many cost units of the limit.
    pub near: Option<i64>,
    /// The instantiations of templates whose names have this.
    pub instantiation: Option<String>,
    #[serde(default)]
    pub cse: bool,
    /// The common subexpression pass's linear IR of this source line (`all`: the whole function), as each pass starts.
    pub ir: Option<String>,
    #[serde(default)]
    pub blocks: bool,
    #[serde(default)]
    pub locals: bool,
    /// Two registers (`A,B`): what an exchange of them would change.
    pub exchange: Option<String>,
    /// The inlining decisions about callees whose names have this, with their costs.
    pub inline_cost: Option<String>,
    #[serde(default)]
    pub pcode: bool,
    /// With `pcode`: each instruction's flags (`; fIsRead fIsConst ...`).
    #[serde(default)]
    pub flags: bool,
    #[serde(default)]
    pub schedule: bool,
    /// The trace's raw lines (the selected function's, when one is).
    #[serde(default)]
    pub raw: bool,
    /// The alias analysis's tables: each alias and each access's (`backend.alias`; gc2's probes, GC 2.6 and 2.7
    /// in process).
    #[serde(default)]
    pub alias: bool,
    /// What the value numbering did with each load and operation, and what killed the memory's values
    /// (`backend.value_numbering`; gc2's probes).
    #[serde(default)]
    pub value_numbering: bool,
    /// Two source lines, `A,B`: why the value numbering did or did not reuse line A's loads and operations at line
    /// B (`backend.why_not_cse`; gc2's probes).
    pub why_not_cse: Option<String>,
    /// The PCode at the stages the compiler's debug listing names (`AFTER VALUE NUMBERING 2`, `AFTER COPY
    /// PROPAGATION`, `AFTER REGISTER COLORING`...), those whose names have one of these, comma-separated, without
    /// regard to case (`value numbering,coloring`; `listing`: every stage) (`backend.stages`, and `text` with
    /// `views.text`; gc2's probes).
    pub stages: Option<String>,
    /// The static objects each function refers to that the compile made, or that are the unit's own (local):
    /// literals, constants' reference temporaries, string literals, static locals, with their sections, sizes and
    /// bytes (`backend.objects`; from the object, any version).
    #[serde(default)]
    pub objects: bool,
    /// The memory accesses each scheduling pass left unordered because the alias analysis said they cannot alias,
    /// where an answer that they may would have changed the order: their aliases, the block scheduled again
    /// with the edge, and what makes a pointer's memory its own (`backend.memory_order`, and `text` with
    /// `views.text`; gc2's probes, GC 2.6 and 2.7 in process).
    #[serde(default)]
    pub memory_order: bool,
}

#[derive(Debug, Default, Deserialize, JsonSchema)]
#[serde(deny_unknown_fields)]
pub struct TraceParams {
    /// A unit (compiled as its build compiles it, through `files`), or a source and its flags.
    pub handle: Option<String>,
    pub project: Option<PathBuf>,
    pub unit: Option<String>,
    #[serde(default)]
    pub files: BTreeMap<String, Option<String>>,
    pub inline_max_size: Option<u32>,
    /// A source and its flags, compiled in `directory` by this version's oracle (a compile outside a project).
    pub source: Option<String>,
    #[serde(default)]
    pub flags: Vec<String>,
    pub directory: Option<PathBuf>,
    pub version: Option<String>,
    /// `PATH=REPLACEMENT`: a file read in place of another (a source, or a header under an include directory).
    #[serde(default)]
    pub overrides: Vec<String>,
    /// Compare the object with the original compiler's (default true; a source only).
    pub check: Option<bool>,
    #[serde(default)]
    pub original_fallback: bool,
    /// The oracle patched to other block limits (`none`: unlimited; a source only).
    pub cut_limit: Option<String>,
    pub merge_limit: Option<String>,
    /// The function (or link name) to analyze; the trace keeps only its records when it is found.
    pub function: Option<String>,
    pub link: Option<String>,
    #[serde(default)]
    #[schemars(with = "Option<Value>")]
    pub streams: Option<Streams>,
    #[serde(default)]
    pub views: ViewParams,
    /// Write the trace, and the object, here.
    pub trace_path: Option<PathBuf>,
    pub object_path: Option<PathBuf>,
    /// A source's compile capture, `{format_version, compilation, report}` (the trace in it), written here: the
    /// provenance and source-guided methods read it.
    pub capture_path: Option<PathBuf>,
    pub timeout_ms: Option<u64>,
}

#[derive(Debug, Serialize, JsonSchema)]
pub struct TraceResult {
    /// The schema-6 document (docs/oracle/trace.md).
    #[schemars(with = "Value")]
    pub document: Value,
    /// The views' lines (with `views.text`).
    #[serde(skip_serializing_if = "Option::is_none")]
    pub text: Option<Vec<String>>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub raw: Option<Vec<String>>,
    /// The backend views (`views.alias`, `views.value_numbering`, `views.why_not_cse`).
    #[serde(skip_serializing_if = "Option::is_none")]
    #[schemars(with = "Option<Value>")]
    pub backend: Option<Value>,
    /// The object equals the build's (a unit) or the original compiler's (a source).
    pub identical: Option<bool>,
    pub differences: Vec<String>,
    pub version: String,
    pub trace_bytes: usize,
    /// A source's compile: its producer, its hashes, its flags and stock check (the capture's `compilation`,
    /// without the trace).
    #[serde(skip_serializing_if = "Option::is_none")]
    #[schemars(with = "Option<Value>")]
    pub compilation: Option<Value>,
}

impl Streams {
    fn options(&self) -> TraceOptions {
        TraceOptions {
            schedule: self.schedule,
            locals: self.locals,
            blocks: self.blocks,
            cse: self.cse,
            stages: self.stages,
            backend: self.backend,
            inline_cost: self.inline_cost,
            full_cost: self.full_cost,
            instantiation: self.instantiation,
            source_arenas: self.source_arenas || self.source_nodes,
            source_nodes: self.source_nodes,
            names: self.names,
            copy_propagation: self.copy_propagation,
            peephole: self.peephole,
            alias: self.alias,
            alias_queries: self.alias_queries,
            value_numbering: self.value_numbering,
            ..TraceOptions::default()
        }
    }
}

/// The streams the views need, over STREAMS.
fn needed(streams: &Streams, views: &ViewParams) -> TraceOptions {
    let mut options = streams.options();
    options.schedule |= views.schedule;
    options.locals |= views.locals;
    options.blocks |= views.blocks;
    options.cse |= views.cse || views.ir.is_some() || views.exchange.is_some();
    options.inline_cost |= views.near.is_some() || views.inline_cost.is_some();
    options.instantiation |= views.instantiation.is_some();
    options.alias |= views.alias || views.memory_order;
    options.alias_queries |= views.memory_order;
    options.schedule |= views.memory_order;
    options.value_numbering |= views.value_numbering || views.why_not_cse.is_some();
    options.listing |= views.stages.is_some();
    // (the instructions' opcode names)
    options.names |= options.alias || options.value_numbering || options.listing;
    options
}

/// The backend views of TRACE (see [`ViewParams`]), FUNCTION's (its name or link name) or LINK's.
fn backend_views(
    trace: &str,
    function: Option<&str>,
    link: Option<&str>,
    views: &ViewParams,
) -> Result<Option<Value>> {
    let lines = views
        .why_not_cse
        .as_deref()
        .map(|lines| {
            let (a, b) = lines
                .split_once(',')
                .context("why_not_cse is two source lines, A,B")?;
            anyhow::Ok((a.trim().parse::<i32>()?, b.trim().parse::<i32>()?))
        })
        .transpose()?;
    let stages: Vec<String> = views
        .stages
        .iter()
        .flat_map(|s| s.split(','))
        .map(|s| s.trim().to_string())
        .filter(|s| !s.is_empty())
        .collect();
    if !views.alias
        && !views.value_numbering
        && lines.is_none()
        && stages.is_empty()
        && !views.objects
    {
        return Ok(None);
    }
    let parsed = mwcc_trace::records::parse(
        trace,
        &mwcc_trace::records::Selection {
            alias: views.alias,
            value_numbering: views.value_numbering || lines.is_some(),
            pcode: stages.clone(),
            ..Default::default()
        },
    );
    let selected = |name: &str, linked: &str| {
        link.is_none_or(|l| l == linked)
            && function.is_none_or(|f| f.is_empty() || f == name || f == linked)
    };
    let passes: Vec<_> = parsed
        .value_numbering
        .iter()
        .filter(|p| selected(&p.function, &p.link))
        .collect();
    let mut backend = serde_json::Map::new();
    if views.alias {
        let tables: Vec<_> = parsed
            .alias
            .iter()
            .filter(|t| selected(&t.function, &t.link))
            .collect();
        backend.insert("alias".into(), json!(tables));
    }
    if views.value_numbering {
        backend.insert("value_numbering".into(), json!(passes));
    }
    if let Some((a, b)) = lines {
        backend.insert("why_not_cse".into(), json!(why_not_cse(&passes, a, b)));
    }
    if !stages.is_empty() {
        let dumps: Vec<_> = parsed
            .pcode
            .iter()
            .filter(|d| function.is_none_or(|f| f.is_empty() || d.function == f))
            .collect();
        backend.insert("stages".into(), json!(dumps));
    }
    Ok(Some(Value::Object(backend)))
}

/// The memory_order view of TRACE (FUNCTION's or LINK's blocks): for each scheduled block, the memory accesses the
/// scheduler was told cannot alias and that nothing else orders, where a may-alias answer would have changed the
/// schedule. Its JSON and its text.
fn memory_order(trace: &str, function: Option<&str>, link: Option<&str>) -> (Value, Vec<String>) {
    use mwcc_trace::sched::{describe_alias, is_distinct_pointee};
    let parsed = mwcc_trace::records::parse(
        trace,
        &mwcc_trace::records::Selection {
            sched: true,
            alias_queries: true,
            ..Default::default()
        },
    );
    let selected = |name: &str| {
        function.is_none_or(|f| f.is_empty() || f == name)
            && link.is_none_or(|l| l == name || l.starts_with(&format!("{name}__")))
    };
    let mut blocks = vec![];
    let mut lines = vec![];
    let mut heading = String::new();
    let (mut binding, mut distinct, mut unordered) = (0, 0, 0);
    let (mut scheduled, mut replayed) = (0, 0);
    for b in parsed.sched.iter().filter(|b| selected(&b.function)) {
        scheduled += 1;
        replayed += usize::from(b.simulation_agrees());
        let pairs = b.unordered_memory();
        unordered += pairs.len();
        // (those the edge would have reordered; where the block cannot be scheduled again as it was, those it
        // would have delayed)
        let pairs: Vec<_> = pairs
            .into_iter()
            .filter(|p| p.reorders.unwrap_or(p.binding))
            .collect();
        if pairs.is_empty() {
            continue;
        }
        let pass = if b.pre_ra {
            "before the allocation"
        } else {
            "after the allocation"
        };
        if heading != b.function {
            heading = b.function.clone();
            lines.push(format!("== {}: memory order ==", b.function));
        }
        let text = |i: usize| b.nodes[i].text.clone();
        let alias = |i: usize| {
            b.nodes[i].alias.map_or("no alias recorded".into(), |a| {
                describe_alias(&b.aliases, a)
            })
        };
        let order = |picks: &[mwcc_trace::sched::SchedPick]| {
            picks
                .iter()
                .map(|p| format!("#{}", p.node))
                .collect::<Vec<_>>()
                .join(" ")
        };
        lines.push(format!(
            "-- block {} ({pass}): {} pair(s) of accesses left unordered changed the order",
            b.block,
            pairs.len()
        ));
        for (i, x) in b.nodes.iter().enumerate() {
            lines.push(format!("   #{i:<3} {}", x.text));
        }
        let mut rows = vec![];
        for p in &pairs {
            binding += 1;
            let readonly = [p.first, p.second].iter().any(|&i| {
                b.nodes[i]
                    .alias
                    .is_some_and(|a| is_distinct_pointee(&b.aliases, a))
            });
            distinct += usize::from(readonly);
            lines.push(format!(
                "   #{} [{}] and #{} [{}] cannot alias: #{} issued at cycle {}, not at {} or later",
                p.first,
                text(p.first),
                p.second,
                text(p.second),
                p.second,
                p.second_cycle,
                p.first_cycle + p.latency
            ));
            lines.push(format!("      #{}: {}", p.first, alias(p.first)));
            lines.push(format!("      #{}: {}", p.second, alias(p.second)));
            match &p.with_edge {
                Some(picks) => lines.push(format!(
                    "      had they may-aliased: {}  (recorded: {})",
                    order(picks),
                    order(&b.picks)
                )),
                None => lines.push(format!(
                    "      (scheduling the block again gives {}, not the recorded {}: no what-if)",
                    order(&b.simulate(&[], b.issue_width())),
                    order(&b.picks)
                )),
            }
            if readonly {
                lines.push(
                    "      a read-only pointer's memory (a const method's `this`, a `const T*` parameter, of a class \
                     with no `mutable` member in itself, its bases or its members' classes) or a restrict \
                     pointer's is an object no other pointer reaches. A `mutable` member, a non-const method or \
                     pointer (or no restrict) makes it the worst case: the two accesses may alias."
                        .into(),
                );
            }
            rows.push(json!({
                "first": p.first,
                "second": p.second,
                "first_alias": alias(p.first),
                "second_alias": alias(p.second),
                "first_cycle": p.first_cycle,
                "second_cycle": p.second_cycle,
                "latency": p.latency,
                "reorders": p.reorders,
                "read_only_pointee": readonly,
                "with_edge": p.with_edge.as_ref().map(|picks| picks.iter().map(|q| q.node).collect::<Vec<_>>()),
            }));
        }
        blocks.push(json!({
            "function": b.function,
            "block": b.block,
            "pre_ra": b.pre_ra,
            "instructions": b.nodes.iter().map(|x| x.text.clone()).collect::<Vec<_>>(),
            "order": b.order(),
            "pairs": rows,
        }));
    }
    let replay =
        format!("scheduled again, {replayed} of {scheduled} blocks give the recorded order");
    if lines.is_empty() {
        lines.push(format!(
            "== memory order: no pair of accesses left unordered changed the order ({unordered} unordered; \
             {replay}) =="
        ));
    } else {
        lines.push(format!(
            "   {binding} pair(s) in {} block(s) ({unordered} unordered in all); {distinct} with a read-only or \
             restrict pointer's memory; {replay}",
            blocks.len()
        ));
    }
    (
        json!({
            "blocks": blocks,
            "unordered": unordered,
            "scheduled": scheduled,
            "replayed": replayed,
        }),
        lines,
    )
}

/// For each function of OBJECT (FUNCTION's or LINK's, when given), the static objects it refers to that the compile
/// made or that are the unit's own: what each is, by its name, section and contents.
fn compiled_objects(object: &[u8], function: Option<&str>, link: Option<&str>) -> Result<Value> {
    use mwcc_compare::strict::elf::{self, Binding};
    let object = elf::load(object)?;
    let mut out = vec![];
    for f in &object.functions {
        if link.is_some_and(|l| l != f.name)
            || function.is_some_and(|n| {
                !n.is_empty() && f.name != n && !f.name.starts_with(&format!("{n}__"))
            })
        {
            continue;
        }
        let mut referred: Vec<Value> = vec![];
        let mut seen = std::collections::BTreeSet::new();
        for r in &f.relocs {
            let Some(data) = object.data.get(&r.target) else {
                continue;
            };
            let made = data.name.starts_with('@') || data.name.contains("...");
            if !(made || data.binding == Binding::Local) || !seen.insert(data.name.clone()) {
                continue;
            }
            let section = data.section.as_str();
            let text = data.bytes.last() == Some(&0)
                && data.bytes.len() > 1
                && data.bytes[..data.bytes.len() - 1]
                    .iter()
                    .all(|b| b.is_ascii_graphic() || *b == b' ' || (8..=13).contains(b));
            let constant = matches!(section, ".sdata2" | ".rodata" | ".sbss2");
            let what = match (made, constant, text) {
                (_, _, true) => "a string literal",
                (true, true, _) => {
                    "a literal of the constant pool (a floating-point or other constant the code loads)"
                }
                (true, false, _) if matches!(section, ".data" | ".sdata" | ".bss" | ".sbss") => {
                    "an object the compiler made, writable: a constant's reference temporary (get_address_of_temp_copy: a \
                     const reference or address taken of a constant), a static guard or a compiler temporary"
                }
                (true, false, _) => "an object the compiler made",
                (false, true, _) => "a static constant of the unit",
                (false, false, _) => "a static variable of the unit (or a function's static local)",
            };
            // (a 4- or 8-byte object read as the float it may be)
            let value = match (data.bytes.len(), text) {
                (4, false) => Some(json!(f32::from_be_bytes(data.bytes[..4].try_into()?))),
                (8, false) => Some(json!(f64::from_be_bytes(data.bytes[..8].try_into()?))),
                _ => None,
            };
            let uses: Vec<String> = f
                .relocs
                .iter()
                .filter(|o| o.target == data.name)
                .map(|o| format!("{:#x}", o.offset))
                .collect();
            referred.push(json!({
                "name": data.name,
                "section": section,
                "size": data.size,
                "local": data.binding == Binding::Local,
                "what": what,
                "bytes": data.bytes.iter().take(32).map(|b| format!("{b:02x}")).collect::<String>(),
                "value": value,
                "text": text.then(|| String::from_utf8_lossy(&data.bytes[..data.bytes.len() - 1]).into_owned()),
                "uses": uses,
            }));
        }
        if !referred.is_empty() {
            out.push(json!({"function": f.name, "objects": referred}));
        }
    }
    Ok(json!(out))
}

/// For each value numbering pass, what became of line B's loads and operations, and whether it is line A's value
/// that they reused or could not.
fn why_not_cse(passes: &[&mwcc_trace::records::VnPass], a: i32, b: i32) -> Vec<Value> {
    let mut answers = vec![];
    for (index, pass) in passes.iter().enumerate() {
        let at_a: Vec<_> = pass.decisions.iter().filter(|d| d.line == a).collect();
        let at_b: Vec<_> = pass.decisions.iter().filter(|d| d.line == b).collect();
        if at_b.is_empty() {
            answers.push(json!({
                "pass": index, "function": pass.function, "aggressive": pass.aggressive,
                "answer": format!("no load or CSE operation of line {b} in this pass (a store, a call, or an operation \
                                   the pass does not number: a real register's li or lis, a record bit, a carry)"),
            }));
            continue;
        }
        for d in at_b {
            let from_a = d.other_line == Some(a);
            let answer = match d.outcome.as_str() {
                "reused" if from_a => format!(
                    "reused line {a}'s value ({})",
                    d.other.as_deref().unwrap_or("?")
                ),
                "reused" => format!(
                    "reused line {}'s value, not line {a}'s",
                    d.other_line.unwrap_or(0)
                ),
                "forwarded" => format!("took the value line {} stored", d.other_line.unwrap_or(0)),
                "skipped" => {
                    "not a candidate: volatile, or it writes more than one register".to_string()
                }
                _ => {
                    let reason = d.reason.clone().unwrap_or_default();
                    let mut answer = if from_a || at_a.is_empty() || d.other_line.is_none() {
                        reason
                    } else {
                        format!(
                            "{reason} (the nearest available value is line {}'s)",
                            d.other_line.unwrap_or(0)
                        )
                    };
                    if let Some(killer) = &d.killed_by {
                        answer.push_str(&format!("; killed by {killer}"));
                    }
                    if !at_a.is_empty() && at_a.iter().all(|x| x.extended_block != d.extended_block)
                    {
                        answer.push_str(&format!(
                            "; line {a} is in another extended basic block (B{} against B{}): its values are not \
                             available here",
                            at_a[0].extended_block, d.extended_block
                        ));
                    } else if at_a.is_empty() {
                        answer.push_str(&format!(
                            "; line {a} has no load or CSE operation in this pass"
                        ));
                    }
                    answer
                }
            };
            answers.push(json!({
                "pass": index, "function": pass.function, "aggressive": pass.aggressive,
                "instruction": d.instruction, "flags": d.flags, "outcome": d.outcome, "other": d.other,
                "other_line": d.other_line, "killed_by": d.killed_by, "answer": answer,
            }));
        }
    }
    answers
}

/// A traced compile: the trace, the object, its version, and how it compares.
pub(crate) struct Traced {
    pub trace: String,
    pub object: Vec<u8>,
    pub version: Version,
    pub source: String,
    pub oracle: String,
    pub identical: Option<bool>,
    pub differences: Vec<String>,
    pub original_fallback: bool,
    /// The unit's target asm, for the locals view.
    pub target_asm: Option<PathBuf>,
    /// A source's compile, as the runner gives it.
    pub compilation: Option<runner::Compilation>,
}

/// The unit's target asm, as dtk writes it beside the target object (`build/<version>/obj/<path>.o` has
/// `build/<version>/asm/<path>.s`), disassembled by the project's dtk when the project has not.
fn target_asm(loaded: &Loaded) -> Option<PathBuf> {
    loaded.unit.target_asm_or_disassembled()
}

/// LOADED compiled with a trace of OPTIONS through FILES.
pub(crate) fn trace_unit(
    call: &Call<'_>,
    timing: &mut Timing,
    loaded: &Loaded,
    files: &BTreeMap<String, Option<String>>,
    inline_max_size: Option<u32>,
    options: &TraceOptions,
    timeout: Duration,
) -> Result<Traced> {
    let unit = &loaded.unit;
    let version = Version::parse(
        unit.mw_version
            .as_deref()
            .context("the unit has no mw_version")?,
    )?;
    let mut overlay = Overlay::new();
    for (path, text) in files {
        let path = loaded.project.root.join(path);
        match text {
            Some(text) => overlay.insert(path, text.as_bytes()),
            None => overlay.insert_absent(path),
        }
    }
    let compiled = compile_unit(
        unit,
        &overlay,
        &CompileOptions {
            oracle_root: &call.session.oracle_root,
            inline_max_size,
            trace: Some(options),
            timeout,
            cancel: call.cancel,
            cache: None,
            persist: None,
        },
    )?;
    timing.trace += ms(compiled.time);
    if compiled.cancelled {
        bail!("cancelled");
    }
    ensure!(!compiled.timed_out, "the compile timed out");
    let object = match compiled.object {
        Some(object) if compiled.exit_code == 0 => object,
        _ => bail!(
            "the unit does not compile:\n{}",
            String::from_utf8_lossy(&compiled.console)
        ),
    };
    let built = std::fs::read(&unit.base_path).ok();
    let (identical, differences) = match &built {
        Some(built) if files.is_empty() && inline_max_size.is_none() => (
            Some(*built == object),
            if *built == object {
                vec![]
            } else {
                mwcc_oracle_core::elf::differences(&object, built).unwrap_or_default()
            },
        ),
        _ => (None, vec![]),
    };
    Ok(Traced {
        trace: String::from_utf8(compiled.trace).context("the trace is not UTF-8")?,
        object,
        version,
        source: unit.source.clone(),
        oracle: unit.compiler.display().to_string(),
        identical,
        differences,
        original_fallback: false,
        target_asm: target_asm(loaded),
        compilation: None,
    })
}

fn limit(text: &Option<String>) -> Result<Option<BlockLimit>> {
    text.as_deref().map(BlockLimit::parse).transpose()
}

fn trace_source(
    call: &Call<'_>,
    timing: &mut Timing,
    params: &TraceParams,
    options: TraceOptions,
) -> Result<Traced> {
    let source = params.source.clone().context("give a unit, or `source`")?;
    let request = CompileRequest {
        root: call.session.oracle_root.clone(),
        working_directory: match &params.directory {
            Some(directory) => directory.clone(),
            None => std::env::current_dir()?,
        },
        source,
        flags: params.flags.clone(),
        version: params
            .version
            .as_deref()
            .map(Version::parse)
            .transpose()?
            .unwrap_or(Version::Gc132),
        options,
        overrides: params.overrides.clone(),
        check: params.check.unwrap_or(true),
        original_fallback: params.original_fallback,
        cut_limit: limit(&params.cut_limit)?,
        merge_limit: limit(&params.merge_limit)?,
        keep_object: None,
        timeout: Duration::from_millis(params.timeout_ms.unwrap_or(600_000)),
    };
    let started = Instant::now();
    let compiled = runner::compile_controlled(&request, call.cancel)?;
    timing.trace += ms(started.elapsed());
    let target_asm = params.directory.as_ref().and_then(|d| {
        let ninja = d.join("build.ninja");
        let directory = mwcc_oracle_core::project::target_directory(&ninja).ok()?;
        mwcc_oracle_core::project::target_asm(&directory, &compiled.source)
            .ok()
            .flatten()
    });
    Ok(Traced {
        compilation: Some(compiled.clone()),
        trace: compiled.trace,
        object: compiled.object,
        version: compiled.version,
        source: compiled.source,
        oracle: compiled.oracle,
        identical: compiled.identical,
        differences: compiled.differences,
        original_fallback: compiled.original_fallback,
        target_asm,
    })
}

/// The document of a trace, its views and its raw lines.
fn documented(
    traced: &Traced,
    function: Option<&str>,
    link: Option<&str>,
    views: &ViewParams,
) -> Result<TraceResult> {
    let parsed = trace::parse(&traced.trace)?;
    let mut document = document::build(
        &parsed,
        traced.version,
        &traced.source,
        &traced.oracle,
        traced.identical,
        &traced.differences,
        &document::Selection {
            function,
            link,
            near: views.near,
        },
    )?;
    // (what ran: the oracle names the executable, and the module that ran in its place, in process, with its hash)
    if let Some(compilation) = &traced.compilation {
        document["module"] = json!(compilation.module);
        document["producer_sha256"] = json!(compilation.producer_sha256);
    }
    let text = if views.text {
        let asm = traced
            .target_asm
            .as_ref()
            .and_then(|p| std::fs::read(p).ok())
            .map(|b| String::from_utf8_lossy(&b).into_owned());
        Some(views::text(
            &document,
            &views::Traced {
                trace: &parsed,
                version: traced.version,
                object: &traced.object,
                original_fallback: traced.original_fallback,
            },
            &views::Views {
                function,
                link,
                near: views.near,
                instantiation: views.instantiation.as_deref(),
                cse: views.cse,
                ir: views.ir.as_deref(),
                blocks: views.blocks,
                block_limit: Some(100),
                locals: views.locals,
                target_asm: asm.as_deref(),
                exchange: views.exchange.as_deref(),
                inline_cost: views.inline_cost.as_deref(),
                pcode: views.pcode,
                flags: views.flags,
                schedule: views.schedule,
            },
        )?)
    } else {
        None
    };
    let mut backend = backend_views(&traced.trace, function, link, views)?;
    let mut text = text;
    if views.memory_order {
        let (value, lines) = memory_order(&traced.trace, function, link);
        backend
            .get_or_insert_with(|| json!({}))
            .as_object_mut()
            .map(|b| b.insert("memory_order".into(), value));
        if let Some(text) = text.as_mut() {
            text.extend(lines);
        }
    }
    if views.objects {
        let objects = compiled_objects(&traced.object, function, link)?;
        backend
            .get_or_insert_with(|| json!({}))
            .as_object_mut()
            .map(|b| b.insert("objects".into(), objects));
    }
    let raw = views.raw.then(|| {
        if function.is_some_and(|f| !f.is_empty()) || link.is_some() {
            trace::raw_selected(&traced.trace, function, link)
        } else {
            traced.trace.lines().map(str::to_owned).collect()
        }
    });
    Ok(TraceResult {
        document,
        text,
        raw,
        backend,
        identical: traced.identical,
        differences: traced.differences.clone(),
        version: traced.version.name().into(),
        trace_bytes: traced.trace.len(),
        compilation: traced
            .compilation
            .as_ref()
            .map(|c| {
                let mut value = serde_json::to_value(c)?;
                value.as_object_mut().map(|m| m.remove("trace"));
                anyhow::Ok(value)
            })
            .transpose()?,
    })
}

fn unit_ref(params: &TraceParams) -> Option<UnitRef> {
    (params.handle.is_some() || params.unit.is_some()).then(|| UnitRef {
        handle: params.handle.clone(),
        project: params.project.clone(),
        unit: params.unit.clone(),
    })
}

fn oracle_trace(call: &mut Call<'_>, params: TraceParams) -> Result<TraceResult> {
    let mut options = needed(&params.streams.clone().unwrap_or_default(), &params.views);
    // (only the selected function's records, when one is; the whole trace when none matches)
    options.functions = params
        .function
        .iter()
        .chain(&params.link)
        .filter(|f| !f.is_empty())
        .cloned()
        .collect();
    let timeout = Duration::from_millis(params.timeout_ms.unwrap_or(600_000));
    let which = unit_ref(&params);
    let loaded = which.as_ref().map(|w| resolve(call, w)).transpose()?;
    let mut timing = Timing::default();
    let run = |call: &Call<'_>, timing: &mut Timing, options: TraceOptions| match &loaded {
        Some(loaded) => trace_unit(
            call,
            timing,
            loaded,
            &params.files,
            params.inline_max_size,
            &options,
            timeout,
        ),
        None => trace_source(call, timing, &params, options),
    };
    let mut traced = run(call, &mut timing, options.clone())?;
    if !options.functions.is_empty() && trace::parse(&traced.trace)?.allocations.is_empty() {
        options.functions.clear();
        traced = run(call, &mut timing, options)?;
    }
    call.timing.add(&timing);
    if let Some(path) = &params.trace_path {
        std::fs::write(path, &traced.trace).with_context(|| format!("write {}", path.display()))?;
    }
    if let Some(path) = &params.object_path {
        std::fs::write(path, &traced.object)
            .with_context(|| format!("write {}", path.display()))?;
    }
    let result = documented(
        &traced,
        params.function.as_deref(),
        params.link.as_deref(),
        &params.views,
    )?;
    if let Some(path) = &params.capture_path {
        let compilation = traced
            .compilation
            .as_ref()
            .context("`capture_path` takes a source's compile: a unit's has no capture")?;
        std::fs::write(
            path,
            serde_json::to_vec_pretty(
                &json!({"format_version": 1, "compilation": compilation, "report": result.document}),
            )?,
        )
        .with_context(|| format!("write {}", path.display()))?;
    }
    Ok(result)
}

#[derive(Debug, Default, Deserialize, JsonSchema)]
#[serde(deny_unknown_fields)]
pub struct RegdiffParams {
    pub handle: Option<String>,
    pub project: Option<PathBuf>,
    pub unit: Option<String>,
    #[serde(default)]
    pub files: BTreeMap<String, Option<String>>,
    pub inline_max_size: Option<u32>,
    /// The function (mangled, demangled or short name).
    pub function: String,
    /// Offline: a saved trace and objdiff's diff document of it, with the trace's version.
    pub trace_path: Option<PathBuf>,
    pub diff_path: Option<PathBuf>,
    pub version: Option<String>,
    /// Give the report's lines (`text`): with the inlining of the function, its pcode, and every vreg (`full`).
    #[serde(default)]
    pub text: bool,
    #[serde(default)]
    pub inlining: bool,
    #[serde(default)]
    pub pcode: bool,
    #[serde(default)]
    pub full: bool,
    pub timeout_ms: Option<u64>,
}

#[derive(Debug, Serialize, JsonSchema)]
pub struct RegdiffResult {
    /// regdiff's document (the symbol, the target, the selection, the explanations).
    #[schemars(with = "Value")]
    pub document: Value,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub text: Option<Vec<String>>,
    /// The function's pcode before allocation (with `pcode`).
    #[serde(skip_serializing_if = "Option::is_none")]
    pub pcode: Option<Vec<String>>,
}

fn oracle_regdiff(call: &mut Call<'_>, params: RegdiffParams) -> Result<RegdiffResult> {
    let (parsed, diff, version, header) = if let Some(path) = &params.trace_path {
        let parsed = trace::parse(
            &std::fs::read_to_string(path).with_context(|| format!("read {}", path.display()))?,
        )?;
        let diff_path = params
            .diff_path
            .as_ref()
            .context("`trace_path` needs `diff_path`")?;
        let diff: Value = serde_json::from_slice(&std::fs::read(diff_path)?)?;
        let version = params
            .version
            .as_deref()
            .map(Version::parse)
            .transpose()?
            .unwrap_or(Version::Gc132);
        (parsed, diff, version, None)
    } else {
        ensure!(params.diff_path.is_none(), "`diff_path` needs `trace_path`");
        let which = UnitRef {
            handle: params.handle.clone(),
            project: params.project.clone(),
            unit: params.unit.clone(),
        };
        let loaded = resolve(call, &which)?;
        let mut timing = Timing::default();
        let traced = trace_unit(
            call,
            &mut timing,
            &loaded,
            &params.files,
            params.inline_max_size,
            &TraceOptions::default(),
            Duration::from_millis(params.timeout_ms.unwrap_or(600_000)),
        )?;
        let target = loaded.target(call.session, &mut timing)?;
        let started = Instant::now();
        let diff = target.diff_document(&traced.object, &score_options(&loaded.unit))?;
        timing.score += ms(started.elapsed());
        call.timing.add(&timing);
        let parsed = trace::parse(&traced.trace)?;
        let header = json!({
            "unit": loaded.unit.name,
            "files": params.files.keys().collect::<Vec<_>>(),
            "project_object_identical": traced.identical,
            "relocation_policy": "functionRelocDiffs=data_value",
        });
        (parsed, diff, traced.version, Some(header))
    };
    let mut document = regdiff::analyze(&diff, &parsed, version, &params.function)?;
    let index = document["allocation_index"].as_u64().unwrap_or(0) as usize;
    let allocation = &parsed.allocations[index];
    document["version"] = json!(version.name());
    if let Some(header) = &header {
        document["compilation"] = header.clone();
    }
    let pcode = params
        .pcode
        .then(|| report::pcode(&allocation.before, version::opcode_names(version), false));
    let text = if params.text {
        Some(regdiff_text(
            &document,
            &parsed,
            index,
            header.as_ref(),
            &params,
        )?)
    } else {
        None
    };
    Ok(RegdiffResult {
        document,
        text,
        pcode,
    })
}

/// regdiff's report: the header, the match, and with `inlining` the function's inlining decisions.
fn regdiff_text(
    document: &Value,
    parsed: &Trace,
    index: usize,
    header: Option<&Value>,
    params: &RegdiffParams,
) -> Result<Vec<String>> {
    let mut out = vec![];
    let symbol = &document["symbol"];
    let readable = symbol["demangled_name"]
        .as_str()
        .filter(|s| !s.is_empty())
        .or(symbol["name"].as_str())
        .unwrap_or("?");
    if let Some(header) = header {
        out.push(format!(
            "oracle {}; unit {}; {readable}",
            document["version"].as_str().unwrap_or("?"),
            header["unit"].as_str().unwrap_or("?")
        ));
        if header["files"].as_array().is_some_and(|f| !f.is_empty()) {
            out.push("candidate files: compared with the target only".into());
        } else {
            out.push(format!(
                "oracle object equals the project object: {}",
                if header["project_object_identical"] == true {
                    "yes"
                } else {
                    "NO (the project object is stale: run ninja; or the oracle differs and the explanation may not hold)"
                }
            ));
        }
    }
    if document["target"].is_null() {
        out.push("no target symbol paired with this function".into());
    } else {
        out.push(format!(
            "match {:.2}%  (trace allocation #{index}, by {})",
            symbol["match_percent"].as_f64().unwrap_or(0.0),
            document["selection"].as_str().unwrap_or("?")
        ));
    }
    if params.inlining {
        let allocation = &parsed.allocations[index];
        let previous = if index == 0 {
            0
        } else {
            parsed.allocations[index - 1].inliner_position
        };
        let decisions = inlining::decisions(
            &parsed.inliner[previous..allocation.inliner_position],
            &BTreeMap::new(),
        )?;
        out.push(String::new());
        out.push("== inlining (call: decision - reason) ==".into());
        let lines = report::inlining(&decisions, false);
        if lines.is_empty() {
            out.push("   no calls".into());
        } else {
            out.extend(lines);
        }
    }
    if params.pcode {
        out.push(String::new());
        let version = Version::parse(document["version"].as_str().unwrap_or("GC/1.3.2"))?;
        out.extend(report::pcode(
            &parsed.allocations[index].before,
            version::opcode_names(version),
            false,
        ));
    }
    out.extend(regdiff::report(document, params.full, params.inlining));
    // (one line per entry: the report's sections begin with an empty line inside their first)
    Ok(out
        .iter()
        .flat_map(|l| l.split('\n').map(String::from))
        .collect())
}

#[derive(Debug, Default, Deserialize, JsonSchema)]
#[serde(deny_unknown_fields)]
pub struct AnalyzeParams {
    /// A saved trace, and its compiler's version.
    pub trace_path: PathBuf,
    pub version: Option<String>,
    pub source: Option<String>,
    pub function: Option<String>,
    pub link: Option<String>,
    pub near: Option<i64>,
    /// The parsed records in place of the document, or only the allocations' analyses.
    #[serde(default)]
    pub events: bool,
    #[serde(default)]
    pub allocation_only: bool,
    #[serde(default)]
    pub raw: bool,
}

fn oracle_analyze(_: &mut Call<'_>, params: AnalyzeParams) -> Result<Value> {
    let path = &params.trace_path;
    let text = std::fs::read_to_string(path).with_context(|| format!("read {}", path.display()))?;
    let parsed = trace::parse(&text)?;
    let version = params
        .version
        .as_deref()
        .map(Version::parse)
        .transpose()?
        .unwrap_or(Version::Gc132);
    let (function, link) = (params.function.as_deref(), params.link.as_deref());
    let mut output = if params.events {
        serde_json::to_value(&parsed)?
    } else if params.allocation_only {
        json!(
            parsed
                .allocations
                .iter()
                .filter(|a| function
                    .is_none_or(|q| a.before.function.contains(q) || a.before.link.contains(q))
                    && link.is_none_or(|q| a.before.link == q))
                .map(|a| allocation::analyze(a, version))
                .collect::<Vec<_>>()
        )
    } else {
        document::build(
            &parsed,
            version,
            params.source.as_deref().unwrap_or(""),
            &format!("build/{}/oracle/mwcceppc.exe", version.name()),
            None,
            &[],
            &document::Selection {
                function,
                link,
                near: params.near,
            },
        )?
    };
    if params.raw {
        ensure!(
            !params.events && !params.allocation_only,
            "`raw` needs the schema-6 document"
        );
        output["raw"] = json!(
            if function.is_some_and(|f| !f.is_empty()) || link.is_some() {
                trace::raw_selected(&text, function, link)
            } else {
                text.lines().map(str::to_owned).collect()
            }
        );
    }
    Ok(output)
}

pub(crate) fn methods() -> Vec<Method> {
    vec![
        crate::method!(
            "oracle.trace",
            "a traced compile of a unit (through files given in place of the disk's) or of a source and flags: the schema-6 document, and on request the text views (inlining, allocation, frame, near, instantiation, cse, blocks, locals, exchange, inline cost, pcode, schedule) and the raw trace",
            TraceParams,
            TraceResult,
            oracle_trace
        ),
        crate::method!(
            "oracle.regdiff",
            "a function's register differences against the target, explained from a traced compile's allocation (or from a saved trace and diff document)",
            RegdiffParams,
            RegdiffResult,
            oracle_regdiff
        ),
        crate::method!(
            "oracle.analyze",
            "the schema-6 document of a saved trace (or its records, or its allocations' analyses)",
            AnalyzeParams,
            Value,
            oracle_analyze
        ),
    ]
}
