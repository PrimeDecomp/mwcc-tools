//! Where a version's normal build takes its sources and split from.
//!
//! By default that is this repository's own tree: `src/`, `include/`, `config/sources.json` and
//! `config/<version>`. A version whose `config.json` names a `source_tree` (`gc2`: the vendored GC 2.x
//! decompilation) builds instead from that tree's sources, headers, `lib/` and its own `config/<version>`
//! (its names, splits, compilers and Matching list), with objects under `build/<version>/<tree>/`. Oracle
//! builds never use it: their hooks link by the names of `config/<version>`'s symbols.
use anyhow::{Context, Result};
use regex::Regex;
use serde_json::Value;
use std::{fs, path::Path};

pub struct SourceTree {
    /// Prepended to the tree's paths: empty, or `gc2/`.
    pub prefix: String,
    /// The directory of `symbols.txt` and `splits.txt`.
    pub config_dir: String,
    /// The directory of `compiled/` and `obj/`.
    pub build_dir: String,
}

/// The JSON document at PATH.
pub fn read_json(path: &Path) -> Result<Value> {
    serde_json::from_slice(&fs::read(path).with_context(|| format!("read {}", path.display()))?)
        .with_context(|| format!("parse {}", path.display()))
}

impl SourceTree {
    pub fn of(root: &Path, version: &str, oracle: bool) -> Result<Self> {
        let config = read_json(&root.join(format!("config/{version}/config.json")))?;
        Ok(match config["source_tree"].as_str().filter(|_| !oracle) {
            Some(tree) => Self {
                prefix: format!("{tree}/"),
                config_dir: format!("{tree}/config/{version}"),
                build_dir: format!("build/{version}/{tree}"),
            },
            None => Self {
                prefix: String::new(),
                config_dir: format!("config/{version}"),
                build_dir: format!("build/{version}"),
            },
        })
    }

    /// gc2's tree for VERSION, whether or not the version's normal build uses it (the WebAssembly compiler is built
    /// from it: crate::wasm_build).
    pub fn gc2(version: &str) -> Self {
        Self {
            prefix: "gc2/".into(),
            config_dir: format!("gc2/config/{version}"),
            build_dir: format!("build/{version}/gc2"),
        }
    }

    pub fn vendored(&self) -> bool {
        !self.prefix.is_empty()
    }

    /// The tree's `config/sources.json`.
    pub fn sources(&self, root: &Path) -> Result<Value> {
        read_json(&root.join(format!("{}config/sources.json", self.prefix)))
    }

    /// `config/<version>/config.json`, with a vendored tree's compilers, overrides, flags and Matching list
    /// (its `state.json`) in place of ours. The original, its SHA-1 and the imports stay ours.
    pub fn config(&self, root: &Path, ours: &Value) -> Result<Value> {
        if !self.vendored() {
            return Ok(ours.clone());
        }
        let theirs = read_json(&root.join(format!("{}/config.json", self.config_dir)))?;
        let state = read_json(&root.join(format!("{}/state.json", self.config_dir)))?;
        let mut config = ours.clone();
        for key in [
            "compilers",
            "compiler_overrides",
            "flags",
            "sym_on",
            "sym_off",
        ] {
            config[key] = theirs.get(key).cloned().unwrap_or(Value::Null);
        }
        let excluded = state["relink_exclude"].as_object();
        config["matching"] = Value::Array(
            state["matching"]
                .as_array()
                .context("state.json lacks matching")?
                .iter()
                .filter(|s| excluded.is_none_or(|e| !s.as_str().is_some_and(|s| e.contains_key(s))))
                .cloned()
                .collect(),
        );
        Ok(config)
    }

    /// The tree's `-DVERSION` value for `version` (its `include/version.h`), or None for ours. A version the tree
    /// has no number for compiles as the one its config.json's `source_version` names (1.3.2 as 2.7).
    pub fn version_define(&self, root: &Path, version: &str) -> Result<Option<usize>> {
        if !self.vendored() {
            return Ok(None);
        }
        let config = read_json(&root.join(format!("config/{version}/config.json")))?;
        let version = config["source_version"].as_str().unwrap_or(version);
        let header = fs::read_to_string(root.join(format!("{}include/version.h", self.prefix)))?;
        let name = format!("VERSION_{}", version.to_uppercase());
        let pattern = Regex::new(&format!(r"(?m)^#define {name} (\d+)"))?;
        let value = pattern
            .captures(&header)
            .with_context(|| format!("{}include/version.h lacks {name}", self.prefix))?;
        Ok(Some(value[1].parse()?))
    }
}
