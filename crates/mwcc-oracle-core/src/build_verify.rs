//! Sequential build verification; never infer success from an earlier completion marker.
use crate::build_config::{self, Platform};
use anyhow::{Context, Result, ensure};
use serde::Serialize;
use serde_json::Value;
use std::{
    collections::BTreeSet,
    fs,
    path::{Path, PathBuf},
    process::{Command, Stdio},
    time::Duration,
};

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Mode {
    Verify,
    Gates,
}
#[derive(Clone, Debug, PartialEq, Eq, Serialize)]
pub struct Step {
    pub version: String,
    pub mode: Option<String>,
    pub split_only: bool,
    pub target: Option<String>,
}
impl Step {
    pub fn label(&self) -> String {
        format!(
            "{}{}",
            self.version,
            if self.mode.as_deref() == Some("link") {
                " link"
            } else {
                ""
            }
        )
    }
}
pub fn plan(mode: Mode) -> Vec<Step> {
    let normal = if mode == Mode::Verify {
        build_config::VERSIONS
            .into_iter()
            .filter(|v| *v != build_config::PRIMARY)
            .chain(std::iter::once(build_config::PRIMARY))
            .collect::<Vec<_>>()
    } else {
        vec![
            "GC_1_1",
            "GC_1_1p1",
            "GC_1_2_5",
            "GC_1_2_5n",
            "GC_1_0",
            "GC_1_3",
        ]
    };
    let mut steps: Vec<_> = normal
        .into_iter()
        .map(|v| Step {
            version: v.into(),
            mode: None,
            split_only: false,
            target: None,
        })
        .collect();
    if mode == Mode::Gates {
        steps.extend(
            ["GC_1_3", "GC_1_3_2", "GC_2_6", "GC_2_7"]
                .into_iter()
                .map(|v| Step {
                    version: v.into(),
                    mode: Some("link".into()),
                    split_only: v != "GC_1_3",
                    target: Some(format!("build/{v}/ok")),
                }),
        );
    }
    steps
}
#[derive(Debug)]
pub struct Request {
    pub root: PathBuf,
    pub mode: Mode,
    pub tools: String,
    pub ninja: PathBuf,
    pub jobs: Option<usize>,
    pub timeout: Duration,
}
pub struct CommandResult {
    pub success: bool,
    pub output: String,
}
pub trait Backend {
    fn configure(&mut self, root: &Path, request: &build_config::Request) -> Result<()>;
    fn ninja(&mut self, request: &Request, targets: &[String]) -> Result<CommandResult>;
}
struct Native;
impl Backend for Native {
    fn configure(&mut self, root: &Path, request: &build_config::Request) -> Result<()> {
        build_config::run(root, request)?;
        Ok(())
    }
    fn ninja(&mut self, request: &Request, targets: &[String]) -> Result<CommandResult> {
        let log = crate::temporary::diagnostic_log()?;
        let stream = log.reopen()?;
        let mut command = Command::new(&request.ninja);
        command
            .current_dir(&request.root)
            .args(targets)
            .stdout(Stdio::from(stream.try_clone()?))
            .stderr(Stdio::from(stream));
        if let Some(jobs) = request.jobs {
            command.args(["-j", &jobs.to_string()]);
        }
        crate::process_tree::prepare(&mut command);
        let mut child = command
            .spawn()
            .with_context(|| format!("launch {command:?}"))?;
        let waited = crate::process_tree::wait(&mut child, request.timeout, &|| false)?;
        if waited.timed_out {
            anyhow::bail!(
                "Ninja timed out after {} seconds",
                request.timeout.as_secs_f64()
            );
        }
        let status = waited.status;
        Ok(CommandResult {
            success: status.success(),
            output: String::from_utf8_lossy(&fs::read(log.path())?).replace("\r\n", "\n"),
        })
    }
}
fn read(root: &Path, name: &str) -> Result<Value> {
    serde_json::from_slice(&fs::read(root.join(name)).with_context(|| format!("read {name}"))?)
        .with_context(|| format!("parse {name}"))
}
/// Legacy verify.py's source-function criterion, independent of linked physical layout.
pub fn inspect_report(
    report: &Value,
    config: &Value,
    functions: &Value,
) -> Result<(String, Vec<String>)> {
    let patched: BTreeSet<_> = functions
        .as_array()
        .context("functions must be an array")?
        .iter()
        .filter(|f| f["binary_patch"].as_bool().unwrap_or(false))
        .map(|f| f["name"].as_str().context("patched function lacks name"))
        .collect::<Result<_>>()?;
    let listed: Option<BTreeSet<_>> = match config.get("matching").filter(|v| !v.is_null()) {
        Some(list) => Some(
            list.as_array()
                .context("matching must be an array")?
                .iter()
                .map(|v| v.as_str().context("matching entry must be text"))
                .collect::<Result<_>>()?,
        ),
        None => None,
    };
    let mut differing = vec![];
    for unit in report["units"]
        .as_array()
        .context("objdiff report lacks units")?
    {
        let Some(source) = unit["metadata"]["source_path"]
            .as_str()
            .filter(|s| !s.is_empty())
        else {
            continue;
        };
        if listed.as_ref().is_some_and(|list| !list.contains(source)) {
            continue;
        }
        if let Some(functions) = unit.get("functions") {
            for function in functions
                .as_array()
                .context("unit functions must be an array")?
            {
                let percent = match function.get("fuzzy_match_percent") {
                    Some(value) => value
                        .as_f64()
                        .context("fuzzy match percent must be numeric")?,
                    None => 0.0,
                };
                let name = function["name"].as_str().context("function lacks name")?;
                if percent < 100.0 && !patched.contains(name.trim_start_matches('_')) {
                    differing.push(format!(
                        "{}/{}",
                        unit["name"].as_str().context("unit lacks name")?,
                        name
                    ));
                }
            }
        }
    }
    let measures = report["measures"]
        .as_object()
        .context("report lacks measures")?;
    let percent = |name: &str| -> Result<String> {
        let value = match measures.get(name) {
            Some(value) => value.as_f64().context("measure must be numeric")?,
            None => 0.0,
        };
        ensure!(value.is_finite(), "measure must be finite");
        Ok(format!("{:.2}%", (value * 100.0).floor() / 100.0))
    };
    let summary = format!(
        "  code {} matched, {} linked; data {} matched, {} linked",
        percent("matched_code_percent")?,
        percent("complete_code_percent")?,
        percent("matched_data_percent")?,
        percent("complete_data_percent")?
    );
    Ok((summary, differing))
}
#[derive(Debug, Serialize)]
pub struct StepResult {
    pub label: String,
    pub passed: bool,
    pub text: String,
}
#[derive(Debug, Serialize)]
pub struct Report {
    pub passed: bool,
    pub steps: Vec<StepResult>,
    pub restoration_error: Option<String>,
}
fn tail(text: &str) -> String {
    let start = text.char_indices().rev().nth(5999).map_or(0, |(i, _)| i);
    text[start..].to_owned()
}
fn one<B: Backend>(request: &Request, step: &Step, backend: &mut B) -> Result<(bool, String)> {
    let targets: Vec<_> = step.target.iter().cloned().collect();
    let result = backend.ninja(request, &targets)?;
    ensure!(result.success, "Ninja failed:\n{}", tail(&result.output));
    let split = request
        .root
        .join(format!("config/{}/splits.txt", step.version))
        .is_file();
    if request.mode == Mode::Verify && split {
        let result = backend.ninja(request, &["progress".into()])?;
        ensure!(
            result.success,
            "Ninja progress failed:\n{}",
            tail(&result.output)
        );
    }
    let marker = fs::read_to_string(request.root.join(format!("build/{}/ok", step.version)))
        .context("successful build lacks a readable completion marker")?;
    if request.mode == Mode::Gates {
        let prefix: String = marker.chars().take(150).collect();
        return Ok((
            true,
            format!("{}: {}", step.label(), prefix.lines().next().unwrap_or("")),
        ));
    }
    let mut text = marker.trim_end().to_owned();
    let mut passed = true;
    if split {
        let (summary, differing) = inspect_report(
            &read(
                &request.root,
                &format!("build/{}/report.json", step.version),
            )?,
            &read(
                &request.root,
                &format!("config/{}/config.json", step.version),
            )?,
            &read(
                &request.root,
                &format!("config/{}/functions.json", step.version),
            )?,
        )?;
        text.push('\n');
        text.push_str(&summary);
        passed = differing.is_empty();
        for name in differing {
            text.push_str(&format!("\n  differs: {name}"));
        }
    }
    Ok((passed, text))
}
pub fn run_with<B: Backend>(
    request: &Request,
    backend: &mut B,
    mut emit: impl FnMut(&StepResult),
) -> Result<Report> {
    ensure!(!request.timeout.is_zero(), "build timeout must be positive");
    ensure!(
        request.jobs.is_none_or(|n| n > 0),
        "build jobs must be positive"
    );
    let mut report = Report {
        passed: true,
        steps: vec![],
        restoration_error: None,
    };
    let mut last_configured = None;
    let mut termination_failed = false;
    let configuration = |version: &str, mode: Option<String>, split_only| build_config::Request {
        version: version.into(),
        mode,
        split_only,
        oracle: false,
        rust_hooks: false,
        tools: request.tools.clone(),
        platform: Platform::current(),
    };
    for step in plan(request.mode) {
        // Configure and wait for this version's Ninja process before overwriting build.ninja.
        last_configured = None;
        let result = backend
            .configure(
                &request.root,
                &configuration(&step.version, step.mode.clone(), step.split_only),
            )
            .and_then(|()| {
                last_configured = Some(step.version.clone());
                one(request, &step, backend)
            });
        let result = match result {
            Ok((passed, text)) => StepResult {
                label: step.label(),
                passed,
                text,
            },
            Err(error) => {
                termination_failed = error
                    .downcast_ref::<crate::process_tree::TerminationFailed>()
                    .is_some();
                StepResult {
                    label: step.label(),
                    passed: false,
                    text: format!("{}: FAILED: {error:#}", step.label()),
                }
            }
        };
        report.passed &= result.passed;
        emit(&result);
        report.steps.push(result);
        if termination_failed {
            break;
        }
    }
    // verify.py leaves PRIMARY configured; extend that guarantee to gates and failure paths.
    if termination_failed {
        report.restoration_error = Some(
            "configuration left in place because the previous process tree could not be stopped"
                .into(),
        );
    } else if last_configured.as_deref() != Some(build_config::PRIMARY)
        && let Err(error) = backend.configure(
            &request.root,
            &configuration(build_config::PRIMARY, None, false),
        )
    {
        report.passed = false;
        report.restoration_error = Some(format!("restore {}: {error:#}", build_config::PRIMARY));
    }
    Ok(report)
}
pub fn run(request: &Request, emit: impl FnMut(&StepResult)) -> Result<Report> {
    run_with(request, &mut Native, emit)
}
