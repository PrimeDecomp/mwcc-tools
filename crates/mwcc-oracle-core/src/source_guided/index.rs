//! An immutable corpus of exact function matches. Static predicates reuse the
//! search evaluator; a missing event is never promoted to absence evidence.
use super::{
    inputs,
    objective::{self, Contract, Input, Objective, Purpose, RequiredRelocation, Selection},
    search::{self, Build},
    state,
};
use crate::{
    elf,
    evidence::EvaluationState,
    provenance,
    runner::{self, CheckStatus, Compilation},
};
use anyhow::{Context, Result, ensure};
use serde::{Deserialize, Serialize};
use serde_json::{Value, json};
use std::{
    collections::{BTreeMap, BTreeSet},
    fs,
    path::{Path, PathBuf},
    time::{Duration, Instant},
};

#[derive(Clone, Debug, Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct SourceRange {
    pub first_line: usize,
    pub last_line: usize,
}
#[derive(Clone, Debug, Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct Function {
    pub link: String,
    pub source_range: SourceRange,
}
#[derive(Clone, Debug, Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct Ingest {
    pub format_version: u32,
    pub capture: PathBuf,
    pub object: PathBuf,
    pub target: PathBuf,
    pub source: PathBuf,
    /// Optional when capture is a native search artifact containing `inputs`.
    pub snapshot: Option<PathBuf>,
    pub build: Build,
    pub repository: String,
    /// A declared revision label, bound to exact captured bytes, not a Git attestation.
    pub revision: String,
    pub functions: Vec<Function>,
}
#[derive(Clone, Debug, PartialEq, Eq, Serialize, Deserialize)]
pub struct Relocation {
    pub offset: u32,
    pub kind: u8,
    pub symbol: String,
    pub symbol_kind: u8,
    pub binding: u8,
    pub addend: i32,
}
#[derive(Clone, Debug, PartialEq, Eq, Serialize, Deserialize)]
pub struct MatchEvidence {
    pub bytes_sha256: String,
    pub size: usize,
    pub relocations: Vec<Relocation>,
}
#[derive(Clone, Debug, Serialize, Deserialize)]
pub struct Entry {
    pub format_version: u32,
    pub id: String,
    pub function: Function,
    pub repository: String,
    pub revision: String,
    pub source_sha256: String,
    pub object_sha256: String,
    pub target_sha256: String,
    pub capture_sha256: String,
    pub input_sha256: String,
    pub compiler_version: crate::version::Version,
    pub build: Build,
    pub inputs: inputs::Snapshot,
    pub matched: MatchEvidence,
    pub coverage: Value,
}
#[derive(Debug, Serialize, Deserialize)]
pub struct IngestResult {
    pub format_version: u32,
    pub added: Vec<String>,
    pub rejected: Vec<Value>,
}
#[derive(Clone, Debug, Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct Query {
    pub format_version: u32,
    pub link: Option<String>,
    pub version: Option<crate::version::Version>,
    pub flags: Option<Vec<String>>,
    pub class: u8,
    pub round: usize,
    pub objective: Objective,
    pub relocations: Vec<RequiredRelocation>,
}
#[derive(Debug, Serialize, Deserialize)]
pub struct Hit {
    pub entry: Entry,
    pub source: String,
    pub evaluation: objective::EvaluationReport,
    pub coalescing: Value,
}
#[derive(Debug, Serialize, Deserialize)]
pub struct QueryResult {
    pub format_version: u32,
    pub hits: Vec<Hit>,
    pub rejected: Vec<Value>,
}

/// Strict symbolic relocation identity. Section/anonymous references are rejected
/// rather than guessing equivalence from unchanged relocation placeholders.
fn function_state(bytes: &[u8], link: &str) -> Result<MatchEvidence> {
    let elf = elf::Elf::parse(bytes)?;
    let symbols = elf.symbols()?;
    let selected: Vec<_> = symbols
        .iter()
        .filter(|s| s.name == link && s.kind == 2 && s.size > 0)
        .collect();
    ensure!(
        selected.len() == 1,
        "function symbol is missing or ambiguous: {link}"
    );
    let symbol = selected[0];
    ensure!(
        !elf.sections
            .iter()
            .any(|section| section.kind == 9 && section.info == symbol.section),
        "SHT_REL relocation identity is unsupported"
    );
    let end = symbol
        .value
        .checked_add(symbol.size)
        .context("function extent overflow")?;
    let mut relocations = vec![];
    for r in elf
        .relocations()?
        .into_iter()
        .filter(|r| r.section == symbol.section && r.offset >= symbol.value && r.offset < end)
    {
        ensure!(
            !r.symbol.name.is_empty() && r.symbol.kind != 3,
            "anonymous/section relocation identity is unsupported"
        );
        ensure!(
            symbols.iter().filter(|s| s.name == r.symbol.name).count() == 1,
            "relocation symbol identity is ambiguous"
        );
        relocations.push(Relocation {
            offset: r.offset - symbol.value,
            kind: r.kind,
            symbol: r.symbol.name,
            symbol_kind: r.symbol.kind,
            binding: r.symbol.binding,
            addend: r.addend,
        });
    }
    relocations.sort_by(|a, b| {
        (
            a.offset,
            a.kind,
            &a.symbol,
            a.symbol_kind,
            a.binding,
            a.addend,
        )
            .cmp(&(
                b.offset,
                b.kind,
                &b.symbol,
                b.symbol_kind,
                b.binding,
                b.addend,
            ))
    });
    let bytes = elf.function_bytes(symbol)?;
    Ok(MatchEvidence {
        bytes_sha256: state::digest(bytes),
        size: bytes.len(),
        relocations,
    })
}
pub fn exact_match(object: &[u8], target: &[u8], link: &str) -> Result<MatchEvidence> {
    let a = function_state(object, link)?;
    ensure!(
        a == function_state(target, link)?,
        "function bytes or symbolic relocations differ"
    );
    Ok(a)
}
fn compilation(bytes: &[u8], object: Vec<u8>) -> Result<Compilation> {
    let mut value: Value = serde_json::from_slice(bytes)?;
    let value = value
        .get_mut("compilation")
        .map(Value::take)
        .unwrap_or(value);
    let mut c: Compilation = serde_json::from_value(value)?;
    ensure!(
        matches!(c.check_status, CheckStatus::Verified) && c.identical == Some(true),
        "stock equivalence must be verified"
    );
    ensure!(
        state::digest(&object) == c.object_sha256,
        "capture object digest differs"
    );
    ensure!(
        c.version.has_oracle() && c.trace_options.backend && c.trace_present,
        "supported backend trace is required"
    );
    c.object = object;
    Ok(c)
}
fn source_range(source: &[u8], range: &SourceRange) -> Result<String> {
    let text = std::str::from_utf8(source).context("index source must be UTF-8")?;
    let lines: Vec<_> = text.split_inclusive('\n').collect();
    ensure!(
        range.first_line > 0
            && range.first_line <= range.last_line
            && range.last_line <= lines.len(),
        "source range outside captured source"
    );
    Ok(lines[range.first_line - 1..range.last_line].concat())
}
fn request(build: &Build) -> Result<runner::CompileRequest> {
    // Reuse the native compiler request contract, including Ninja validation.
    let contract = Contract {
        format_version: 1,
        selection: Selection {
            link: "index".into(),
            class: 4,
            round: 0,
        },
        objective: Objective::SymbolAddressPairs {
            symbol: "index".into(),
            minimum: 0,
            maximum: 0,
        },
        relocations: vec![],
        purpose: Purpose::MechanismReproducer,
    };
    let plan = search::Plan {
        format_version: 1,
        build: build.clone(),
        contract,
        transformations: vec![],
        maximum_depth: 0,
        generation_limit: 1,
        semantic_check: None,
        target: None,
    };
    search::validate(&plan)?;
    search::request(&plan)
}
fn validate_inputs(c: &Compilation, snapshot: &inputs::Snapshot, source: &[u8]) -> Result<()> {
    ensure!(
        snapshot.format_version == 1 && snapshot.reusable && snapshot.cache_limitations.is_empty(),
        "input closure is not reusable"
    );
    ensure!(
        state::digest(source) == c.source_sha256 && snapshot.source_sha256 == c.source_sha256,
        "source identity differs"
    );
    let flags: Vec<_> = c
        .flags
        .iter()
        .filter(|f| !matches!(f.as_str(), "-MD" | "-MMD" | "-c" | "--"))
        .cloned()
        .collect();
    ensure!(
        snapshot.flags == flags
            && snapshot.trace_options == serde_json::to_value(&c.trace_options)?,
        "snapshot compiler flags or traces differ"
    );
    ensure!(
        snapshot
            .dependencies
            .iter()
            .any(|f| f.path == snapshot.source && f.sha256 == c.source_sha256),
        "source absent from input closure"
    );
    ensure!(
        snapshot
            .runtime
            .iter()
            .any(|f| f.sha256 == c.producer_sha256)
            && c.original_sha256
                .as_ref()
                .is_some_and(|sha| snapshot.runtime.iter().any(|f| &f.sha256 == sha)),
        "compiler identity absent from runtime closure"
    );
    Ok(())
}
fn effective_flags(flags: &[String]) -> Vec<String> {
    flags
        .iter()
        .filter(|f| !matches!(f.as_str(), "-MD" | "-MMD" | "-c" | "--"))
        .cloned()
        .collect()
}
fn blob(root: &Path, sha: &str) -> Result<Vec<u8>> {
    ensure!(
        sha.len() == 64 && sha.bytes().all(|b| b.is_ascii_hexdigit()),
        "invalid blob digest"
    );
    let bytes = fs::read(root.join("blobs").join(sha))?;
    ensure!(state::digest(&bytes) == sha, "index blob was modified");
    Ok(bytes)
}
fn put(root: &Path, bytes: &[u8]) -> Result<String> {
    let sha = state::digest(bytes);
    let path = root.join("blobs").join(&sha);
    if path.exists() {
        blob(root, &sha)?;
    } else {
        fs::write(path, bytes)?;
    }
    Ok(sha)
}
pub fn ingest(root: &Path, spec: &Ingest) -> Result<IngestResult> {
    ensure!(
        spec.format_version == 1
            && !spec.repository.is_empty()
            && !spec.revision.is_empty()
            && !spec.functions.is_empty(),
        "invalid index ingestion metadata"
    );
    let capture = fs::read(&spec.capture)?;
    let c = compilation(&capture, fs::read(&spec.object)?)?;
    let snapshot: inputs::Snapshot = if let Some(path) = &spec.snapshot {
        serde_json::from_slice(&fs::read(path)?)?
    } else {
        serde_json::from_value(serde_json::from_slice::<Value>(&capture)?["inputs"].clone())?
    };
    let source = fs::read(&spec.source)?;
    validate_inputs(&c, &snapshot, &source)?;
    let request = request(&spec.build)?;
    ensure!(
        request.version == c.version
            && effective_flags(&request.flags) == effective_flags(&c.flags)
            && request.working_directory.canonicalize()? == snapshot.working_directory,
        "reproduction build disagrees with capture"
    );
    ensure!(
        serde_json::to_value(&request.options)? == snapshot.trace_options,
        "reproduction trace options differ from capture"
    );
    for (path, sha) in [
        (
            request.root.join(format!(
                "build/{}/oracle/mwcceppc.exe",
                request.version.name()
            )),
            c.producer_sha256.as_str(),
        ),
        (
            request.root.join(format!(
                "build/compilers/GC/{}/mwcceppc.exe",
                request.version.compiler_directory()
            )),
            c.original_sha256
                .as_deref()
                .context("stock compiler identity missing")?,
        ),
    ] {
        let path = path.canonicalize()?;
        ensure!(
            snapshot
                .runtime
                .iter()
                .any(|file| file.path == path && file.sha256 == sha)
                && state::digest(&fs::read(&path)?) == sha,
            "reproduction compiler differs from captured runtime"
        );
    }
    let request_source = Path::new(&request.source);
    let request_source = if request_source.is_absolute() {
        request_source.to_owned()
    } else {
        request.working_directory.join(request_source)
    };
    ensure!(
        request_source.canonicalize()? == snapshot.source,
        "reproduction source differs from input closure"
    );
    let input_key = inputs::key(&snapshot)?;
    let target = fs::read(&spec.target)?;
    let report = provenance::parse(&c.trace)?;
    fs::create_dir_all(root.join("blobs"))?;
    fs::create_dir_all(root.join("entries"))?;
    for file in snapshot.dependencies.iter().chain(&snapshot.runtime) {
        let bytes = fs::read(&file.path)
            .with_context(|| format!("input unavailable: {}", file.path.display()))?;
        ensure!(
            state::digest(&bytes) == file.sha256,
            "input changed since capture: {}",
            file.path.display()
        );
        put(root, &bytes)?;
    }
    let capture_sha256 = put(root, &capture)?;
    let object_sha256 = put(root, &c.object)?;
    let source_sha256 = put(root, &source)?;
    let target_sha256 = put(root, &target)?;
    let mut result = IngestResult {
        format_version: 1,
        added: vec![],
        rejected: vec![],
    };
    let mut seen = BTreeSet::new();
    for function in &spec.functions {
        ensure!(seen.insert(&function.link), "duplicate indexed link");
        source_range(&source, &function.source_range)?;
        let matched = match exact_match(&c.object, &target, &function.link) {
            Ok(m) => m,
            Err(e) => {
                result
                    .rejected
                    .push(json!({"link":function.link,"reason":format!("{e:#}")}));
                continue;
            }
        };
        let captured: Vec<_> = report
            .history
            .functions
            .iter()
            .filter(|f| f.link == function.link)
            .collect();
        ensure!(
            captured.len() == 1,
            "indexed function requires one captured lifecycle"
        );
        let mut entry = Entry {
            format_version: 1,
            id: String::new(),
            function: function.clone(),
            repository: spec.repository.clone(),
            revision: spec.revision.clone(),
            source_sha256: source_sha256.clone(),
            object_sha256: object_sha256.clone(),
            target_sha256: target_sha256.clone(),
            capture_sha256: capture_sha256.clone(),
            input_sha256: input_key.clone(),
            compiler_version: c.version,
            build: spec.build.clone(),
            inputs: snapshot.clone(),
            matched,
            coverage: json!({"lifecycle":report.lifecycle,"decisions":report.decisions,"function_counter":captured[0].counter_observations,"materialization":captured[0].materialization_observations,"source_range_association":"declared; exact captured source bytes retained","absence_scope":"query-specific complete guarded path only"}),
        };
        entry.id = state::digest(&serde_json::to_vec(&entry)?);
        let path = root.join("entries").join(format!("{}.json", entry.id));
        let bytes = serde_json::to_vec_pretty(&entry)?;
        if path.exists() {
            ensure!(
                fs::read(&path)? == bytes,
                "index entry collision or modification"
            );
        } else {
            fs::write(path, bytes)?;
        }
        result.added.push(entry.id);
    }
    Ok(result)
}
pub fn load(root: &Path, id: &str) -> Result<Entry> {
    ensure!(
        id.len() == 64 && id.bytes().all(|b| b.is_ascii_hexdigit()),
        "invalid entry ID"
    );
    let entry: Entry =
        serde_json::from_slice(&fs::read(root.join("entries").join(format!("{id}.json")))?)?;
    let mut unsigned = entry.clone();
    unsigned.id.clear();
    ensure!(
        entry.format_version == 1
            && entry.id == id
            && state::digest(&serde_json::to_vec(&unsigned)?) == id,
        "index entry was modified"
    );
    Ok(entry)
}
fn contract(query: &Query, link: &str) -> Result<Contract> {
    ensure!(query.format_version == 1, "unsupported index query format");
    ensure!(
        !matches!(query.objective, Objective::ChangeRangeCategory { .. }),
        "static corpus queries cannot establish a category transition; use source search with a baseline"
    );
    let c = Contract {
        format_version: 1,
        selection: Selection {
            link: link.into(),
            class: query.class,
            round: query.round,
        },
        objective: query.objective.clone(),
        relocations: query.relocations.clone(),
        purpose: Purpose::MechanismReproducer,
    };
    objective::validate(&c)?;
    Ok(c)
}
fn evaluate(
    entry: &Entry,
    query: &Query,
    c: &Compilation,
    report: &provenance::Report,
    source: &[u8],
) -> Result<Hit> {
    let input = Input {
        compilation: c,
        provenance: report,
        source,
        semantic_check: None,
    };
    let evaluation = objective::evaluate(&contract(query, &entry.function.link)?, &input, &input)?;
    let passes: Vec<_> = report
        .passes
        .iter()
        .filter(|p| p.link == entry.function.link && p.class == query.class)
        .collect();
    Ok(Hit {
        entry: entry.clone(),
        source: source_range(source, &entry.function.source_range)?,
        evaluation,
        coalescing: json!(passes),
    })
}
fn retained(root: &Path, entry: &Entry) -> Result<(Compilation, Vec<u8>)> {
    let source = blob(root, &entry.source_sha256)?;
    let c = compilation(
        &blob(root, &entry.capture_sha256)?,
        blob(root, &entry.object_sha256)?,
    )?;
    ensure!(
        c.version == entry.compiler_version,
        "indexed compiler version differs from capture"
    );
    validate_inputs(&c, &entry.inputs, &source)?;
    ensure!(
        inputs::key(&entry.inputs)? == entry.input_sha256,
        "entry input key differs"
    );
    for file in entry
        .inputs
        .dependencies
        .iter()
        .chain(&entry.inputs.runtime)
    {
        blob(root, &file.sha256)?;
    }
    ensure!(
        exact_match(
            &c.object,
            &blob(root, &entry.target_sha256)?,
            &entry.function.link
        )? == entry.matched,
        "retained match differs"
    );
    Ok((c, source))
}
fn matches_entry(entry: &Entry, query: &Query) -> bool {
    query
        .link
        .as_ref()
        .is_none_or(|l| l == &entry.function.link)
        && query.version.is_none_or(|v| v == entry.compiler_version)
        && query
            .flags
            .as_ref()
            .is_none_or(|f| f == &entry.inputs.flags)
}
pub fn query(root: &Path, query: &Query) -> Result<QueryResult> {
    contract(query, "validation")?;
    let mut paths = fs::read_dir(root.join("entries"))?
        .map(|e| e.map(|e| e.path()))
        .collect::<std::io::Result<Vec<_>>>()?;
    paths.sort();
    let mut result = QueryResult {
        format_version: 1,
        hits: vec![],
        rejected: vec![],
    };
    // Group metadata first so only one translation unit's large trace/report is
    // live at a time, while parsing it just once for all matching functions.
    let mut groups: BTreeMap<String, Vec<Entry>> = BTreeMap::new();
    for path in paths
        .into_iter()
        .filter(|p| p.extension().is_some_and(|e| e == "json"))
    {
        let id = path
            .file_stem()
            .and_then(|s| s.to_str())
            .context("invalid entry filename")?;
        let entry = load(root, id)?;
        if !matches_entry(&entry, query) {
            continue;
        }
        let key = (
            &entry.capture_sha256,
            &entry.source_sha256,
            &entry.object_sha256,
            &entry.target_sha256,
            &entry.input_sha256,
        );
        let key = serde_json::to_string(&key)?;
        groups.entry(key).or_default().push(entry);
    }
    for entries in groups.values() {
        let (c, source) = retained(root, &entries[0])?;
        let report = provenance::parse(&c.trace)?;
        let target = blob(root, &entries[0].target_sha256)?;
        for entry in entries {
            ensure!(
                inputs::key(&entry.inputs)? == entry.input_sha256
                    && c.version == entry.compiler_version
                    && exact_match(&c.object, &target, &entry.function.link)? == entry.matched,
                "indexed evidence differs from retained capture"
            );
            let hit = evaluate(entry, query, &c, &report, &source)?;
            if hit.evaluation.overall.state == EvaluationState::Satisfied {
                result.hits.push(hit);
            } else {
                result.rejected.push(json!({"entry":entry.id,"evaluation":hit.evaluation.overall,"objective":hit.evaluation.objective}));
            }
        }
    }
    Ok(result)
}
/// Recompile without cache reuse, requiring the entire original input identity
/// before and after compilation, then recheck target match and the same predicate.
pub fn verify(
    root: &Path,
    id: &str,
    query: &Query,
    output: &Path,
    seconds: u64,
    cancel_file: &Path,
) -> Result<Hit> {
    ensure!(
        seconds > 0 && !output.exists(),
        "verification requires positive timeout and a new output directory"
    );
    let began = Instant::now();
    let cancelled = || cancel_file.exists() || began.elapsed() >= Duration::from_secs(seconds);
    ensure!(
        !cancelled(),
        "source-index verification cancelled or time budget exceeded"
    );
    let entry = load(root, id)?;
    ensure!(
        matches_entry(&entry, query),
        "entry does not match query link, compiler version or flags"
    );
    let (old, source) = retained(root, &entry)?;
    let old_report = provenance::parse(&old.trace)?;
    ensure!(
        evaluate(&entry, query, &old, &old_report, &source)?
            .evaluation
            .overall
            .state
            == EvaluationState::Satisfied,
        "retained example does not satisfy query"
    );
    ensure!(
        !cancelled(),
        "source-index verification cancelled or time budget exceeded"
    );
    let mut request = request(&entry.build)?;
    request.timeout = request.timeout.min(Duration::from_secs(seconds));
    fs::create_dir_all(output)?;
    let output = output.canonicalize()?;
    let before = inputs::capture(&request, &output.join("before"), &cancelled)?;
    fs::write(
        output.join("before.json"),
        serde_json::to_vec_pretty(&before)?,
    )?;
    ensure!(
        before == entry.inputs,
        "current exact build inputs differ from indexed example"
    );
    let fresh = runner::compile_controlled(&request, &cancelled)?;
    fs::write(output.join("object.o"), &fresh.object)?;
    fs::write(
        output.join("capture.json"),
        serde_json::to_vec(&json!({"compilation":fresh}))?,
    )?;
    let after = inputs::capture(&request, &output.join("after"), &cancelled)?;
    fs::write(
        output.join("after.json"),
        serde_json::to_vec_pretty(&after)?,
    )?;
    ensure!(before == after, "build inputs changed during revalidation");
    ensure!(
        matches!(fresh.check_status, CheckStatus::Verified) && fresh.identical == Some(true),
        "fresh stock equivalence is not verified"
    );
    validate_inputs(&fresh, &after, &source)?;
    ensure!(
        fresh.producer_sha256 == old.producer_sha256
            && fresh.original_sha256 == old.original_sha256,
        "compiler identity changed"
    );
    ensure!(
        exact_match(
            &fresh.object,
            &blob(root, &entry.target_sha256)?,
            &entry.function.link
        )? == entry.matched,
        "fresh function match differs"
    );
    let fresh_report = provenance::parse(&fresh.trace)?;
    let hit = evaluate(&entry, query, &fresh, &fresh_report, &source)?;
    ensure!(
        hit.evaluation.overall.state == EvaluationState::Satisfied,
        "fresh objective is not satisfied"
    );
    ensure!(
        !cancelled(),
        "source-index verification cancelled or time budget exceeded"
    );
    fs::write(
        output.join("verified.json"),
        serde_json::to_vec_pretty(
            &json!({"format_version":1,"entry":id,"query":query,"input_sha256":entry.input_sha256,"fresh_object_sha256":fresh.object_sha256,"evaluation":hit.evaluation,"matched":entry.matched}),
        )?,
    )?;
    Ok(hit)
}
