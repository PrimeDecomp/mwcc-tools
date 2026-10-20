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
    path::{Path, PathBuf},
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
    /// The common subexpression pass's IR, after this pass.
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
    #[serde(default)]
    pub schedule: bool,
    /// The trace's raw lines (the selected function's, when one is).
    #[serde(default)]
    pub raw: bool,
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
    options
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
/// `build/<version>/asm/<path>.s`).
fn target_asm(loaded: &Loaded) -> Option<PathBuf> {
    let target = loaded.unit.target_path.as_ref()?;
    let text = target.to_string_lossy().replace('\\', "/");
    let (directory, object) = text.rsplit_once("/obj/")?;
    let asm = Path::new(&format!("{directory}/asm/{object}")).with_extension("s");
    asm.is_file().then_some(asm)
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
                schedule: views.schedule,
            },
        )?)
    } else {
        None
    };
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
        .then(|| report::pcode(&allocation.before, version::opcode_names(version)));
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
