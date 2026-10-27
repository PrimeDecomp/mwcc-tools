//! The disk as a run sees it. Each directory is listed once and each file read once. What the run creates, writes
//! or deletes is kept here, and reaches the disk only when the module ends ([`Files::flush`]). Nothing else is
//! expected to change the run's files while it runs, so what was read stays true until then.
//!
//! The linker opens each object seven times (the driver, its importer plugin and the link each look at it), and on
//! Windows an open costs about 45 µs of the system's time: without this, opening files was most of a link's.
use super::{
    ERROR_ACCESS_DENIED, ERROR_ALREADY_EXISTS, ERROR_FILE_EXISTS, ERROR_FILE_NOT_FOUND,
    ERROR_INVALID_HANDLE, ERROR_PATH_NOT_FOUND, Entry, FILE_ATTRIBUTE_DIRECTORY,
    FILE_ATTRIBUTE_NORMAL, FILE_ATTRIBUTE_READONLY, entry_of, io_error, now_filetime,
};
use std::{
    collections::{BTreeSet, HashMap},
    fs,
    path::{Component, Path, PathBuf, Prefix},
    sync::Arc,
};

const ERROR_DIR_NOT_EMPTY: u32 = 145;
const ERROR_DIRECTORY: u32 = 267;

/// A path as Windows compares paths: absolute, its `.` and `..` resolved by name, `/` between names, and its ASCII
/// letters in upper case.
#[derive(Clone, Debug, PartialEq, Eq, Hash, PartialOrd, Ord)]
pub(crate) struct Key(String);

impl Key {
    /// The key of the directory this one is in (none for a root).
    fn parent(&self) -> Option<Key> {
        let (parent, name) = self.0.rsplit_once('/')?;
        if name.is_empty() {
            return None;
        }
        Some(Key(if parent.contains('/') {
            parent.to_owned()
        } else {
            format!("{parent}/")
        }))
    }
}

/// PATH made absolute, its `.` and `..` resolved by name (as Windows resolves them), and its key.
pub(crate) fn normal(path: &Path) -> (PathBuf, Key) {
    let absolute = std::path::absolute(path).unwrap_or_else(|_| path.to_path_buf());
    let mut prefix = String::new();
    let mut root = false;
    let mut names: Vec<&std::ffi::OsStr> = vec![];
    for component in absolute.components() {
        match component {
            Component::Prefix(p) => {
                prefix = match p.kind() {
                    Prefix::Disk(d) | Prefix::VerbatimDisk(d) => format!("{}:", d as char),
                    Prefix::UNC(server, share) | Prefix::VerbatimUNC(server, share) => format!(
                        r"\\{}\{}",
                        server.to_string_lossy(),
                        share.to_string_lossy()
                    ),
                    _ => p.as_os_str().to_string_lossy().into_owned(),
                }
            }
            Component::RootDir => root = true,
            Component::CurDir => {}
            Component::ParentDir => {
                names.pop();
            }
            Component::Normal(name) => names.push(name),
        }
    }
    let mut out = PathBuf::from(format!(
        "{prefix}{}",
        if root {
            std::path::MAIN_SEPARATOR_STR
        } else {
            ""
        }
    ));
    out.extend(&names);
    let names: Vec<_> = names.iter().map(|n| n.to_string_lossy()).collect();
    let key = format!("{prefix}/{}", names.join("/")).to_ascii_uppercase();
    (out, Key(key))
}

/// What the run sees at a path.
#[derive(Clone)]
pub(crate) enum Found {
    File(Entry),
    Directory(Entry),
    /// Not in its directory.
    NoFile,
    /// Its directory is not there.
    NoPath,
}

impl Found {
    pub(crate) fn entry(&self) -> Option<&Entry> {
        match self {
            Found::File(e) | Found::Directory(e) => Some(e),
            Found::NoFile | Found::NoPath => None,
        }
    }
}

/// What a lookup that finds nothing gives the module (GetLastError's).
pub(crate) fn not_found(found: &Found) -> u32 {
    match found {
        Found::NoPath => ERROR_PATH_NOT_FOUND,
        _ => ERROR_FILE_NOT_FOUND,
    }
}

/// A directory's entries, and each by its name in upper case (Windows compares names case-insensitively).
pub(crate) struct Listing {
    pub(crate) entries: Vec<Entry>,
    by_name: HashMap<String, usize>,
}

fn listing_of(directory: &Path) -> Option<Listing> {
    let mut entries = vec![];
    for e in fs::read_dir(directory).ok()?.flatten() {
        let name = e.file_name().to_string_lossy().into_owned();
        // (a link's entry describes the link: follow it)
        let metadata = match e.file_type() {
            Ok(t) if t.is_symlink() => fs::metadata(e.path()),
            _ => e.metadata(),
        };
        if let Ok(m) = metadata {
            entries.push(entry_of(name, &m));
        }
    }
    let by_name = entries
        .iter()
        .enumerate()
        .map(|(i, e)| (e.name.to_ascii_uppercase(), i))
        .collect();
    Some(Listing { entries, by_name })
}

/// A file's bytes: not read yet, as the disk has them, or as the run wrote them.
enum Data {
    Unread,
    Read(Arc<[u8]>),
    Written(Vec<u8>),
}

/// What the run knows of a path: what the disk has there, or what the run made of it.
enum Node {
    /// A file. ON_DISK: the disk has one there; CHANGED: the run wrote it (DATA is what it wrote).
    File {
        path: PathBuf,
        entry: Entry,
        data: Data,
        on_disk: bool,
        changed: bool,
    },
    /// A directory (CREATED by the run).
    Directory {
        path: PathBuf,
        entry: Entry,
        created: bool,
    },
    /// Nothing, since the run deleted what was there: REMOVED, what of the disk's to delete at the end.
    Deleted {
        path: PathBuf,
        removed: Option<Removed>,
    },
}

#[derive(Clone, Copy, PartialEq, Eq)]
enum Removed {
    File,
    Directory,
}

/// The run's files (see the module).
#[derive(Default)]
pub(crate) struct Files {
    /// Each directory listed, by its key (`None`: it is not there).
    listings: HashMap<Key, Option<Arc<Listing>>>,
    nodes: HashMap<Key, Node>,
    /// The nodes the run changed, in order of their keys (a directory before what is in it).
    changed: BTreeSet<Key>,
    /// Elsewhere than Windows, each path as the file system spells its names ([`Files::resolve`]).
    resolved: HashMap<PathBuf, PathBuf>,
}

impl Files {
    /// DIRECTORY's listing, read once (`None`: it is not there).
    fn listing(&mut self, key: &Key, directory: &Path) -> Option<Arc<Listing>> {
        self.listings
            .entry(key.clone())
            .or_insert_with(|| listing_of(directory).map(Arc::new))
            .clone()
    }

    /// What the run sees at PATH, and its key.
    pub(crate) fn stat(&mut self, path: &Path) -> (Key, Found) {
        let (path, key) = normal(path);
        let found = self.find(&path, &key);
        (key, found)
    }

    fn find(&mut self, path: &Path, key: &Key) -> Found {
        let parent = key.parent();
        // (what the run made of its directory: one it created has only what the run put there)
        let in_created = match parent.as_ref().and_then(|p| self.nodes.get(p)) {
            Some(Node::Directory { created, .. }) => *created,
            Some(Node::File { .. } | Node::Deleted { .. }) => return Found::NoPath,
            None => false,
        };
        match self.nodes.get(key) {
            Some(Node::File { entry, .. }) => return Found::File(entry.clone()),
            Some(Node::Directory { entry, .. }) => return Found::Directory(entry.clone()),
            Some(Node::Deleted { .. }) => return Found::NoFile,
            None if in_created => return Found::NoFile,
            None => {}
        }
        let found = match (path.parent(), path.file_name(), &parent) {
            (Some(directory), Some(name), Some(parent)) => match self.listing(parent, directory) {
                None => Found::NoPath,
                Some(listing) => {
                    let name = name.to_string_lossy();
                    match listing.by_name.get(&name.to_ascii_uppercase()) {
                        Some(&i) => found_of(listing.entries[i].clone()),
                        // (perhaps a short (8.3) alias, which a listing does not have)
                        None if name.contains('~') => metadata_of(path),
                        None => Found::NoFile,
                    }
                }
            },
            // (a root)
            _ => metadata_of(path),
        };
        match &found {
            Found::File(entry) => {
                self.nodes.insert(
                    key.clone(),
                    Node::File {
                        path: path.to_path_buf(),
                        entry: entry.clone(),
                        data: Data::Unread,
                        on_disk: true,
                        changed: false,
                    },
                );
            }
            Found::Directory(entry) => {
                self.nodes.insert(
                    key.clone(),
                    Node::Directory {
                        path: path.to_path_buf(),
                        entry: entry.clone(),
                        created: false,
                    },
                );
            }
            // (what is not there is not kept: the run may create its directory)
            Found::NoFile | Found::NoPath => {}
        }
        found
    }

    /// The entries of DIRECTORY as the run sees it (`None`: it is not there), without `.` and `..`.
    pub(crate) fn entries(&mut self, directory: &Path) -> Option<Vec<Entry>> {
        let (path, key) = normal(directory);
        let mut entries = match self.find(&path, &key) {
            Found::Directory(_) => match self.nodes.get(&key) {
                Some(Node::Directory { created: true, .. }) => vec![],
                _ => self
                    .listing(&key, &path)
                    .map(|l| l.entries.clone())
                    .unwrap_or_default(),
            },
            _ => return None,
        };
        for changed in &self.changed {
            if changed.parent().as_ref() != Some(&key) {
                continue;
            }
            let name = changed.0.rsplit_once('/').map_or("", |(_, n)| n);
            entries.retain(|e| !e.name.eq_ignore_ascii_case(name));
            match self.nodes.get(changed) {
                Some(Node::File { entry, .. } | Node::Directory { entry, .. }) => {
                    entries.push(entry.clone());
                }
                Some(Node::Deleted { .. }) | None => {}
            }
        }
        Some(entries)
    }

    /// The bytes of the file at KEY, read once (`Err`: GetLastError's).
    pub(crate) fn contents(&mut self, key: &Key) -> Result<Arc<[u8]>, u32> {
        match self.loaded(key)? {
            Data::Read(bytes) => Ok(bytes.clone()),
            Data::Written(bytes) => Ok(Arc::from(&bytes[..])),
            Data::Unread => unreachable!(),
        }
    }

    /// The file at KEY's data, read.
    fn loaded(&mut self, key: &Key) -> Result<&mut Data, u32> {
        let Some(Node::File { path, data, .. }) = self.nodes.get_mut(key) else {
            return Err(ERROR_INVALID_HANDLE);
        };
        if let Data::Unread = data {
            *data = Data::Read(fs::read(&*path).map_err(|e| io_error(&e))?.into());
        }
        Ok(data)
    }

    /// Up to BUFFER's length of the file at KEY from AT: how many bytes.
    pub(crate) fn read(&mut self, key: &Key, at: u64, buffer: &mut [u8]) -> Result<usize, u32> {
        let bytes: &[u8] = match self.loaded(key)? {
            Data::Read(bytes) => bytes,
            Data::Written(bytes) => bytes,
            Data::Unread => unreachable!(),
        };
        let start = (at as usize).min(bytes.len());
        let n = (bytes.len() - start).min(buffer.len());
        buffer[..n].copy_from_slice(&bytes[start..start + n]);
        Ok(n)
    }

    /// The file at KEY, to change: its bytes, as the run's from now on.
    fn written(&mut self, key: &Key) -> Result<&mut Vec<u8>, u32> {
        let data = self.loaded(key)?;
        if let Data::Read(bytes) = data {
            let copy = bytes.to_vec();
            *data = Data::Written(copy);
        }
        let Some(Node::File {
            data: Data::Written(bytes),
            entry,
            changed,
            ..
        }) = self.nodes.get_mut(key)
        else {
            unreachable!()
        };
        *changed = true;
        entry.written = now_filetime();
        self.changed.insert(key.clone());
        Ok(bytes)
    }

    /// DATA written to the file at KEY from AT (what is between its end and AT reads as zeros): where it ends.
    pub(crate) fn write(&mut self, key: &Key, at: u64, data: &[u8]) -> Result<u64, u32> {
        let bytes = self.written(key)?;
        let end = at as usize + data.len();
        if bytes.len() < end {
            bytes.resize(end, 0);
        }
        bytes[at as usize..end].copy_from_slice(data);
        let size = bytes.len() as u64;
        self.set_size(key, size);
        Ok(end as u64)
    }

    /// The file at KEY cut, or extended with zeros, to SIZE bytes.
    pub(crate) fn set_len(&mut self, key: &Key, size: u64) -> Result<(), u32> {
        self.written(key)?.resize(size as usize, 0);
        self.set_size(key, size);
        Ok(())
    }

    fn set_size(&mut self, key: &Key, size: u64) {
        if let Some(Node::File { entry, .. }) = self.nodes.get_mut(key) {
            entry.size = size;
        }
    }

    /// The entry of the file at KEY (its size and write time).
    pub(crate) fn entry(&self, key: &Key) -> Result<&Entry, u32> {
        match self.nodes.get(key) {
            Some(Node::File { entry, .. }) => Ok(entry),
            _ => Err(ERROR_INVALID_HANDLE),
        }
    }

    /// An empty file at PATH, in place of what is there: its key. FOUND is what [`Files::stat`] found there.
    pub(crate) fn create(&mut self, path: &Path, found: &Found) -> Key {
        let (path, key) = normal(path);
        let name = found.entry().map_or_else(
            || {
                path.file_name()
                    .map_or_else(String::new, |n| n.to_string_lossy().into_owned())
            },
            |e| e.name.clone(),
        );
        let on_disk = match self.nodes.get(&key) {
            Some(Node::File { on_disk, .. }) => *on_disk,
            Some(Node::Deleted { removed, .. }) => *removed == Some(Removed::File),
            _ => false,
        };
        self.nodes.insert(
            key.clone(),
            Node::File {
                path,
                entry: Entry {
                    name,
                    attributes: FILE_ATTRIBUTE_NORMAL,
                    size: 0,
                    written: now_filetime(),
                    short: String::new(),
                },
                data: Data::Written(vec![]),
                on_disk,
                changed: true,
            },
        );
        self.changed.insert(key.clone());
        key
    }

    /// DeleteFileA's.
    pub(crate) fn delete(&mut self, path: &Path) -> Result<(), u32> {
        let (key, found) = self.stat(path);
        match &found {
            Found::File(e) if e.attributes & FILE_ATTRIBUTE_READONLY != 0 => {
                return Err(ERROR_ACCESS_DENIED);
            }
            Found::File(_) => {}
            Found::Directory(_) => return Err(ERROR_ACCESS_DENIED),
            Found::NoFile | Found::NoPath => return Err(not_found(&found)),
        }
        let Some(Node::File { path, on_disk, .. }) = self.nodes.remove(&key) else {
            unreachable!()
        };
        self.nodes.insert(
            key.clone(),
            Node::Deleted {
                path,
                removed: on_disk.then_some(Removed::File),
            },
        );
        self.changed.insert(key);
        Ok(())
    }

    /// CreateDirectoryA's.
    pub(crate) fn create_directory(&mut self, path: &Path) -> Result<(), u32> {
        let (key, found) = self.stat(path);
        match found {
            Found::File(_) | Found::Directory(_) => return Err(ERROR_ALREADY_EXISTS),
            Found::NoPath => return Err(ERROR_PATH_NOT_FOUND),
            Found::NoFile => {}
        }
        let (path, _) = normal(path);
        // (one the run removed is still on the disk)
        let created = !matches!(
            self.nodes.get(&key),
            Some(Node::Deleted {
                removed: Some(Removed::Directory),
                ..
            })
        );
        let name = path
            .file_name()
            .map_or_else(String::new, |n| n.to_string_lossy().into_owned());
        self.nodes.insert(
            key.clone(),
            Node::Directory {
                path,
                entry: Entry {
                    name,
                    attributes: FILE_ATTRIBUTE_DIRECTORY,
                    size: 0,
                    written: now_filetime(),
                    short: String::new(),
                },
                created,
            },
        );
        self.changed.insert(key);
        Ok(())
    }

    /// RemoveDirectoryA's.
    pub(crate) fn remove_directory(&mut self, path: &Path) -> Result<(), u32> {
        let (key, found) = self.stat(path);
        match &found {
            Found::Directory(_) => {}
            Found::File(_) => return Err(ERROR_DIRECTORY),
            Found::NoFile | Found::NoPath => return Err(not_found(&found)),
        }
        if !self.entries(path).unwrap_or_default().is_empty() {
            return Err(ERROR_DIR_NOT_EMPTY);
        }
        let Some(Node::Directory { path, created, .. }) = self.nodes.remove(&key) else {
            unreachable!()
        };
        self.nodes.insert(
            key.clone(),
            Node::Deleted {
                path,
                removed: (!created).then_some(Removed::Directory),
            },
        );
        self.changed.insert(key);
        Ok(())
    }

    /// Whether the run changed anything.
    pub(crate) fn changed(&self) -> bool {
        !self.changed.is_empty()
    }

    /// Forget the files read and looked up (a persistent run's next candidate reads them again); the directory
    /// listings stay.
    pub(crate) fn forget_files(&mut self) {
        self.nodes.clear();
    }

    /// What the run changed, made on the disk: the directories it created, then the files it deleted, then the files
    /// it wrote, then the directories it removed. Each failure, with its path.
    pub(crate) fn flush(&mut self) -> Vec<(PathBuf, std::io::Error)> {
        let mut errors = vec![];
        let changed: Vec<&Node> = self
            .changed
            .iter()
            .filter_map(|k| self.nodes.get(k))
            .collect();
        for node in &changed {
            if let Node::Directory {
                path,
                created: true,
                ..
            } = node
                && let Err(e) = fs::create_dir_all(path)
            {
                errors.push((path.clone(), e));
            }
        }
        for node in &changed {
            if let Node::Deleted {
                path,
                removed: Some(Removed::File),
            } = node
                && let Err(e) = fs::remove_file(path)
                && e.kind() != std::io::ErrorKind::NotFound
            {
                errors.push((path.clone(), e));
            }
        }
        for node in &changed {
            if let Node::File {
                path,
                data: Data::Written(bytes),
                changed: true,
                ..
            } = node
                && let Err(e) = fs::write(path, bytes)
            {
                errors.push((path.clone(), e));
            }
        }
        for node in changed.iter().rev() {
            if let Node::Deleted {
                path,
                removed: Some(Removed::Directory),
            } = node
                && let Err(e) = fs::remove_dir(path)
                && e.kind() != std::io::ErrorKind::NotFound
            {
                errors.push((path.clone(), e));
            }
        }
        self.changed.clear();
        errors
    }

    /// PATH with each of its names that is not there as spelled replaced by the entry of its directory with that
    /// name ignoring ASCII case (Windows's names, as wibo resolves them on a case-sensitive file system). From the
    /// first name that is not there in any case (a file to create), the rest is kept as it is. Each path once.
    pub(crate) fn resolve(&mut self, path: &Path) -> PathBuf {
        if let Some(resolved) = self.resolved.get(path) {
            return resolved.clone();
        }
        let resolved = self.resolve_once(path);
        self.resolved.insert(path.to_path_buf(), resolved.clone());
        resolved
    }

    fn resolve_once(&mut self, path: &Path) -> PathBuf {
        if fs::symlink_metadata(path).is_ok() {
            return path.to_path_buf();
        }
        let mut out = PathBuf::new();
        let mut components = path.components();
        while let Some(component) = components.next() {
            let Component::Normal(name) = component else {
                out.push(component);
                continue;
            };
            let exact = out.join(name);
            if out.as_os_str().is_empty() || fs::symlink_metadata(&exact).is_ok() {
                out = exact;
                continue;
            }
            let key = name.to_string_lossy().to_ascii_uppercase();
            let (directory, directory_key) = normal(&out);
            match self
                .listing(&directory_key, &directory)
                .and_then(|listing| Some(listing.entries[*listing.by_name.get(&key)?].name.clone()))
            {
                Some(found) => out.push(found),
                None => {
                    out.push(name);
                    out.extend(components);
                    break;
                }
            }
        }
        out
    }
}

fn found_of(entry: Entry) -> Found {
    if entry.attributes & FILE_ATTRIBUTE_DIRECTORY != 0 {
        Found::Directory(entry)
    } else {
        Found::File(entry)
    }
}

/// What the file system says of PATH, asked directly (a path a listing does not answer for).
fn metadata_of(path: &Path) -> Found {
    match fs::metadata(path) {
        Ok(m) => {
            let name = path
                .file_name()
                .map_or_else(String::new, |n| n.to_string_lossy().into_owned());
            found_of(entry_of(name, &m))
        }
        Err(_) if path.parent().is_some_and(|p| !p.exists()) => Found::NoPath,
        Err(_) => Found::NoFile,
    }
}

/// What CreateFileA finds for CREATION (its disposition) at a path where FOUND is: the error it gives, if any, and
/// whether it makes an empty file there.
pub(crate) fn disposition(found: &Found, creation: u32) -> Result<bool, u32> {
    let exists = matches!(found, Found::File(_));
    match creation {
        // CREATE_NEW
        1 if exists => Err(ERROR_FILE_EXISTS),
        1 | 2 | 4 if matches!(found, Found::NoPath) => Err(ERROR_PATH_NOT_FOUND),
        1 | 2 => Ok(true),
        // OPEN_ALWAYS
        4 => Ok(!exists),
        // OPEN_EXISTING, TRUNCATE_EXISTING
        _ if !exists => Err(not_found(found)),
        5 => Ok(true),
        _ => Ok(false),
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn directory(name: &str) -> PathBuf {
        let directory =
            std::env::temp_dir().join(format!("mwcc-wasm-{name}-{}", std::process::id()));
        let _ = fs::remove_dir_all(&directory);
        fs::create_dir_all(&directory).unwrap();
        directory
    }

    fn name(found: &Found) -> Option<String> {
        found.entry().map(|e| e.name.clone())
    }

    #[test]
    fn keys_compare_paths_as_windows_does() {
        let (_, a) = normal(Path::new("/x/y/./../Y/z.o"));
        let (_, b) = normal(Path::new("/x/y/Z.O"));
        assert_eq!(a, b);
        assert_eq!(b.parent(), Some(normal(Path::new("/x/y")).1));
        let (_, root) = normal(Path::new("/"));
        assert_eq!(root.parent(), None);
        assert_eq!(normal(Path::new("/x")).1.parent(), Some(root));
    }

    #[test]
    fn listings_answer_lookups_once() {
        let directory = directory("listings");
        fs::create_dir_all(directory.join("sub")).unwrap();
        fs::write(directory.join("Header.h"), "x").unwrap();
        let mut files = Files::default();
        assert_eq!(
            name(&files.stat(&directory.join("HEADER.H")).1),
            Some("Header.h".into())
        );
        assert!(matches!(
            files.stat(&directory.join("other.h")).1,
            Found::NoFile
        ));
        assert!(matches!(
            files.stat(&directory.join("none/a.h")).1,
            Found::NoPath
        ));
        assert!(matches!(
            files.stat(&directory.join("sub")).1,
            Found::Directory(_)
        ));
        // (the disk is read once: a file made since is not seen)
        fs::write(directory.join("other.h"), "y").unwrap();
        assert!(matches!(
            files.stat(&directory.join("other.h")).1,
            Found::NoFile
        ));
        let mut names: Vec<String> = files
            .entries(&directory)
            .unwrap()
            .into_iter()
            .map(|e| e.name)
            .collect();
        names.sort();
        assert_eq!(names, ["Header.h", "sub"]);
        fs::remove_dir_all(&directory).unwrap();
    }

    #[test]
    fn a_file_is_read_once() {
        let directory = directory("read-once");
        let path = directory.join("a.o");
        fs::write(&path, "first").unwrap();
        let mut files = Files::default();
        let (key, _) = files.stat(&path);
        assert_eq!(&*files.contents(&key).unwrap(), b"first");
        fs::write(&path, "second").unwrap();
        let (again, _) = files.stat(&directory.join("A.O"));
        assert_eq!(&*files.contents(&again).unwrap(), b"first");
        fs::remove_dir_all(&directory).unwrap();
    }

    #[test]
    fn writes_reach_the_disk_when_flushed() {
        let directory = directory("writes");
        fs::write(directory.join("old.o"), "old").unwrap();
        fs::write(directory.join("kept.o"), "kept").unwrap();
        let mut files = Files::default();
        // (a new file in a new directory, a file grown past its end, a file deleted)
        files.create_directory(&directory.join("out")).unwrap();
        let path = directory.join("out/new.o");
        let (_, found) = files.stat(&path);
        assert!(matches!(found, Found::NoFile));
        assert_eq!(disposition(&found, 1), Ok(true));
        let key = files.create(&path, &found);
        assert_eq!(files.write(&key, 2, b"ab").unwrap(), 4);
        let (kept, _) = files.stat(&directory.join("kept.o"));
        files.write(&kept, 4, b"!").unwrap();
        files.delete(&directory.join("old.o")).unwrap();
        // (the run sees its changes; the disk does not have them yet)
        assert_eq!(&*files.contents(&key).unwrap(), b"\0\0ab");
        assert_eq!(files.entry(&key).unwrap().size, 4);
        assert!(matches!(
            files.stat(&directory.join("old.o")).1,
            Found::NoFile
        ));
        assert_eq!(disposition(&files.stat(&path).1, 1), Err(ERROR_FILE_EXISTS));
        let mut names: Vec<String> = files
            .entries(&directory)
            .unwrap()
            .into_iter()
            .map(|e| e.name)
            .collect();
        names.sort();
        assert_eq!(names, ["kept.o", "out"]);
        assert_eq!(files.entries(&directory.join("out")).unwrap().len(), 1);
        assert!(!directory.join("out").exists() && directory.join("old.o").exists());
        assert!(files.flush().is_empty());
        assert_eq!(fs::read(&path).unwrap(), b"\0\0ab");
        assert_eq!(fs::read(directory.join("kept.o")).unwrap(), b"kept!");
        assert!(!directory.join("old.o").exists());
        fs::remove_dir_all(&directory).unwrap();
    }

    #[test]
    fn what_the_run_makes_and_removes_never_reaches_the_disk() {
        let directory = directory("undone");
        fs::create_dir_all(directory.join("gone")).unwrap();
        let mut files = Files::default();
        files.create_directory(&directory.join("tmp")).unwrap();
        let path = directory.join("tmp/a.o");
        let found = files.stat(&path).1;
        let key = files.create(&path, &found);
        files.write(&key, 0, b"x").unwrap();
        assert_eq!(
            files.remove_directory(&directory.join("tmp")),
            Err(ERROR_DIR_NOT_EMPTY)
        );
        files.delete(&path).unwrap();
        files.remove_directory(&directory.join("tmp")).unwrap();
        files.remove_directory(&directory.join("gone")).unwrap();
        assert!(matches!(files.stat(&path).1, Found::NoPath));
        assert_eq!(
            files.delete(&directory.join("none.o")),
            Err(ERROR_FILE_NOT_FOUND)
        );
        assert!(files.flush().is_empty());
        assert!(!directory.join("tmp").exists() && !directory.join("gone").exists());
        fs::remove_dir_all(&directory).unwrap();
    }

    #[test]
    fn dispositions_are_create_files() {
        let file = Found::File(Entry {
            name: "a".into(),
            attributes: FILE_ATTRIBUTE_NORMAL,
            size: 1,
            written: 0,
            short: String::new(),
        });
        assert_eq!(disposition(&file, 1), Err(ERROR_FILE_EXISTS));
        assert_eq!(disposition(&file, 2), Ok(true));
        assert_eq!(disposition(&file, 3), Ok(false));
        assert_eq!(disposition(&file, 4), Ok(false));
        assert_eq!(disposition(&file, 5), Ok(true));
        assert_eq!(disposition(&Found::NoFile, 3), Err(ERROR_FILE_NOT_FOUND));
        assert_eq!(disposition(&Found::NoPath, 3), Err(ERROR_PATH_NOT_FOUND));
        assert_eq!(disposition(&Found::NoPath, 2), Err(ERROR_PATH_NOT_FOUND));
        assert_eq!(disposition(&Found::NoFile, 4), Ok(true));
        assert_eq!(disposition(&Found::NoFile, 5), Err(ERROR_FILE_NOT_FOUND));
    }

    #[test]
    fn names_resolve_case_insensitively() {
        let directory = directory("resolve");
        fs::create_dir_all(directory.join("Inc")).unwrap();
        fs::write(directory.join("Inc/CCrc32.hpp"), "x").unwrap();
        let mut files = Files::default();
        let resolved = files.resolve(&directory.join("inc/CCRC32.hpp"));
        assert_eq!(fs::read_to_string(&resolved).unwrap(), "x");
        if fs::symlink_metadata(directory.join("inc")).is_err() {
            // (a case-sensitive file system: the names as they are there)
            assert_eq!(resolved, directory.join("Inc/CCrc32.hpp"));
            assert_eq!(
                files.resolve(&directory.join("INC/new.o")),
                directory.join("Inc/new.o")
            );
            assert_eq!(
                files.resolve(&directory.join("none/A.h")),
                directory.join("none/A.h")
            );
            // (the name as spelled first, when the directory has it)
            fs::write(directory.join("Inc/ccrc32.HPP"), "y").unwrap();
            let mut files = Files::default();
            assert_eq!(
                files.resolve(&directory.join("Inc/ccrc32.HPP")),
                directory.join("Inc/ccrc32.HPP")
            );
        } else {
            eprintln!("(a case-insensitive file system: nothing to resolve)");
        }
        fs::remove_dir_all(&directory).unwrap();
    }
}
