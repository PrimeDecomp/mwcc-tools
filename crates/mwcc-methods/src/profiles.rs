//! `unit.inline_sweep` (inline-sweep's measure: units scored at other `inline_max_size` values, the configured
//! object never touched) and `unit.sinit_profile` (sinit-profile's block profile of a function in each object).
use crate::{
    Call, Method, Timing, ms,
    unit::{CompileParams, Loaded, Status, UnitRef, compile_with, resolve, score_options},
};
use anyhow::{Context, Result, bail};
use mwcc_compare::{UnitScore, stores::StoreProfile};
use regex::Regex;
use schemars::JsonSchema;
use serde::{Deserialize, Serialize};
use std::{
    collections::BTreeMap,
    path::{Path, PathBuf},
    sync::Arc,
    time::Instant,
};

#[derive(Debug, Default, Deserialize, JsonSchema)]
#[serde(deny_unknown_fields)]
pub struct SweepParams {
    pub handle: Option<String>,
    pub project: Option<PathBuf>,
    pub unit: Option<String>,
    #[serde(default)]
    pub units: Vec<String>,
    /// The `inline_max_size` values to score each unit at (its configured one is scored too).
    pub sizes: Vec<u32>,
    /// Compiles at once (default 8).
    pub jobs: Option<usize>,
}

#[derive(Debug, Serialize, JsonSchema)]
pub struct Change {
    pub function: String,
    pub before: Option<f32>,
    pub after: Option<f32>,
}

#[derive(Debug, Serialize, JsonSchema)]
pub struct SizeScore {
    pub size: u32,
    pub status: Status,
    /// The size-weighted function match (inline-sweep's).
    pub fuzzy: Option<f64>,
    /// The functions whose match differs from the configured compile's.
    pub changed: Vec<Change>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub console: Option<String>,
}

#[derive(Debug, Serialize, JsonSchema)]
pub struct UnitSweep {
    pub unit: String,
    /// The flags' last `inline_max_size` (none: the sweep adds the pragma).
    pub configured: Option<u32>,
    /// The source sets its own `inline_max_size`, which overrides the flags': its sizes measure nothing.
    pub source_sets: bool,
    pub configured_fuzzy: Option<f64>,
    pub sizes: Vec<SizeScore>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub error: Option<String>,
}

#[derive(Debug, Serialize, JsonSchema)]
pub struct SweepResult {
    pub units: Vec<UnitSweep>,
}

static PRAGMA: std::sync::LazyLock<Regex> =
    std::sync::LazyLock::new(|| Regex::new(r"inline_max_size\((\d+)\)").unwrap());
static SOURCE_PRAGMA: std::sync::LazyLock<Regex> =
    std::sync::LazyLock::new(|| Regex::new(r"(?m)^\s*#\s*pragma\s+inline_max_size").unwrap());

/// The unit compiled with SIZE (none: as configured) and scored: its status, score and console.
fn measure(
    call: &Call<'_>,
    loaded: &Loaded,
    size: Option<u32>,
    timing: &mut Timing,
) -> Result<(Status, Option<UnitScore>, String)> {
    let (result, object) = compile_with(
        call.session,
        call.cancel,
        timing,
        loaded,
        &CompileParams {
            inline_max_size: size,
            ..CompileParams::default()
        },
    )?;
    let Some(object) = object.filter(|_| result.status == Status::Ok) else {
        return Ok((Status::CompileError, None, result.console));
    };
    let target = loaded.target(call.session, timing)?;
    let started = Instant::now();
    let score = target.score(&object, &score_options(&loaded.unit))?;
    timing.score += ms(started.elapsed());
    Ok((Status::Ok, Some(score), result.console))
}

fn percents(score: &UnitScore) -> BTreeMap<String, f32> {
    score
        .functions
        .iter()
        .map(|f| {
            (
                f.demangled.clone().unwrap_or_else(|| f.name.clone()),
                f.match_percent.unwrap_or(0.0),
            )
        })
        .collect()
}

/// Each unit's handles, from the parameters.
fn units(call: &mut Call<'_>, which: &UnitRef, more: &[String]) -> Result<Vec<Arc<Loaded>>> {
    let mut loaded = vec![];
    if which.handle.is_some() || which.unit.is_some() {
        loaded.push(resolve(call, which)?);
    }
    for name in more {
        loaded.push(resolve(
            call,
            &UnitRef {
                project: which.project.clone(),
                unit: Some(name.clone()),
                ..UnitRef::default()
            },
        )?);
    }
    if loaded.is_empty() {
        bail!("give `unit`, `units` or `handle`");
    }
    Ok(loaded)
}

/// Run WORK on each of ITEMS on JOBS threads, in order; the timing added to CALL's.
fn parallel<T: Sync, R: Send>(
    call: &mut Call<'_>,
    items: &[T],
    jobs: usize,
    work: impl Fn(&Call<'_>, &T, &mut Timing) -> R + Sync,
) -> Result<Vec<R>> {
    let shared = &*call;
    let (results, timing) = crate::each(shared, items, jobs, |item, timing| {
        work(shared, item, timing)
    })?;
    call.timing.add(&timing);
    Ok(results)
}

type Measured = Result<(Status, Option<UnitScore>, String), String>;

fn sweep(call: &mut Call<'_>, params: SweepParams) -> Result<SweepResult> {
    if params.sizes.is_empty() {
        bail!("give `sizes`");
    }
    let loaded = units(
        call,
        &UnitRef {
            handle: params.handle.clone(),
            project: params.project.clone(),
            unit: params.unit.clone(),
        },
        &params.units,
    )?;
    // (each unit as configured, then at each size)
    let sizes: Vec<Option<u32>> = std::iter::once(None)
        .chain(params.sizes.iter().copied().map(Some))
        .collect();
    let work: Vec<(usize, Option<u32>)> = (0..loaded.len())
        .flat_map(|u| sizes.iter().map(move |&s| (u, s)))
        .collect();
    let measured: Vec<Measured> = parallel(
        call,
        &work,
        params.jobs.unwrap_or(8),
        |call, &(u, size), timing| {
            measure(call, &loaded[u], size, timing).map_err(|e| format!("{e:#}"))
        },
    )?;
    let mut measured = measured.into_iter();
    let mut out = vec![];
    for unit in &loaded {
        let flags = unit.unit.flags.join(" ");
        let configured = PRAGMA
            .captures_iter(&flags)
            .last()
            .and_then(|c| c[1].parse().ok());
        let source_sets = std::fs::read(unit.unit.source_path())
            .map(|b| SOURCE_PRAGMA.is_match(&String::from_utf8_lossy(&b)))
            .unwrap_or(false);
        let mut sweep = UnitSweep {
            unit: unit.unit.name.clone(),
            configured,
            source_sets,
            configured_fuzzy: None,
            sizes: vec![],
            error: None,
        };
        let base = measured.next().unwrap();
        let rest: Vec<Measured> = measured.by_ref().take(params.sizes.len()).collect();
        let before = match base {
            Ok((Status::Ok, Some(score), _)) => score,
            Ok((_, _, console)) => {
                sweep.error = Some(format!("the configured compile failed: {console}"));
                out.push(sweep);
                continue;
            }
            Err(e) => {
                sweep.error = Some(e);
                out.push(sweep);
                continue;
            }
        };
        sweep.configured_fuzzy = before.fuzzy;
        let before_percents = percents(&before);
        for (&size, result) in params.sizes.iter().zip(rest) {
            sweep.sizes.push(match result {
                Ok((Status::Ok, Some(score), _)) => {
                    let after = percents(&score);
                    let names: std::collections::BTreeSet<&String> =
                        before_percents.keys().chain(after.keys()).collect();
                    SizeScore {
                        size,
                        status: Status::Ok,
                        fuzzy: score.fuzzy,
                        changed: names
                            .into_iter()
                            .filter(|n| before_percents.get(*n) != after.get(*n))
                            .map(|n| Change {
                                function: n.clone(),
                                before: before_percents.get(n).copied(),
                                after: after.get(n).copied(),
                            })
                            .collect(),
                        console: None,
                    }
                }
                Ok((status, _, console)) => SizeScore {
                    size,
                    status,
                    fuzzy: None,
                    changed: vec![],
                    console: Some(console),
                },
                Err(e) => SizeScore {
                    size,
                    status: Status::CompileError,
                    fuzzy: None,
                    changed: vec![],
                    console: Some(e),
                },
            });
        }
        out.push(sweep);
    }
    Ok(SweepResult { units: out })
}

#[derive(Debug, Default, Deserialize, JsonSchema)]
#[serde(deny_unknown_fields)]
pub struct ProfileParams {
    /// Objects to profile, by path.
    #[serde(default)]
    pub objects: Vec<PathBuf>,
    /// A directory of candidates (an experiment's run with `keep_objects`, or try-variants' `--keep`): the first
    /// object in each of its subdirectories.
    pub directory: Option<PathBuf>,
    /// A unit: its source compiled (`current`) and its target (`TARGET`) are profiled too.
    pub handle: Option<String>,
    pub project: Option<PathBuf>,
    pub unit: Option<String>,
    /// Another object to profile as `TARGET`.
    pub target: Option<PathBuf>,
    /// The prefix of the function's name (default `__sinit_`).
    pub function: Option<String>,
}

#[derive(Debug, Serialize, JsonSchema)]
pub struct Profiled {
    pub name: String,
    #[schemars(with = "Option<serde_json::Value>")]
    pub profile: Option<StoreProfile>,
    /// sinit-profile's text: the profile, or why there is none.
    pub line: String,
}

#[derive(Debug, Serialize, JsonSchema)]
pub struct ProfileResult {
    pub profiles: Vec<Profiled>,
}

/// The first object (by path) under DIRECTORY.
fn first_object(directory: &Path) -> Option<PathBuf> {
    let mut found = vec![];
    let mut pending = vec![directory.to_path_buf()];
    while let Some(at) = pending.pop() {
        for entry in std::fs::read_dir(&at).ok()?.flatten() {
            let path = entry.path();
            if path.is_dir() {
                pending.push(path);
            } else if path.extension().is_some_and(|e| e == "o") {
                found.push(path);
            }
        }
    }
    found.sort();
    found.into_iter().next()
}

fn profiled(name: String, object: Option<&[u8]>, prefix: &str) -> Profiled {
    let outcome = object.map(|o| mwcc_compare::stores::profile_object(o, prefix));
    match outcome {
        None => Profiled {
            name,
            profile: None,
            line: "no object".into(),
        },
        Some(Err(e)) => Profiled {
            name,
            profile: None,
            line: format!("{e:#}"),
        },
        Some(Ok(None)) => Profiled {
            name,
            profile: None,
            line: format!("no function starting with {prefix}"),
        },
        Some(Ok(Some(profile))) => Profiled {
            name,
            line: profile.line(),
            profile: Some(profile),
        },
    }
}

fn sinit_profile(call: &mut Call<'_>, params: ProfileParams) -> Result<ProfileResult> {
    let prefix = params.function.as_deref().unwrap_or("__sinit_");
    let mut profiles = vec![];
    let read =
        |path: &Path| std::fs::read(path).with_context(|| format!("read {}", path.display()));
    for path in &params.objects {
        profiles.push(profiled(
            path.display().to_string(),
            Some(&read(path)?),
            prefix,
        ));
    }
    if let Some(directory) = &params.directory {
        let mut subdirectories: Vec<PathBuf> = std::fs::read_dir(directory)
            .with_context(|| format!("read {}", directory.display()))?
            .flatten()
            .map(|e| e.path())
            .filter(|p| p.is_dir())
            .collect();
        // (an experiment's run: its control first, then each kept candidate by its index and name; its sources'
        // directories, and candidates that kept no object, are not objects)
        let run = directory.join("manifest.json").is_file();
        subdirectories.sort_by_key(|p| (!p.ends_with("control"), p.clone()));
        for subdirectory in subdirectories {
            let mut name = subdirectory
                .file_name()
                .unwrap_or_default()
                .to_string_lossy()
                .into_owned();
            if run && (name == "original" || name == "baseline") {
                continue;
            }
            let object = first_object(&subdirectory).map(|p| read(&p)).transpose()?;
            if run && object.is_none() {
                continue;
            }
            if let Some(variant) = std::fs::read(subdirectory.join("variant.json"))
                .ok()
                .and_then(|v| serde_json::from_slice::<serde_json::Value>(&v).ok())
                && let Some(label) = variant["name"].as_str()
            {
                name = format!("{name} {label}");
            }
            profiles.push(profiled(name, object.as_deref(), prefix));
        }
    }
    if params.handle.is_some() || params.unit.is_some() {
        let loaded = resolve(
            call,
            &UnitRef {
                handle: params.handle.clone(),
                project: params.project.clone(),
                unit: params.unit.clone(),
            },
        )?;
        let (result, object) = compile_with(
            call.session,
            call.cancel,
            &mut call.timing,
            &loaded,
            &CompileParams::default(),
        )?;
        let object = object.filter(|_| result.status == Status::Ok);
        profiles.push(profiled("current".into(), object.as_deref(), prefix));
        let target = loaded
            .unit
            .target_path
            .as_ref()
            .map(|p| read(p))
            .transpose()?;
        profiles.push(profiled("TARGET".into(), target.as_deref(), prefix));
    }
    if let Some(target) = &params.target {
        profiles.push(profiled("TARGET".into(), Some(&read(target)?), prefix));
    }
    if profiles.is_empty() {
        bail!("give `objects`, `directory`, a unit or `target`");
    }
    Ok(ProfileResult { profiles })
}

pub(crate) fn methods() -> Vec<Method> {
    vec![
        crate::method!(
            "unit.inline_sweep",
            "inline-sweep's measure: each unit compiled in memory at other `inline_max_size` values and scored, with the functions whose match changes; the configured object is never touched, and a source that sets its own value is reported",
            SweepParams,
            SweepResult,
            sweep
        ),
        crate::method!(
            "unit.sinit_profile",
            "sinit-profile's block profile of a function (default `__sinit_`) in each object: instructions, stmw register, frame, lwz/mr, long store runs",
            ProfileParams,
            ProfileResult,
            sinit_profile
        ),
    ]
}
