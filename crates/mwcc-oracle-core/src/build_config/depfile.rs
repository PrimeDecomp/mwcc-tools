use anyhow::{Context, Result};
use std::{
    fs,
    path::{Component, Path, PathBuf},
};
fn normalized(path: &Path) -> PathBuf {
    let mut out = PathBuf::new();
    for component in path.components() {
        match component {
            Component::CurDir => {}
            Component::ParentDir => {
                out.pop();
            }
            other => out.push(other.as_os_str()),
        }
    }
    out
}
fn relative(path: &str, directory: &Path) -> Result<String> {
    let target = normalized(&directory.join(path));
    let directory = normalized(directory);
    let target: Vec<_> = target.components().collect();
    let directory: Vec<_> = directory.components().collect();
    let mut common = 0;
    while common < target.len().min(directory.len()) {
        let a = target[common].as_os_str().to_string_lossy();
        let b = directory[common].as_os_str().to_string_lossy();
        if if cfg!(windows) {
            a.to_lowercase() != b.to_lowercase()
        } else {
            a != b
        } {
            break;
        }
        common += 1;
    }
    let mut out = PathBuf::new();
    for _ in common..directory.len() {
        out.push("..");
    }
    for component in &target[common..] {
        out.push(component.as_os_str());
    }
    Ok(if out.as_os_str().is_empty() {
        ".".into()
    } else {
        out.to_str().context("depfile path is not Unicode")?.into()
    })
}
pub fn convert(data: &[u8], output: &str, directory: &Path) -> Result<String> {
    let text = data
        .iter()
        .map(|b| char::from(*b))
        .collect::<String>()
        .replace("\r\n", "\n")
        .replace('\r', "\n")
        .replace("\\\n", " ");
    let mut paths = vec![];
    for token in text.split_whitespace().skip(1) {
        let mut path = token.replace('\\', "/");
        if path.as_bytes().first().is_some_and(u8::is_ascii_alphabetic)
            && path.as_bytes().get(1..3) == Some(b":/")
        {
            path = path[2..].to_owned();
        }
        if path.starts_with('/') {
            path = relative(&path, directory)?;
        }
        paths.push(path.replace(' ', "\\ "));
    }
    Ok(format!("{output}: {}\n", paths.join(" ")))
}
pub fn run(root: &Path, dependency: &Path, output: &str) -> Result<()> {
    let root = root.canonicalize()?;
    let dependency = root.join(dependency);
    let data = fs::read(&dependency).with_context(|| format!("read {}", dependency.display()))?;
    let text = convert(&data, output, &root)?;
    let text = if cfg!(windows) {
        text.replace('\n', "\r\n")
    } else {
        text
    };
    fs::write(root.join(format!("{output}.d")), text)?;
    fs::remove_file(dependency)?;
    Ok(())
}
