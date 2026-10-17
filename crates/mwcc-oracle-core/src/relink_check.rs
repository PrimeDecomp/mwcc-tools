//! Which compiled units of a source tree's build link into the original (crate::source_tree).
//!
//! A vendored tree's link trusts its Matching list, less `state.json`'s `relink_exclude`; this finds that list. The
//! candidates (the tree's Matching sources) are tried with trial links on top of those already accepted, a group at a
//! time, halving a group that does not give the original's SHA-1 down to the units that do not. Those are tried again,
//! one at a time, until no more of them link.
use crate::{build_config, link, source_tree::SourceTree};
use anyhow::{Context, Result, ensure};
use serde_json::{Value, json};
use std::{collections::BTreeSet, fs, path::Path, time::Duration};

pub struct Outcome {
    pub accepted: Vec<String>,
    pub rejected: Vec<String>,
    pub links: usize,
}

/// Puts state.json back as it was unless the new one is kept.
struct Restore<'a> {
    path: &'a Path,
    original: Option<Vec<u8>>,
}

impl Drop for Restore<'_> {
    fn drop(&mut self) {
        if let Some(original) = self.original.take() {
            let _ = fs::write(self.path, original);
        }
    }
}

struct Trial<'a> {
    root: &'a Path,
    version: &'a str,
    inputs: Vec<String>,
    output: String,
    accepted: BTreeSet<String>,
    rejected: Vec<String>,
    links: usize,
}

impl Trial<'_> {
    fn passes(&mut self, chosen: &BTreeSet<String>) -> Result<bool> {
        self.links += 1;
        let inputs = self
            .inputs
            .iter()
            .map(|p| match p.split_once("/compiled/") {
                Some((build, unit)) if !chosen.contains(p) => format!("{build}/obj/{unit}"),
                _ => p.clone(),
            })
            .collect();
        let report = link::run(&link::Request {
            root: self.root.to_path_buf(),
            version: self.version.into(),
            output: self.output.clone(),
            inputs,
            oracle: false,
            source: true,
            record: false,
            timeout: Duration::from_secs(600),
        });
        // (a link error is a unit that does not fit; anything the link put back as split is one too)
        Ok(report.is_ok_and(|r| {
            r.identical == Some(true) && !r.messages.iter().any(|m| m.contains("linked as split"))
        }))
    }

    fn check(&mut self, group: &[String]) -> Result<()> {
        if group.is_empty() {
            return Ok(());
        }
        let chosen = self.accepted.iter().chain(group).cloned().collect();
        if self.passes(&chosen)? {
            self.accepted.extend(group.iter().cloned());
        } else if group.len() == 1 {
            self.rejected.push(group[0].clone());
        } else {
            let (a, b) = group.split_at(group.len() / 2);
            self.check(a)?;
            self.check(b)?;
        }
        Ok(())
    }
}

/// Try VERSION's compiled candidates (built beforehand: ninja all_source). With WRITE, the tree's state.json gets
/// the units that do not link as its `relink_exclude`; without, state.json is left as it was.
pub fn run(root: &Path, version: &str, tools: &str, write: bool) -> Result<Outcome> {
    let tree = SourceTree::of(root, version, false)?;
    ensure!(tree.vendored(), "{version} builds from no source tree");
    let state_path = root.join(&tree.config_dir).join("state.json");
    let original = fs::read(&state_path)?;
    let mut state: Value = serde_json::from_slice(&original)?;
    let mut restore = Restore {
        path: &state_path,
        original: Some(original),
    };
    // (every Matching source is a candidate again, those excluded before too)
    state["relink_exclude"] = json!({});
    fs::write(&state_path, serde_json::to_string_pretty(&state)? + "\n")?;
    let generation = build_config::generate(
        root,
        &build_config::Request {
            version: version.into(),
            mode: Some("link".into()),
            split_only: false,
            oracle: false,
            rust_hooks: false,
            tools: tools.into(),
            platform: build_config::Platform::current(),
        },
    )?;
    let inputs = generation
        .link_inputs
        .context("the version's build has no link")?;
    let prefix = format!("{}/compiled/", tree.build_dir);
    let candidates: Vec<_> = inputs
        .iter()
        .filter(|p| p.starts_with(&prefix))
        .cloned()
        .collect();
    for path in inputs.iter() {
        ensure!(
            root.join(path).is_file(),
            "{path} is not built: run ninja all_source and ninja {}/obj/ok first",
            tree.build_dir
        );
    }
    let mut trial = Trial {
        root,
        version,
        inputs,
        output: format!("{}/trial.exe", tree.build_dir),
        accepted: BTreeSet::new(),
        rejected: vec![],
        links: 0,
    };
    trial.check(&candidates)?;
    // (a unit can need one accepted after it: retry the others until none more link)
    loop {
        let before = trial.accepted.len();
        for path in std::mem::take(&mut trial.rejected) {
            trial.check(&[path])?;
        }
        if trial.accepted.len() == before {
            break;
        }
    }
    let unit = |p: &String| p[prefix.len()..p.len() - ".obj".len()].to_owned();
    let accepted: Vec<_> = trial.accepted.iter().map(unit).collect();
    let rejected: Vec<_> = trial.rejected.iter().map(unit).collect();
    if write {
        state["relink_exclude"] = rejected
            .iter()
            .map(|u| (u.clone(), json!("does not link into the original")))
            .collect::<serde_json::Map<_, _>>()
            .into();
        fs::write(&state_path, serde_json::to_string_pretty(&state)? + "\n")?;
        restore.original = None;
    }
    drop(restore);
    Ok(Outcome {
        accepted,
        rejected,
        links: trial.links,
    })
}
