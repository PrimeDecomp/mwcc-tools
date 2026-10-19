//! Files a run sees in place of the disk's ([`Overlay`]), and sources read as sjiswrap gives them to the compiler
//! (UTF-8 transcoded to Shift JIS, [`transcode`]).
use std::{
    collections::HashMap,
    path::{Component, Path, PathBuf},
    sync::Arc,
};

/// The write time of a file only the overlay has (2020-01-01, as a FILETIME). A file the overlay shadows keeps the
/// disk file's.
pub(crate) const OVERLAY_ONLY_WRITTEN: u64 = 132_223_104_000_000_000;

/// Files that replace the disk's for a run: each path is given bytes, or is absent (as if not on disk). Paths are
/// matched as Windows matches them: absolute (a relative one is from the process's current directory when inserted),
/// with `.` and `..` resolved, ignoring ASCII case. The bytes are shared: a clone is cheap.
#[derive(Clone, Default, Debug)]
pub struct Overlay {
    /// By key: the path as given, and its bytes.
    files: HashMap<String, (PathBuf, Bytes)>,
}

/// An overlaid file's bytes (`None`: absent).
type Bytes = Option<Arc<[u8]>>;

/// PATH lexically made plain: absolute from DIRECTORY, its `.` and `..` resolved and its separators one kind.
pub(crate) fn normal(directory: &Path, path: &Path) -> PathBuf {
    let path = super::plain(&directory.join(path));
    let mut out = PathBuf::new();
    for component in path.components() {
        match component {
            Component::CurDir => {}
            Component::ParentDir => {
                if !matches!(
                    out.components().next_back(),
                    None | Some(Component::RootDir | Component::Prefix(_))
                ) {
                    out.pop();
                }
            }
            other => out.push(other),
        }
    }
    out
}

/// The overlay's key of a plain path: its text, separators unified and ASCII upper case.
fn key(path: &Path) -> String {
    path.to_string_lossy()
        .replace('\\', "/")
        .to_ascii_uppercase()
}

impl Overlay {
    pub fn new() -> Self {
        Self::default()
    }

    fn absolute(path: &Path) -> PathBuf {
        normal(&std::env::current_dir().unwrap_or_default(), path)
    }

    /// PATH reads as BYTES.
    pub fn insert(&mut self, path: impl AsRef<Path>, bytes: impl Into<Arc<[u8]>>) {
        let path = Self::absolute(path.as_ref());
        self.files.insert(key(&path), (path, Some(bytes.into())));
    }

    /// PATH is not there (a query or an open says "not found").
    pub fn insert_absent(&mut self, path: impl AsRef<Path>) {
        let path = Self::absolute(path.as_ref());
        self.files.insert(key(&path), (path, None));
    }

    /// PATH is no longer overlaid.
    pub fn remove(&mut self, path: impl AsRef<Path>) {
        self.files.remove(&key(&Self::absolute(path.as_ref())));
    }

    pub fn is_empty(&self) -> bool {
        self.files.is_empty()
    }

    /// Each entry's key and the SHA-256 of its bytes (`None`: absent), by key: what the overlay gives a run.
    pub(crate) fn fingerprint(&self) -> Vec<(String, Option<[u8; 32]>)> {
        use sha2::{Digest, Sha256};
        let mut out: Vec<_> = self
            .files
            .iter()
            .map(|(k, (_, bytes))| {
                (
                    k.clone(),
                    bytes.as_deref().map(|b| Sha256::digest(b).into()),
                )
            })
            .collect();
        out.sort();
        out
    }

    /// Each overlaid path and its bytes (`None`: absent), in no order.
    pub fn entries(&self) -> impl Iterator<Item = (&Path, Option<&[u8]>)> {
        self.files
            .values()
            .map(|(path, bytes)| (path.as_path(), bytes.as_deref()))
    }

    /// What the overlay says of PLAIN (a [`normal`] path): not overlaid, absent, or its bytes.
    pub(crate) fn get(&self, plain: &Path) -> Option<Option<&Arc<[u8]>>> {
        if self.files.is_empty() {
            return None;
        }
        self.files.get(&key(plain)).map(|(_, bytes)| bytes.as_ref())
    }

    /// The overlaid files of DIRECTORY (a [`normal`] path): each one's name and bytes (`None`: absent).
    pub(crate) fn in_directory(&self, directory: &Path) -> Vec<(String, Option<&Arc<[u8]>>)> {
        let directory = key(directory);
        self.files
            .values()
            .filter(|(path, _)| path.parent().is_some_and(|p| key(p) == directory))
            .filter_map(|(path, bytes)| {
                Some((
                    path.file_name()?.to_string_lossy().into_owned(),
                    bytes.as_ref(),
                ))
            })
            .collect()
    }
}

/// Whether sjiswrap transcodes PATH: its extension is one of a source's or a header's, as spelled (case-sensitively).
pub(crate) fn is_text_file(path: &Path) -> bool {
    path.extension().and_then(|e| e.to_str()).is_some_and(|e| {
        matches!(
            e,
            "c" | "cc"
                | "cp"
                | "cpp"
                | "cxx"
                | "c++"
                | "h"
                | "hh"
                | "hp"
                | "hpp"
                | "hxx"
                | "h++"
                | "pch"
                | "pch++"
                | "inc"
        )
    })
}

/// BYTES (PATH's) as sjiswrap gives them: UTF-8 encoded as Shift JIS (a character Shift JIS lacks becomes `&#N;`),
/// and the warning it writes when there was one. Not UTF-8: sjiswrap's error, which ends the run.
pub(crate) fn transcode(path: &Path, bytes: &[u8]) -> Result<(Vec<u8>, Option<String>), String> {
    let text = std::str::from_utf8(bytes)
        .map_err(|e| format!("sjiswrap: File {} is not valid UTF-8: {e}", path.display()))?;
    let (encoded, _, error) = encoding_rs::SHIFT_JIS.encode(text);
    let warning = error.then(|| {
        format!(
            "sjiswrap: File {} contains Shift JIS encoding errors\n",
            path.display()
        )
    });
    Ok((encoded.into_owned(), warning))
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn paths_match_ignoring_case_and_dots() {
        let root = if cfg!(windows) {
            Path::new(r"C:\work")
        } else {
            Path::new("/work")
        };
        let mut overlay = Overlay::new();
        overlay.insert(root.join("include/Foo.h"), b"x".as_slice());
        overlay.insert_absent(root.join("include/gone.h"));
        let spelled = normal(root, Path::new("src/../INCLUDE/./foo.H"));
        assert_eq!(overlay.get(&spelled).unwrap().unwrap().as_ref(), b"x");
        assert!(
            overlay
                .get(&normal(root, Path::new("include/GONE.h")))
                .unwrap()
                .is_none()
        );
        assert!(
            overlay
                .get(&normal(root, Path::new("include/other.h")))
                .is_none()
        );
        let mut names: Vec<_> = overlay
            .in_directory(&normal(root, Path::new("Include")))
            .into_iter()
            .map(|(n, b)| (n, b.is_some()))
            .collect();
        names.sort();
        assert_eq!(
            names,
            [("Foo.h".to_string(), true), ("gone.h".to_string(), false)]
        );
    }

    #[test]
    fn sources_transcode_as_sjiswrap_does() {
        assert!(is_text_file(Path::new("a/b.cpp")) && is_text_file(Path::new("x.h++")));
        assert!(!is_text_file(Path::new("a/b.CPP")) && !is_text_file(Path::new("a/b.s")));
        // (表's second byte is a backslash)
        let (sjis, warning) = transcode(Path::new("x.c"), "\"表\"".as_bytes()).unwrap();
        assert_eq!(sjis, b"\"\x95\x5c\"");
        assert!(warning.is_none());
        let (sjis, warning) = transcode(Path::new("x.c"), "é".as_bytes()).unwrap();
        assert_eq!(sjis, b"&#233;");
        assert!(warning.is_some());
        assert!(transcode(Path::new("x.c"), b"\x95\x5c").is_err());
    }
}
