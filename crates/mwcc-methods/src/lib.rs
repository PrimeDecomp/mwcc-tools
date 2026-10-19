//! The method registry: each capability of the engine is a method, a typed function of its parameters (serde, with a
//! JSON schema) that gives one result per input. A [`Session`] keeps what methods load (projects, units, targets)
//! between calls; `mwcc-tools serve` answers requests with it, and `mwcc-tools call` runs one method in a fresh one.
//!
//! **Inputs:** a call's parameters are one input, or, with `inputs: [ … ]`, a list of them, each over the other
//! parameters; a list gives one result per input, in order, each with its `index`, and an input that fails gives a
//! result with its `error` (the call goes on).
//!
//! **Timing:** every result carries `timing` (milliseconds: `load`, `compile`, `score`, `trace`, `model`, `queue`,
//! `other`), and a call's total is its sum.
use anyhow::{Context, Result, anyhow, bail};
use mwcc_project::Project;
use serde::{Serialize, de::DeserializeOwned};
use serde_json::{Map, Value, json};
use std::{
    collections::HashMap,
    path::{Path, PathBuf},
    sync::{Arc, Mutex},
    time::{Duration, Instant},
};

mod colouring;
mod experiment;
mod guided;
mod inline;
mod oracle;
mod profiles;
mod realize;
pub mod reference;
mod residuals;
mod source;
mod unit;

/// The version of the methods' parameters and results: a change that breaks a caller bumps it.
pub const SCHEMA_VERSION: u32 = 1;

/// Where a method's time went, in milliseconds.
#[derive(Clone, Copy, Debug, Default, Serialize, PartialEq)]
pub struct Timing {
    pub load: f64,
    pub compile: f64,
    pub score: f64,
    pub trace: f64,
    pub model: f64,
    pub queue: f64,
    pub other: f64,
}

impl Timing {
    pub fn add(&mut self, other: &Timing) {
        self.load += other.load;
        self.compile += other.compile;
        self.score += other.score;
        self.trace += other.trace;
        self.model += other.model;
        self.queue += other.queue;
        self.other += other.other;
    }

    pub fn total(&self) -> f64 {
        self.load + self.compile + self.score + self.trace + self.model + self.queue + self.other
    }

    /// The share of the time in compiling, scoring and models.
    pub fn productive(&self) -> f64 {
        let total = self.total();
        if total > 0.0 {
            (self.compile + self.score + self.model) / total
        } else {
            0.0
        }
    }
}

/// Milliseconds of DURATION.
pub fn ms(duration: Duration) -> f64 {
    duration.as_secs_f64() * 1e3
}

/// What a session keeps between calls.
pub struct Session {
    /// The checkout of mwcc whose modules run the projects' compilers.
    pub oracle_root: PathBuf,
    /// The project a call names none of.
    pub project: Option<PathBuf>,
    /// The compiles the session's methods have run, answered again while their files are unchanged.
    pub compile_cache: mwcc_project::CompileCache,
    /// The persistent compilers experiments compile candidates with (none with `MWCC_PERSIST=0`; with
    /// `MWCC_PERSIST_VERIFY=1`, each of their candidates is also compiled normally and compared).
    pub persist_pool: Option<mwcc_project::PersistPool>,
    projects: Mutex<HashMap<PathBuf, Arc<Project>>>,
    units: Mutex<HashMap<String, Arc<unit::Loaded>>>,
    /// The traces `trace.load` kept, by handle.
    traces: Mutex<HashMap<String, Arc<colouring::LoadedTrace>>>,
    loaded_traces: std::sync::atomic::AtomicUsize,
    /// The strict level's extern indexes, by project and module ([`Session::externs`]).
    externs: Mutex<HashMap<(PathBuf, String), mwcc_compare::score::Externs>>,
}

impl Session {
    pub fn new(oracle_root: PathBuf, project: Option<PathBuf>) -> Self {
        Self {
            oracle_root,
            project,
            compile_cache: mwcc_project::CompileCache::default(),
            persist_pool: (std::env::var("MWCC_PERSIST").as_deref() != Ok("0")).then(|| {
                mwcc_project::PersistPool::new(mwcc_project::PoolOptions {
                    verify: std::env::var("MWCC_PERSIST_VERIFY").as_deref() == Ok("1"),
                    ..mwcc_project::PoolOptions::default()
                })
            }),
            projects: Mutex::new(HashMap::new()),
            units: Mutex::new(HashMap::new()),
            traces: Mutex::new(HashMap::new()),
            loaded_traces: std::sync::atomic::AtomicUsize::new(0),
            externs: Mutex::new(HashMap::new()),
        }
    }

    /// The strict level's extern indexes of MODULE in PROJECT, built once a session (main's are the base of a
    /// REL's): our side reads the build objects as they were then.
    pub(crate) fn externs(
        &self,
        project: &Project,
        module: &str,
    ) -> Result<mwcc_compare::score::Externs> {
        use mwcc_compare::{score::Externs, strict};
        let mut cache = self.externs.lock().unwrap();
        let key = |m: &str| (project.root.clone(), m.to_string());
        if let Some(e) = cache.get(&key(module)) {
            return Ok(e.clone());
        }
        let main = match cache.get(&key("main")) {
            Some(e) => e.clone(),
            None => {
                let objects = project.module_objects("main")?;
                let e = Externs {
                    target: Arc::new(strict::ExternIndex::new(strict::load_objects(
                        &objects.targets,
                    ))),
                    ours: Arc::new(strict::ExternIndex::new(strict::load_objects(
                        &objects.linked,
                    ))),
                };
                cache.insert(key("main"), e.clone());
                e
            }
        };
        if module == "main" {
            return Ok(main);
        }
        let objects = project.module_objects(module)?;
        let e = Externs {
            target: Arc::new(strict::ExternIndex::layered(
                main.target,
                strict::load_objects(&objects.targets),
            )),
            ours: Arc::new(strict::ExternIndex::layered(
                main.ours,
                strict::load_objects(&objects.linked),
            )),
        };
        cache.insert(key(module), e.clone());
        Ok(e)
    }

    /// Keep TRACE: its handle.
    pub(crate) fn keep_trace(&self, trace: Arc<colouring::LoadedTrace>) -> String {
        let n = self
            .loaded_traces
            .fetch_add(1, std::sync::atomic::Ordering::Relaxed);
        let handle = format!("trace:{}", n + 1);
        self.traces.lock().unwrap().insert(handle.clone(), trace);
        handle
    }

    pub(crate) fn trace(&self, handle: &str) -> Option<Arc<colouring::LoadedTrace>> {
        self.traces.lock().unwrap().get(handle).cloned()
    }

    /// The project at PATH (else the session's), read once.
    pub fn project(&self, path: Option<&Path>) -> Result<Arc<Project>> {
        let path = path
            .or(self.project.as_deref())
            .context("no project: give `project`, or start the session with one")?;
        let key = std::fs::canonicalize(path)
            .with_context(|| format!("project {} missing", path.display()))?;
        if let Some(project) = self.projects.lock().unwrap().get(&key) {
            return Ok(project.clone());
        }
        let project = Arc::new(Project::open(&key)?);
        self.projects.lock().unwrap().insert(key, project.clone());
        Ok(project)
    }
}

/// The context of one input: its session, its cancellation, its timing, and its stream.
pub struct Call<'a> {
    pub session: &'a Session,
    pub cancel: &'a (dyn Fn() -> bool + Sync),
    pub timing: Timing,
    stream: &'a (dyn Fn(Value) + Sync),
    index: Option<usize>,
}

impl Call<'_> {
    /// A result given before the method's last one (a run's row as it finishes), with the input's `index`.
    pub fn emit(&self, mut result: Map<String, Value>) {
        if let Some(index) = self.index {
            result.insert("index".into(), json!(index));
        }
        (self.stream)(Value::Object(result));
    }
}

/// A method: its name, what it does, the schemas of its parameters and results, and its function.
pub struct Method {
    pub name: &'static str,
    pub summary: &'static str,
    pub params: fn() -> Value,
    pub result: fn() -> Value,
    pub run: fn(&mut Call<'_>, Value) -> Result<Value>,
}

/// A method of parameters P and result R, typed: its parameters are read with P's serde (unknown fields refused).
#[macro_export]
macro_rules! method {
    ($name:literal, $summary:literal, $params:ty, $result:ty, $function:path) => {
        $crate::Method {
            name: $name,
            summary: $summary,
            params: || serde_json::to_value(schemars::schema_for!($params)).unwrap(),
            result: || serde_json::to_value(schemars::schema_for!($result)).unwrap(),
            run: |call, params| {
                let params: $params = $crate::parse(params)?;
                let result: $result = $function(call, params)?;
                Ok(serde_json::to_value(result)?)
            },
        }
    };
}

/// PARAMS read as P.
pub fn parse<P: DeserializeOwned>(params: Value) -> Result<P> {
    serde_json::from_value(params).map_err(|e| anyhow!("parameters: {e}"))
}

#[derive(Serialize, schemars::JsonSchema)]
pub struct Hello {
    pub schema_version: u32,
    pub version: String,
    pub methods: Vec<String>,
}

#[derive(serde::Deserialize, schemars::JsonSchema)]
#[serde(deny_unknown_fields)]
pub struct NoParams {}

#[derive(serde::Deserialize, schemars::JsonSchema)]
#[serde(deny_unknown_fields)]
pub struct ListParams {
    /// Only this method (with its schemas).
    pub name: Option<String>,
}

#[derive(Serialize, schemars::JsonSchema)]
pub struct MethodInfo {
    pub name: String,
    pub summary: String,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub params: Option<Value>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub result: Option<Value>,
}

fn hello(_: &mut Call<'_>, _: NoParams) -> Result<Hello> {
    Ok(Hello {
        schema_version: SCHEMA_VERSION,
        version: env!("CARGO_PKG_VERSION").into(),
        methods: methods().iter().map(|m| m.name.to_owned()).collect(),
    })
}

fn list(_: &mut Call<'_>, params: ListParams) -> Result<Vec<MethodInfo>> {
    let chosen: Vec<&Method> = methods()
        .iter()
        .filter(|m| params.name.as_deref().is_none_or(|n| n == m.name))
        .collect();
    if chosen.is_empty() {
        bail!("no method {}", params.name.unwrap_or_default());
    }
    Ok(chosen
        .into_iter()
        .map(|m| MethodInfo {
            name: m.name.into(),
            summary: m.summary.into(),
            params: params.name.is_some().then(|| (m.params)()),
            result: params.name.is_some().then(|| (m.result)()),
        })
        .collect())
}

/// Every method.
pub fn methods() -> &'static [Method] {
    static METHODS: std::sync::OnceLock<Vec<Method>> = std::sync::OnceLock::new();
    METHODS.get_or_init(|| {
        let mut all = vec![
            method!("session.hello", "the schema version, this build's version and the methods", NoParams, Hello, hello),
            method!(
                "methods.list",
                "the methods and what they do; with `name`, that one's parameter and result schemas",
                ListParams,
                Vec<MethodInfo>,
                list
            ),
        ];
        all.extend(unit::methods());
        all.extend(experiment::methods());
        all.extend(residuals::methods());
        all.extend(profiles::methods());
        all.extend(source::methods());
        all.extend(oracle::methods());
        all.extend(colouring::methods());
        all.extend(realize::methods());
        all.extend(inline::methods());
        all.extend(guided::methods());
        all
    })
}

/// METHOD of the session with PARAMS: each result given to EMIT as it is made (its last with its timing, and in a
/// list its `index`), and the call's timing returned. An error of a single input is the call's.
pub fn call(
    session: &Session,
    method: &str,
    params: Value,
    cancel: &(dyn Fn() -> bool + Sync),
    emit: &(dyn Fn(Value) + Sync),
) -> Result<Timing> {
    let method = methods()
        .iter()
        .find(|m| m.name == method)
        .ok_or_else(|| anyhow!("no method {method} (methods.list lists them)"))?;
    let mut params = match params {
        Value::Null => Map::new(),
        Value::Object(map) => map,
        _ => bail!("parameters must be an object"),
    };
    let mut total = Timing::default();
    let mut one = |params: Map<String, Value>, index: Option<usize>| -> Result<Value> {
        let started = Instant::now();
        let mut call = Call {
            session,
            cancel,
            timing: Timing::default(),
            stream: emit,
            index,
        };
        let outcome = (method.run)(&mut call, Value::Object(params));
        // (what the method did not account for)
        call.timing.other += (ms(started.elapsed()) - call.timing.total()).max(0.0);
        total.add(&call.timing);
        let mut result = match (outcome, index) {
            (Ok(Value::Object(map)), _) => map,
            (Ok(value), _) => Map::from_iter([("value".to_owned(), value)]),
            (Err(error), Some(_)) => {
                Map::from_iter([("error".to_owned(), json!(format!("{error:#}")))])
            }
            (Err(error), None) => return Err(error),
        };
        if let Some(index) = index {
            result.insert("index".into(), json!(index));
        }
        result.insert("timing".into(), serde_json::to_value(call.timing)?);
        Ok(Value::Object(result))
    };
    match params.remove("inputs") {
        None => emit(one(params, None)?),
        Some(Value::Array(inputs)) => {
            for (index, input) in inputs.into_iter().enumerate() {
                if cancel() {
                    bail!("cancelled");
                }
                let Value::Object(input) = input else {
                    bail!("inputs[{index}] is not an object");
                };
                let mut merged = params.clone();
                merged.extend(input);
                emit(one(merged, Some(index))?);
            }
        }
        Some(_) => bail!("`inputs` must be a list"),
    }
    Ok(total)
}

#[cfg(test)]
mod tests {
    use super::*;

    fn session() -> Session {
        Session::new(PathBuf::from("."), None)
    }

    fn run(method: &str, params: Value) -> Result<(Vec<Value>, Timing)> {
        let out = Mutex::new(vec![]);
        let timing = call(&session(), method, params, &|| false, &|v| {
            out.lock().unwrap().push(v)
        })?;
        Ok((out.into_inner().unwrap(), timing))
    }

    #[test]
    fn hello_names_the_schema_and_the_methods() {
        let (out, _) = run("session.hello", json!({})).unwrap();
        assert_eq!(out[0]["schema_version"], SCHEMA_VERSION);
        assert!(
            out[0]["methods"]
                .as_array()
                .unwrap()
                .iter()
                .any(|m| m == "unit.compile")
        );
        assert!(out[0]["timing"]["other"].is_number());
    }

    #[test]
    fn lists_give_one_result_per_input_and_errors_in_place() {
        let (out, _) = run(
            "methods.list",
            json!({"inputs": [{"name": "unit.score"}, {"name": "nothing"}]}),
        )
        .unwrap();
        assert_eq!(out.len(), 2);
        assert_eq!(out[0]["index"], 0);
        assert!(out[0]["value"][0]["params"]["properties"].is_object());
        assert_eq!(out[1]["index"], 1);
        assert!(
            out[1]["error"]
                .as_str()
                .unwrap()
                .contains("no method nothing")
        );
    }

    #[test]
    fn unknown_methods_and_parameters_are_refused() {
        assert!(run("no.such", json!({})).is_err());
        let error = run("session.hello", json!({"surprise": 1})).unwrap_err();
        assert!(format!("{error:#}").contains("surprise"), "{error:#}");
    }

    #[test]
    fn every_method_has_schemas() {
        for method in methods() {
            assert!((method.params)().is_object(), "{}", method.name);
            assert!((method.result)().is_object(), "{}", method.name);
        }
    }
}
