//! Writing and listing the files of a checkout.
use anyhow::{Context, Result};
use std::{
    fs,
    io::Write,
    path::{Path, PathBuf},
};

/// Write DATA to PATH (relative to ROOT), creating its directories: the path written.
pub fn write_under(root: &Path, path: &str, data: &[u8]) -> Result<PathBuf> {
    let path = root.join(path);
    fs::create_dir_all(path.parent().context("output lacks parent")?)?;
    fs::write(&path, data)?;
    Ok(path)
}

/// Write DATA to PATH through a temporary file beside it, so that PATH is either its old or its new contents.
pub fn write_atomic(path: &Path, data: &[u8]) -> Result<()> {
    let parent = path
        .parent()
        .filter(|p| !p.as_os_str().is_empty())
        .unwrap_or(Path::new("."));
    fs::create_dir_all(parent)?;
    let mut temporary = tempfile::NamedTempFile::new_in(parent)?;
    temporary.write_all(data)?;
    temporary.persist(path)?;
    Ok(())
}

/// The files in DIRECTORY (and its subdirectories if RECURSIVE), sorted; none if it is not a directory.
pub fn files(directory: &Path, recursive: bool) -> Result<Vec<PathBuf>> {
    fn collect(directory: &Path, recursive: bool, out: &mut Vec<PathBuf>) -> Result<()> {
        for entry in fs::read_dir(directory)? {
            let entry = entry?;
            let file_type = entry.file_type()?;
            if file_type.is_dir() && recursive {
                collect(&entry.path(), true, out)?;
            } else if file_type.is_file() {
                out.push(entry.path());
            }
        }
        Ok(())
    }
    let mut out = vec![];
    if directory.is_dir() {
        collect(directory, recursive, &mut out)?;
    }
    out.sort();
    Ok(out)
}
