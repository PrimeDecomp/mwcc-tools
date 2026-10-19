//! The compile cache, in direct mode: a compile's key is its module, arguments (its output path left out),
//! environment, directory, overlay and Shift JIS setting; its entry keeps the object and console, and what each path
//! the compiler consulted outside the overlay was (a file's contents, a directory, absent, or a directory's
//! listing). An entry answers only while every one of those paths is as it was: a header changed, or one created
//! where the compiler looked first, misses. It lives in memory, bounded by bytes; cancelled and timed-out compiles
//! are not kept.
use mwcc_wasm::Consulted;
use sha1::{Digest, Sha1};
use std::{
    collections::HashMap,
    fs,
    path::{Path, PathBuf},
    sync::{
        Arc, Mutex,
        atomic::{AtomicUsize, Ordering},
    },
    time::SystemTime,
};

pub type Key = [u8; 20];

/// A file's hash, and the size and write time it was taken at.
type Stamped = (u64, Option<SystemTime>, Key);

/// A cached compile.
#[derive(Clone, Debug)]
pub struct Hit {
    pub object: Option<Arc<[u8]>>,
    pub console: Arc<[u8]>,
    pub exit_code: i32,
}

/// What a consulted path was.
#[derive(Clone, Debug, PartialEq, Eq)]
enum Seen {
    File {
        size: u64,
        modified: Option<SystemTime>,
        sha1: Key,
    },
    Directory,
    Absent,
    /// A directory's names (as Windows compares them), or none: the directory is not there.
    Listing(Option<Vec<String>>),
}

struct Entry {
    hit: Hit,
    seen: Vec<(Consulted, Seen)>,
    bytes: usize,
    used: u64,
}

#[derive(Default)]
struct Inner {
    entries: HashMap<Key, Entry>,
    bytes: usize,
    clock: u64,
}

pub struct CompileCache {
    limit: usize,
    inner: Mutex<Inner>,
    /// Each file's hash, by its size and write time (rehashed only when they change).
    hashes: Mutex<HashMap<PathBuf, Stamped>>,
    hits: AtomicUsize,
    misses: AtomicUsize,
}

impl Default for CompileCache {
    fn default() -> Self {
        Self::new(256 << 20)
    }
}

/// The key of a compile: its PARTS, each length-prefixed.
pub fn key<'a>(parts: impl IntoIterator<Item = &'a [u8]>) -> Key {
    let mut hash = Sha1::new();
    hash.update(b"mwcc-compile-cache 1\0");
    for part in parts {
        hash.update((part.len() as u64).to_le_bytes());
        hash.update(part);
    }
    hash.finalize().into()
}

fn listing(directory: &Path) -> Option<Vec<String>> {
    let mut names: Vec<String> = fs::read_dir(directory)
        .ok()?
        .flatten()
        .map(|e| e.file_name().to_string_lossy().to_lowercase())
        .collect();
    names.sort();
    Some(names)
}

impl CompileCache {
    /// A cache of at most LIMIT bytes of objects and consoles.
    pub fn new(limit: usize) -> Self {
        Self {
            limit,
            inner: Mutex::default(),
            hashes: Mutex::default(),
            hits: AtomicUsize::new(0),
            misses: AtomicUsize::new(0),
        }
    }

    /// The lookups answered, and those not.
    pub fn hits(&self) -> usize {
        self.hits.load(Ordering::Relaxed)
    }

    pub fn misses(&self) -> usize {
        self.misses.load(Ordering::Relaxed)
    }

    pub fn len(&self) -> usize {
        self.inner.lock().unwrap().entries.len()
    }

    pub fn is_empty(&self) -> bool {
        self.len() == 0
    }

    fn hash(&self, path: &Path, size: u64, modified: Option<SystemTime>) -> Option<Key> {
        if let Some((s, m, hash)) = self.hashes.lock().unwrap().get(path)
            && (*s, *m) == (size, modified)
        {
            return Some(*hash);
        }
        let hash: Key = Sha1::digest(fs::read(path).ok()?).into();
        self.hashes
            .lock()
            .unwrap()
            .insert(path.to_path_buf(), (size, modified, hash));
        Some(hash)
    }

    fn observe(&self, what: &Consulted) -> Seen {
        match what {
            Consulted::Listing(directory) => Seen::Listing(listing(directory)),
            Consulted::Path(path) => match fs::metadata(path) {
                Ok(m) if m.is_dir() => Seen::Directory,
                Ok(m) => {
                    let modified = m.modified().ok();
                    match self.hash(path, m.len(), modified) {
                        Some(sha1) => Seen::File {
                            size: m.len(),
                            modified,
                            sha1,
                        },
                        None => Seen::Absent,
                    }
                }
                Err(_) => Seen::Absent,
            },
        }
    }

    /// Whether WHAT is still as SEEN (a file whose size or write time changed is rehashed).
    fn unchanged(&self, what: &Consulted, seen: &Seen) -> bool {
        if let (Consulted::Path(path), Seen::File { size, modified, .. }) = (what, seen)
            && let Ok(m) = fs::metadata(path)
            && m.is_file()
            && (m.len(), m.modified().ok()) == (*size, *modified)
        {
            return true;
        }
        match (self.observe(what), seen) {
            (Seen::File { sha1: now, .. }, Seen::File { sha1: then, .. }) => now == *then,
            (now, then) => now == *then,
        }
    }

    /// The compile of KEY, if one is kept and what it consulted is unchanged.
    pub fn lookup(&self, key: &Key) -> Option<Hit> {
        let found = {
            let mut inner = self.inner.lock().unwrap();
            inner.clock += 1;
            let clock = inner.clock;
            inner.entries.get_mut(key).map(|entry| {
                entry.used = clock;
                (entry.hit.clone(), entry.seen.clone())
            })
        };
        let hit = found.and_then(|(hit, seen)| {
            seen.iter()
                .all(|(what, seen)| self.unchanged(what, seen))
                .then_some(hit)
        });
        match &hit {
            Some(_) => self.hits.fetch_add(1, Ordering::Relaxed),
            None => self.misses.fetch_add(1, Ordering::Relaxed),
        };
        hit
    }

    /// Keep the compile of KEY: HIT, and what the compiler CONSULTED (any path under IGNORED, its scratch
    /// directory, left out).
    pub fn store(&self, key: Key, hit: Hit, consulted: &[Consulted], ignored: &Path) {
        let seen: Vec<(Consulted, Seen)> = consulted
            .iter()
            .filter(|c| {
                let (Consulted::Path(path) | Consulted::Listing(path)) = c;
                !path.starts_with(ignored)
            })
            .map(|c| (c.clone(), self.observe(c)))
            .collect();
        let bytes =
            hit.object.as_ref().map_or(0, |o| o.len()) + hit.console.len() + 64 * seen.len();
        if bytes > self.limit {
            return;
        }
        let mut inner = self.inner.lock().unwrap();
        inner.clock += 1;
        let used = inner.clock;
        if let Some(old) = inner.entries.insert(
            key,
            Entry {
                hit,
                seen,
                bytes,
                used,
            },
        ) {
            inner.bytes -= old.bytes;
        }
        inner.bytes += bytes;
        // (the least recently used go first)
        while inner.bytes > self.limit {
            let Some(oldest) = inner
                .entries
                .iter()
                .min_by_key(|(_, e)| e.used)
                .map(|(k, _)| *k)
            else {
                break;
            };
            let removed = inner.entries.remove(&oldest).unwrap();
            inner.bytes -= removed.bytes;
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn hit(object: &[u8]) -> Hit {
        Hit {
            object: Some(object.into()),
            console: Arc::from(&b""[..]),
            exit_code: 0,
        }
    }

    #[test]
    fn entries_answer_while_what_they_consulted_is_unchanged() {
        let work = tempfile::tempdir().unwrap();
        let header = work.path().join("inc/a.h");
        let shadow = work.path().join("src/a.h");
        fs::create_dir_all(header.parent().unwrap()).unwrap();
        fs::create_dir_all(shadow.parent().unwrap()).unwrap();
        fs::write(&header, "#define A 1\n").unwrap();
        let consulted = [
            Consulted::Path(shadow.clone()),
            Consulted::Path(header.clone()),
            Consulted::Listing(work.path().join("inc")),
            Consulted::Path(work.path().join("scratch/out.o")),
        ];
        let cache = CompileCache::default();
        let k = key([b"argv".as_slice()]);
        assert!(cache.lookup(&k).is_none());
        cache.store(k, hit(b"object"), &consulted, &work.path().join("scratch"));
        assert_eq!(
            cache.lookup(&k).unwrap().object.as_deref(),
            Some(&b"object"[..])
        );
        // (written again with the same text: rehashed, still a hit)
        std::thread::sleep(std::time::Duration::from_millis(20));
        fs::write(&header, "#define A 1\n").unwrap();
        assert!(cache.lookup(&k).is_some());
        // (another text: a miss)
        fs::write(&header, "#define A 2\n").unwrap();
        assert!(cache.lookup(&k).is_none());
        cache.store(
            k,
            hit(b"object 2"),
            &consulted,
            &work.path().join("scratch"),
        );
        assert!(cache.lookup(&k).is_some());
        // (a header created where the compiler looked first: a miss)
        fs::write(&shadow, "#define A 3\n").unwrap();
        assert!(cache.lookup(&k).is_none());
        fs::remove_file(&shadow).unwrap();
        assert!(cache.lookup(&k).is_some());
        // (a file added to a listed directory: a miss)
        fs::write(work.path().join("inc/b.h"), "").unwrap();
        assert!(cache.lookup(&k).is_none());
        assert_eq!((cache.hits(), cache.misses()), (4, 4));
    }

    #[test]
    fn the_least_recently_used_go_first() {
        let work = tempfile::tempdir().unwrap();
        let cache = CompileCache::new(300);
        let keys: Vec<Key> = (0..3u8).map(|i| key([[i].as_slice()])).collect();
        cache.store(keys[0], hit(&[0; 100]), &[], work.path());
        cache.store(keys[1], hit(&[1; 100]), &[], work.path());
        assert!(cache.lookup(&keys[0]).is_some());
        cache.store(keys[2], hit(&[2; 150]), &[], work.path());
        assert!(cache.lookup(&keys[0]).is_some());
        assert!(cache.lookup(&keys[1]).is_none());
        assert!(cache.lookup(&keys[2]).is_some());
    }
}
