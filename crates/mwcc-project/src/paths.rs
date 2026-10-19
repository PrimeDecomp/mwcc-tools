//! A project's paths: a unit's source by its name, the target's build directory and asm, and how paths display.
use anyhow::{Context, Result, bail};
use regex::Regex;
use serde_json::Value;
use std::{
    collections::BTreeSet,
    fs,
    path::{Path, PathBuf},
    sync::LazyLock,
};

pub fn unit_source(from_ninja: Option<&Path>, name: &str) -> Result<String> {
    let root = if let Some(ninja) = from_ninja {
        fs::canonicalize(ninja)?
            .parent()
            .context("Ninja file has no parent")?
            .to_owned()
    } else {
        std::env::current_dir()?
    };
    if root.join(name).is_file() || !root.join("objdiff.json").is_file() {
        return Ok(name.into());
    }
    let config: Value = serde_json::from_slice(&fs::read(root.join("objdiff.json"))?)?;
    let units = config["units"]
        .as_array()
        .context("objdiff.json missing units")?;
    let exact = units.iter().any(|u| u["name"] == name);
    let sources: BTreeSet<_> = units
        .iter()
        .filter(|u| {
            u["name"] == name
                || (!exact
                    && u["name"]
                        .as_str()
                        .is_some_and(|v| v.ends_with(&format!("/{name}"))))
        })
        .filter_map(|u| u["metadata"]["source_path"].as_str())
        .collect();
    if sources.len() != 1 {
        bail!(
            "{name}: not a file, and objdiff.json has {}",
            if sources.is_empty() {
                "no unit with a source by that name".into()
            } else {
                format!(
                    "{} units with sources by that name: {}",
                    sources.len(),
                    sources
                        .iter()
                        .take(5)
                        .copied()
                        .collect::<Vec<_>>()
                        .join(", ")
                )
            }
        );
    }
    Ok(sources.first().unwrap().to_string())
}
pub fn target_directory(from_ninja: &Path) -> Result<PathBuf> {
    static IDS: LazyLock<Regex> = LazyLock::new(|| Regex::new(r"build/(\w+)/").unwrap());
    let ninja = fs::canonicalize(from_ninja)?;
    let root = ninja.parent().context("Ninja file has no parent")?;
    let text = fs::read_to_string(&ninja)?;
    let mut counts: Vec<(String, usize)> = vec![];
    for c in IDS.captures_iter(&text) {
        if let Some((_, n)) = counts.iter_mut().find(|(id, _)| id == &c[1]) {
            *n += 1;
        } else {
            counts.push((c[1].to_owned(), 1));
        }
    }
    counts.sort_by_key(|entry| std::cmp::Reverse(entry.1));
    for (id, _) in counts {
        let path = root.join("build").join(&id);
        if id != "compilers" && path.is_dir() {
            return Ok(path);
        }
    }
    bail!(
        "no target build directory named in {}; give --target DIR",
        from_ninja.display()
    )
}
pub fn target_asm(directory: &Path, source: &str) -> Result<Option<PathBuf>> {
    let source = source.replace('\\', "/");
    let relative = source.strip_prefix("src/").unwrap_or(&source);
    let relative = Path::new(relative).with_extension("s");
    let direct = directory.join("asm").join(&relative);
    if direct.is_file() {
        return Ok(Some(direct));
    }
    let mut found = vec![];
    for entry in fs::read_dir(directory).with_context(|| format!("read {}", directory.display()))? {
        let entry = entry?;
        if entry.path().is_dir() {
            let path = entry.path().join("asm").join(&relative);
            if path.is_file() {
                found.push(path);
            }
        }
    }
    found.sort();
    Ok(found.into_iter().next())
}

/// Display canonical Windows paths without the filesystem-only verbatim prefix.
pub fn display_path(path: &Path) -> String {
    let text = path.to_string_lossy();
    if let Some(rest) = text.strip_prefix(r"\\?\UNC\") {
        format!(r"\\{rest}")
    } else {
        text.strip_prefix(r"\\?\").unwrap_or(&text).to_owned()
    }
}
