//! Historical-linker orchestration with retail hooks, supplements and exact SHA gates.
use crate::{coff, executable, pe, process_tree, runner};
use anyhow::{Context, Result, ensure};
use indexmap::IndexMap;
use regex::Regex;
use serde_json::Value;
use sha1::{Digest, Sha1};
use std::{
    collections::{BTreeMap, BTreeSet},
    fs,
    path::{Path, PathBuf},
    process::Stdio,
    thread,
    time::{Duration, Instant},
};
fn basename(path: &str) -> &str {
    path.rsplit(['\\', '/']).next().unwrap()
}
fn split_path(version: &str, path: &str) -> String {
    path.replacen(
        &format!("build/{version}/compiled/"),
        &format!("build/{version}/obj/"),
        1,
    )
}
pub fn usable(root: &Path, version: &str, path: &str) -> Result<bool> {
    let split = root.join(split_path(version, path));
    if !split.is_file() {
        return Ok(false);
    }
    Ok(
        coff::section_sizes(&fs::read(root.join(path))?)?
            == coff::section_sizes(&fs::read(split)?)?,
    )
}
pub fn revert_conflicts(
    root: &Path,
    version: &str,
    inputs: &[String],
    text: &str,
) -> Result<BTreeSet<String>> {
    let compiled: IndexMap<_, _> = inputs
        .iter()
        .filter(|p| p.starts_with(&format!("build/{version}/compiled/")))
        .map(|p| (basename(p), p))
        .collect();
    let undefined = Regex::new(r"Undefined symbol: (\S+) in\s+#\s+(\S+)")?;
    let duplicate = Regex::new(r"Multiple definitions of symbol: (\S+) in\s+#\s+([^\n]+)")?;
    let mut reverted = BTreeSet::new();
    let mut counterparts = None;
    for c in undefined.captures_iter(text) {
        if let Some(path) = compiled.get(&c[2]) {
            reverted.insert((*path).clone());
            continue;
        }
        if counterparts.is_none() {
            counterparts = Some(
                compiled
                    .values()
                    .map(|p| {
                        Ok((
                            (*p).clone(),
                            coff::defined_symbols(&fs::read(root.join(split_path(version, p)))?)?,
                        ))
                    })
                    .collect::<Result<BTreeMap<_, _>>>()?,
            );
        }
        for (path, names) in counterparts.as_ref().unwrap() {
            if names.contains(&c[1]) {
                reverted.insert(path.clone());
            }
        }
    }
    for c in duplicate.captures_iter(text) {
        for name in c[2].split(',').map(str::trim) {
            if let Some(path) = compiled.get(name) {
                reverted.insert((*path).clone());
            }
        }
    }
    Ok(reverted)
}
pub struct Request {
    pub root: PathBuf,
    pub version: String,
    pub output: String,
    pub inputs: Vec<String>,
    pub oracle: bool,
    pub timeout: Duration,
}
#[derive(Debug)]
pub struct Report {
    pub identical: Option<bool>,
    pub sha1: String,
    pub messages: Vec<String>,
    pub linked_from_source: usize,
}
fn write(root: &Path, path: &str, data: &[u8]) -> Result<()> {
    let path = root.join(path);
    fs::create_dir_all(path.parent().context("output lacks parent")?)?;
    fs::write(path, data)?;
    Ok(())
}
fn execute(request: &Request, inputs: &[String]) -> Result<(Option<i32>, String)> {
    let root = request.root.canonicalize()?;
    let executable = root.join("build/compilers/pro53/mwld.exe");
    let mut command = runner::launcher(&root, &executable)?;
    command
        .args([
            "-nostdlib",
            "-subsystem",
            "console",
            "-m",
            "_mainCRTStartup",
        ])
        .args(inputs)
        .args(["-o", &request.output, "-map"])
        .arg(Path::new(&request.output).with_extension("map"))
        .current_dir(root);
    let log = crate::temporary::diagnostic_log()?;
    let stream = log.reopen()?;
    command
        .stdout(Stdio::from(stream.try_clone()?))
        .stderr(Stdio::from(stream));
    process_tree::prepare(&mut command);
    let mut child = command
        .spawn()
        .with_context(|| format!("launch {}", executable.display()))?;
    let start = Instant::now();
    let status = loop {
        if let Some(status) = child.try_wait()? {
            break status;
        }
        if start.elapsed() >= request.timeout {
            process_tree::terminate(&mut child)?;
            anyhow::bail!(
                "linker timed out after {} seconds",
                request.timeout.as_secs_f64()
            );
        }
        thread::sleep(Duration::from_millis(10));
    };
    let data = fs::read(log.path())?;
    let text = data
        .iter()
        .map(|b| char::from(*b))
        .collect::<String>()
        .replace("\r\n", "\n");
    Ok((status.code(), text))
}
pub fn run(request: &Request) -> Result<Report> {
    ensure!(
        !request.timeout.is_zero(),
        "linker timeout must be positive"
    );
    let config: Value = serde_json::from_slice(&fs::read(
        request
            .root
            .join("config")
            .join(&request.version)
            .join("config.json"),
    )?)?;
    let mut inputs = request.inputs.clone();
    let mut messages = vec![];
    let version = &request.version;
    let compiled_prefix = format!("build/{version}/compiled/");
    if config["validate_units"] == true && !request.oracle {
        let mut replaced = BTreeSet::new();
        for input in &inputs {
            if input.starts_with(&compiled_prefix) && !usable(&request.root, version, input)? {
                replaced.insert(input.clone());
            }
        }
        inputs = inputs
            .into_iter()
            .map(|p| {
                if replaced.contains(&p) {
                    split_path(version, &p)
                } else {
                    p
                }
            })
            .collect();
        messages.push(format!(
            "{} compiled units do not fit their ranges and are linked as split",
            replaced.len()
        ));
    }
    let compiled_objects: Vec<_> = inputs
        .iter()
        .filter(|p| p.replace('\\', "/").contains("/oracle/compiled/"))
        .cloned()
        .collect();
    if request.oracle {
        for symbol in config["oracle"]["hooks"]
            .as_array()
            .into_iter()
            .flatten()
            .filter_map(Value::as_str)
        {
            let mut held = vec![];
            for path in &inputs {
                if path.ends_with(".obj")
                    && !path.replace('\\', "/").contains("/oracle/compiled/")
                    && coff::defined_symbols(&fs::read(request.root.join(path))?)?.contains(symbol)
                {
                    held.push(path.clone());
                }
            }
            ensure!(
                held.len() == 1,
                "hook {symbol}: defined by {} objects",
                held.len()
            );
            let patched = format!("build/{version}/oracle/hooked/{}", basename(&held[0]));
            let names = BTreeSet::from([symbol.to_owned()]);
            write(
                &request.root,
                &patched,
                &coff::hook_symbols(&fs::read(request.root.join(&held[0]))?, &names)?,
            )?;
            for input in &mut inputs {
                if *input == held[0] {
                    *input = patched.clone();
                }
            }
        }
    }
    let duplicate = Regex::new(r"Multiple definitions of symbol: (\S+) in\s+#\s+([^\n]+)")?;
    let undefined = Regex::new(r"Undefined symbol: (\S+) in")?;
    let mut attempts = BTreeSet::new();
    loop {
        ensure!(
            attempts.insert(inputs.clone()),
            "linker retry made no progress\n{}",
            messages.join("\n")
        );
        let (code, text) = execute(request, &inputs)?;
        if code == Some(0) {
            break;
        }
        if request.oracle {
            let wanted: BTreeSet<_> = undefined
                .captures_iter(&text)
                .map(|c| c[1].to_owned())
                .collect();
            let mut joined = false;
            if !wanted.is_empty() {
                let mut ours = BTreeSet::new();
                for path in &compiled_objects {
                    ours.extend(coff::defined_symbols(&fs::read(request.root.join(path))?)?);
                }
                for path in &compiled_objects {
                    let split = path.replacen("/oracle/compiled/", "/obj/", 1);
                    let patched = format!("build/{version}/oracle/supplement/{}", basename(&split));
                    if inputs.contains(&patched) || !request.root.join(&split).is_file() {
                        continue;
                    }
                    let defined = coff::defined_symbols(&fs::read(request.root.join(&split))?)?;
                    if defined.difference(&ours).any(|s| wanted.contains(s)) {
                        write(
                            &request.root,
                            &patched,
                            &coff::undefine_symbols(&fs::read(request.root.join(&split))?, &ours)?,
                        )?;
                        inputs.push(patched);
                        joined = true;
                    }
                }
            }
            if joined {
                continue;
            }
            let mut stripped: IndexMap<String, BTreeSet<String>> = IndexMap::new();
            for c in duplicate.captures_iter(&text) {
                let names: BTreeSet<_> = c[2].split(',').map(str::trim).collect();
                let ours: Vec<_> = inputs
                    .iter()
                    .filter(|p| {
                        p.replace('\\', "/").contains("/oracle/compiled/")
                            && names.contains(basename(p))
                    })
                    .collect();
                if ours.len() == 1 {
                    stripped
                        .entry(ours[0].clone())
                        .or_default()
                        .insert(c[1].into());
                }
            }
            if !stripped.is_empty() {
                for (path, names) in stripped {
                    let patched = format!("build/{version}/oracle/patched/{}", basename(&path));
                    write(
                        &request.root,
                        &patched,
                        &coff::undefine_symbols(&fs::read(request.root.join(&path))?, &names)?,
                    )?;
                    for input in &mut inputs {
                        if *input == path {
                            *input = patched.clone();
                        }
                    }
                    messages.push(format!(
                        "{}: {} symbols taken from the original",
                        basename(&path),
                        names.len()
                    ));
                }
                continue;
            }
        }
        let reverted = if config["validate_units"] == true && !request.oracle {
            revert_conflicts(&request.root, version, &inputs, &text)?
        } else {
            BTreeSet::new()
        };
        ensure!(
            !reverted.is_empty(),
            "{}{}",
            if messages.is_empty() {
                String::new()
            } else {
                format!("{}\n", messages.join("\n"))
            },
            text
        );
        messages.push(format!(
            "{} compiled units are linked as split, for the symbols they define or need: {}",
            reverted.len(),
            reverted
                .iter()
                .map(|p| basename(p))
                .collect::<Vec<_>>()
                .join(", ")
        ));
        for input in &mut inputs {
            if reverted.contains(input) {
                *input = split_path(version, input);
            }
        }
    }
    let output = request.root.join(&request.output);
    let mut linked = fs::read(&output)?;
    let built = inputs
        .iter()
        .filter(|p| p.starts_with(&compiled_prefix))
        .count();
    if request.oracle {
        let stub = request.root.join("tools/lmgr_stub/lmgr326b.dll");
        if stub.is_file() {
            fs::copy(
                stub,
                output
                    .parent()
                    .context("oracle output lacks directory")?
                    .join("lmgr326b.dll"),
            )?;
        }
        return Ok(Report {
            identical: None,
            sha1: format!("{:x}", Sha1::digest(&linked)),
            messages,
            linked_from_source: built,
        });
    }
    let original = executable::Original::read(&request.root, version)?;
    let stamp = pe::u32le(&original.data, 0x3c)? as usize + 8;
    linked
        .get_mut(stamp..stamp + 4)
        .context("linked image lacks timestamp field")?
        .copy_from_slice(pe::bytes(&original.data, stamp, 4)?);
    fs::write(&output, &linked)?;
    let actual = format!("{:x}", Sha1::digest(&linked));
    let identical = config["sha1"] == actual;
    let splits = executable::read_splits(&fs::read_to_string(
        request.root.join("config").join(version).join("splits.txt"),
    )?)?;
    if identical {
        write(
            &request.root,
            &format!("build/{version}/ok"),
            format!(
                "{version}: the original, {built} of {} units linked from source{}",
                splits.units.len(),
                if cfg!(windows) { "\r\n" } else { "\n" }
            )
            .as_bytes(),
        )?;
    } else {
        messages.push(format!(
            "{}: SHA-1 is not {}",
            request.output,
            config["sha1"].as_str().unwrap_or("?")
        ));
        if linked.len() != original.data.len() {
            messages.push(format!(
                "  {} bytes, not {}",
                linked.len(),
                original.data.len()
            ));
        }
        let pe = original.pe()?;
        let mut shown = BTreeSet::new();
        for (offset, (a, b)) in linked.iter().zip(&original.data).enumerate() {
            if a == b {
                continue;
            }
            let address = pe.offset_to_address(offset).ok();
            let unit = splits
                .units
                .iter()
                .find(|u| {
                    u.ranges
                        .iter()
                        .any(|r| address.is_some_and(|a| r.start <= a && a < r.end))
                })
                .map_or("-", |u| u.name.as_str());
            if shown.insert(unit.to_owned()) {
                messages.push(format!(
                    "  differs at {} ({unit})",
                    address
                        .map_or_else(|| format!("offset 0x{offset:x}"), |a| format!("0x{a:08x}"))
                ));
                if shown.len() == 10 {
                    break;
                }
            }
        }
    }
    Ok(Report {
        identical: Some(identical),
        sha1: actual,
        messages,
        linked_from_source: built,
    })
}
