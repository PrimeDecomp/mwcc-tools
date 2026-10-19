//! `source.variants`: try-variants' job. Source variants of a unit (the `@@@` variants format, complete candidate
//! files, or a plan's `{files, baseline, candidates}`) compiled in memory and scored, with the unmodified source as
//! the control, each variant's fuzzy match, the chosen function's, and every other function whose match changed.
//! `apply` writes a variant into the working tree (in the file's newline style), refused when the file changed
//! since the run started: the only write to the tree.
use crate::{
    Call, Method,
    experiment::run_loaded,
    unit::{CompileParams, Status, UnitRef, compile_with, resolve, score_options},
};
use anyhow::{Context, Result, bail};
use mwcc_experiment::{
    plan::{Candidate, Files, Plan, ScoreStage, Space, Stage},
    run::{Status as RowStatus, find_function},
};
use schemars::JsonSchema;
use serde::{Deserialize, Serialize};

use std::{collections::BTreeMap, path::PathBuf};

#[derive(Debug, Default, Deserialize, JsonSchema)]
#[serde(deny_unknown_fields)]
pub struct VariantsParams {
    pub handle: Option<String>,
    pub project: Option<PathBuf>,
    pub unit: Option<String>,
    /// Variants in the `### name / @@@ old / @@@ new` format, as text or a file; they edit the unit's source.
    pub variants: Option<String>,
    pub variants_path: Option<PathBuf>,
    /// Complete candidates: a source's text or file (`text`, `path`), or files by path (`files`).
    #[serde(default)]
    pub candidates: Vec<SourceCandidate>,
    /// The files candidates may change (default: the unit's source), and the baseline's in place of the disk's.
    #[serde(default)]
    pub files: Vec<String>,
    #[schemars(with = "BTreeMap<String, Option<String>>")]
    #[serde(default)]
    pub baseline: Files,
    /// The function reported first (mangled, demangled or short name).
    pub function: Option<String>,
    pub jobs: Option<usize>,
    /// Keep the run (each candidate's files, object and console) in this new directory.
    pub keep: Option<PathBuf>,
    /// Write this variant into the working tree after the run.
    pub apply: Option<String>,
}

#[derive(Debug, Default, Deserialize, JsonSchema)]
#[serde(deny_unknown_fields)]
pub struct SourceCandidate {
    /// Its name (default: its path, or `candidate N`).
    pub name: Option<String>,
    pub path: Option<PathBuf>,
    pub text: Option<String>,
    #[schemars(with = "BTreeMap<String, Option<String>>")]
    #[serde(default)]
    pub files: Files,
}

#[derive(Debug, Serialize, JsonSchema)]
pub struct Chosen {
    pub function: String,
    pub demangled: Option<String>,
    pub percent: Option<f32>,
}

#[derive(Debug, Serialize, JsonSchema)]
pub struct Changed {
    pub function: String,
    pub before: f32,
    pub after: f32,
}

#[derive(Debug, Serialize, JsonSchema)]
pub struct VariantResult {
    pub name: String,
    /// ok, compile_error or evaluation_error.
    pub status: String,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub error: Option<String>,
    pub fuzzy: Option<f64>,
    /// Against the unmodified source's.
    pub delta: Option<f64>,
    /// The chosen function's match, and its change.
    pub percent: Option<f32>,
    pub percent_delta: Option<f32>,
    /// The other functions whose match changed (worst first), by their demangled names.
    pub changed: Vec<Changed>,
    /// The functions paired before that are not now.
    pub unpaired: Vec<String>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub duplicate_of: Option<String>,
    /// Where the run kept it (with `keep`).
    #[serde(skip_serializing_if = "Option::is_none")]
    pub directory: Option<PathBuf>,
}

#[derive(Debug, Serialize, JsonSchema)]
pub struct VariantsResult {
    pub unit: String,
    /// The unmodified source's object equals the build's (else: run ninja, or the source has path-dependent code).
    pub identical_to_build: Option<bool>,
    pub deterministic: Option<bool>,
    pub fuzzy: Option<f64>,
    pub function: Option<Chosen>,
    pub variants: Vec<VariantResult>,
    /// The variants format's warnings (an edit that replaces text with itself).
    pub warnings: Vec<String>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub applied: Option<String>,
    /// The files `apply` wrote (or removed).
    pub applied_files: Vec<String>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub output: Option<PathBuf>,
}

fn variants(call: &mut Call<'_>, params: VariantsParams) -> Result<VariantsResult> {
    let loaded = resolve(
        call,
        &UnitRef {
            handle: params.handle.clone(),
            project: params.project.clone(),
            unit: params.unit.clone(),
        },
    )?;
    let unit = &loaded.unit;
    let source = unit.source.replace('\\', "/");
    let mut candidates = vec![];
    let mut warnings = vec![];
    let text = match (&params.variants, &params.variants_path) {
        (Some(_), Some(_)) => bail!("give `variants` or `variants_path`, not both"),
        (Some(text), None) => Some(text.clone()),
        (None, Some(path)) => Some(
            std::fs::read_to_string(path).with_context(|| format!("read {}", path.display()))?,
        ),
        (None, None) => None,
    };
    if let Some(text) = text {
        let (found, found_warnings) = mwcc_experiment::variants::parse(&text)?;
        warnings = found_warnings;
        candidates.extend(found.into_iter().map(|v| Candidate {
            name: v.name,
            edits: BTreeMap::from([(source.clone(), v.edits)]),
            ..Candidate::default()
        }));
    }
    for (index, candidate) in params.candidates.iter().enumerate() {
        let mut files = candidate.files.clone();
        let text = match (&candidate.text, &candidate.path) {
            (Some(text), _) => Some(text.clone()),
            (None, Some(path)) => Some(
                std::fs::read_to_string(path)
                    .with_context(|| format!("read {}", path.display()))?,
            ),
            (None, None) => None,
        };
        if let Some(text) = text {
            files.insert(source.clone(), Some(text));
        }
        let name = candidate
            .name
            .clone()
            .unwrap_or_else(|| match &candidate.path {
                Some(path) => path.display().to_string(),
                None => format!("candidate {}", index + 1),
            });
        candidates.push(Candidate {
            name,
            files,
            ..Candidate::default()
        });
    }
    if candidates.is_empty() {
        bail!("give `variants`, `variants_path` or `candidates`");
    }
    // (the files at the start, for `apply`)
    let declared: Vec<String> = if params.files.is_empty() {
        vec![source.clone()]
    } else {
        params.files.iter().map(|f| f.replace('\\', "/")).collect()
    };
    let read = |file: &str| std::fs::read(unit.directory.join(file)).ok();
    let at_start: BTreeMap<String, Option<Vec<u8>>> =
        declared.iter().map(|f| (f.clone(), read(f))).collect();
    let scratch = tempfile::Builder::new().prefix("mwcc-variants").tempdir()?;
    let output = params
        .keep
        .clone()
        .unwrap_or_else(|| scratch.path().join("run"));
    let plan = Plan {
        output: output.clone(),
        files: params.files.clone(),
        baseline: params.baseline.clone(),
        space: Space::List(candidates.clone()),
        jobs: params.jobs,
        keep_objects: params.keep.is_some(),
        functions: true,
        stages: vec![Stage::Compile, Stage::Score(ScoreStage::default())],
        ..Plan::default()
    };
    let rows = std::sync::Mutex::new(BTreeMap::new());
    let summary = run_loaded(call, &loaded, &plan, &|_, row| {
        rows.lock().unwrap().insert(row.name.clone(), row.clone());
    })?;
    let rows = rows.into_inner().unwrap();
    // (the names and pairing of the control's functions, from its score: the cache has both)
    let (compiled, object) = compile_with(
        call.session,
        call.cancel,
        &mut call.timing,
        &loaded,
        &CompileParams {
            files: plan
                .baseline
                .iter()
                .map(|(k, v)| (k.clone(), v.clone()))
                .collect(),
            ..CompileParams::default()
        },
    )?;
    let base = match object.filter(|_| compiled.status == Status::Ok) {
        Some(object) => loaded
            .target(call.session, &mut call.timing)?
            .score(&object, &score_options(unit))?,
        None => bail!(
            "the unmodified source does not compile: {}",
            compiled.console
        ),
    };
    let chosen = match &params.function {
        Some(query) => {
            Some(find_function(&base, query)?.with_context(|| format!("no function {query}"))?)
        }
        None => None,
    };
    let demangled: BTreeMap<&str, String> = base
        .functions
        .iter()
        .map(|f| {
            (
                f.name.as_str(),
                f.demangled.clone().unwrap_or_else(|| f.name.clone()),
            )
        })
        .collect();
    let before: BTreeMap<&str, Option<f32>> = base
        .functions
        .iter()
        .map(|f| (f.name.as_str(), f.match_percent))
        .collect();
    let mut results = vec![];
    for candidate in &candidates {
        let Some(row) = rows.get(&candidate.name) else {
            continue;
        };
        let percent = chosen.and_then(|c| row.functions.get(&c.name).copied().flatten());
        let mut changed: Vec<Changed> = row
            .functions
            .iter()
            .filter(|(name, _)| chosen.is_none_or(|c| c.name != **name))
            .filter_map(|(name, after)| {
                let (before, after) = ((*before.get(name.as_str())?)?, (*after)?);
                ((after - before).abs() > 0.005).then(|| Changed {
                    function: demangled
                        .get(name.as_str())
                        .cloned()
                        .unwrap_or_else(|| name.clone()),
                    before,
                    after,
                })
            })
            .collect();
        changed.sort_by(|a, b| (a.after - a.before).total_cmp(&(b.after - b.before)));
        let unpaired = if row.status == RowStatus::Ok {
            row.functions
                .iter()
                .filter(|(name, after)| {
                    after.is_none() && before.get(name.as_str()).is_some_and(Option::is_some)
                })
                .map(|(name, _)| {
                    demangled
                        .get(name.as_str())
                        .cloned()
                        .unwrap_or_else(|| name.clone())
                })
                .collect()
        } else {
            vec![]
        };
        let status = serde_json::to_value(row.status)?;
        results.push(VariantResult {
            name: row.name.clone(),
            status: status.as_str().unwrap_or("").into(),
            error: row.error.clone().or_else(|| {
                (row.status == RowStatus::CompileError).then(|| compile_failure(&row.directory))
            }),
            fuzzy: row.fuzzy,
            delta: row.fuzzy.zip(base.fuzzy).map(|(a, b)| a - b),
            percent,
            percent_delta: percent
                .zip(chosen.and_then(|c| c.match_percent))
                .map(|(a, b)| a - b),
            changed,
            unpaired,
            duplicate_of: row.duplicate_of.clone(),
            directory: params.keep.as_ref().map(|_| row.directory.clone()),
        });
    }
    let mut applied = None;
    let mut applied_files = Vec::new();
    if let Some(name) = &params.apply {
        let candidate = candidates
            .iter()
            .find(|c| c.name == *name)
            .with_context(|| format!("apply: no variant named {name:?}"))?;
        if rows.get(name).is_none_or(|r| r.status != RowStatus::Ok) {
            bail!("apply: {name:?} did not compile");
        }
        let baseline: Files = at_start
            .iter()
            .map(|(file, bytes)| {
                let text = plan.baseline.get(file).cloned().unwrap_or_else(|| {
                    bytes
                        .as_ref()
                        .map(|b| String::from_utf8_lossy(b).into_owned())
                });
                (file.clone(), text)
            })
            .collect();
        let rendered = candidate.render(&baseline);
        if let Some(error) = rendered.error {
            bail!("apply: {error}");
        }
        for (file, bytes) in &at_start {
            if read(file) != *bytes {
                bail!("apply: {file} changed since the run started; nothing written");
            }
        }
        for (file, text) in &rendered.files {
            if baseline.get(file) == Some(text) {
                continue;
            }
            let path = unit.directory.join(file);
            applied_files.push(file.clone());
            match text {
                Some(text) => {
                    let original = at_start.get(file).cloned().flatten().unwrap_or_default();
                    let newline = if original.windows(2).any(|w| w == b"\r\n") {
                        "\r\n"
                    } else {
                        "\n"
                    };
                    let text = text.replace("\r\n", "\n").replace('\n', newline);
                    std::fs::write(&path, text)
                        .with_context(|| format!("write {}", path.display()))?;
                }
                None => std::fs::remove_file(&path)
                    .with_context(|| format!("remove {}", path.display()))?,
            }
        }
        applied = Some(name.clone());
    }
    Ok(VariantsResult {
        unit: unit.name.clone(),
        identical_to_build: summary.control.identical_to_build,
        deterministic: summary.control.deterministic,
        fuzzy: base.fuzzy,
        function: chosen.map(|c| Chosen {
            function: c.name.clone(),
            demangled: c.demangled.clone(),
            percent: c.match_percent,
        }),
        variants: results,
        warnings,
        applied,
        applied_files,
        output: params.keep.clone(),
    })
}

/// try-variants' message of a failed compile: its first error lines, else the end of its console.
fn compile_failure(directory: &std::path::Path) -> String {
    let log = std::fs::read_to_string(directory.join("build.log")).unwrap_or_default();
    let errors: Vec<&str> = log.lines().filter(|l| l.contains("rror")).take(4).collect();
    if errors.is_empty() {
        let tail: String = log
            .trim()
            .chars()
            .rev()
            .take(400)
            .collect::<Vec<_>>()
            .into_iter()
            .rev()
            .collect();
        format!("compile failed: {tail}")
    } else {
        format!("compile failed: {}", errors.join("\n      "))
    }
}

pub(crate) fn methods() -> Vec<Method> {
    vec![crate::method!(
        "source.variants",
        "try-variants: source variants (the @@@ format, complete files, or a plan's files/baseline/candidates) compiled in memory and scored against the unmodified source, with the chosen function's match and every function that changed; `apply` writes one into the tree",
        VariantsParams,
        VariantsResult,
        variants
    )]
}
