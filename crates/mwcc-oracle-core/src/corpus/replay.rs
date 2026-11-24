use super::{Job, is_path_option};
use crate::{fsutil, hash::sha256_hex, ninja::CompileRule, runner, version::Version};
use anyhow::{Context, Result, ensure};
use flate2::{Compression, read::GzDecoder, write::GzEncoder};
use serde::{Deserialize, Serialize};
use sha2::{Digest, Sha256};
use std::{
    collections::{BTreeMap, BTreeSet},
    fs,
    path::{Path, PathBuf},
    process::Command,
    time::{Duration, UNIX_EPOCH},
};

const FORMAT: u32 = 1;

#[derive(Debug, Deserialize, Serialize)]
pub struct Project {
    graph: PathBuf,
    graph_sha256: String,
    revision: Option<String>,
    compiler_labels: BTreeSet<String>,
    edges: serde_json::Value,
}

impl Project {
    pub(super) fn new(
        graph: PathBuf,
        contents: &[u8],
        compiler_labels: BTreeSet<String>,
        edges: &[CompileRule],
    ) -> Result<Self> {
        let revision = graph.parent().and_then(|parent| {
            let git = parent
                .ancestors()
                .map(|path| path.join(".git"))
                .find(|p| p.exists())?;
            let git = if git.is_file() {
                let pointer = fs::read_to_string(&git).ok()?;
                git.parent()?.join(pointer.trim().strip_prefix("gitdir: ")?)
            } else {
                git
            };
            let output = Command::new("git")
                .arg("--git-dir")
                .arg(git)
                .args(["rev-parse", "--verify", "HEAD"])
                .output()
                .ok()?;
            output
                .status
                .success()
                .then(|| String::from_utf8_lossy(&output.stdout).trim().to_owned())
        });
        Ok(Self {
            graph,
            graph_sha256: sha256_hex(contents),
            revision,
            compiler_labels,
            edges: serde_json::to_value(edges)?,
        })
    }
}

#[derive(Debug, Deserialize, Serialize)]
pub struct Manifest {
    format: u32,
    pub version: Version,
    stock_sha256: BTreeMap<String, String>,
    pub jobs: Vec<Job>,
    projects: Vec<Project>,
    inputs: BTreeMap<PathBuf, String>,
}

impl Manifest {
    pub fn capture(
        root: &Path,
        version: Version,
        jobs: Vec<Job>,
        projects: Vec<Project>,
        extra_inputs: Vec<PathBuf>,
    ) -> Result<Self> {
        ensure!(!jobs.is_empty(), "no corpus inputs");
        let mut paths = BTreeSet::new();
        for path in extra_inputs {
            let path = std::path::absolute(path)?;
            ensure!(
                path.exists(),
                "declared input does not exist: {}",
                path.display()
            );
            paths.insert(path);
        }
        for job in &jobs {
            let resolve = |value: &str| {
                job.directory
                    .join(value.replace(['/', '\\'], std::path::MAIN_SEPARATOR_STR))
            };
            let source = resolve(&job.source);
            paths.insert(source.clone());
            if let Some(directory) = source.parent() {
                paths.insert(directory.to_owned());
            }
            let mut flags = job.flags.iter().map(String::as_str);
            while let Some(flag) = flags.next() {
                let path = if is_path_option(flag) {
                    flags.next()
                } else {
                    flag.strip_prefix("-I").filter(|v| !matches!(*v, "" | "-"))
                };
                if let Some(path) = path {
                    paths.insert(resolve(path));
                }
            }
        }
        let mut paths = paths
            .iter()
            .map(std::path::absolute)
            .collect::<std::io::Result<BTreeSet<_>>>()?;
        // An ancestor already covers every descendant, including paths currently missing.
        let all = paths.clone();
        paths.retain(|path| !path.ancestors().skip(1).any(|parent| all.contains(parent)));
        let inputs = paths
            .into_iter()
            .map(|path| Ok((path.clone(), input_digest(&path)?)))
            .collect::<Result<_>>()?;
        let manifest = Self {
            format: FORMAT,
            version,
            stock_sha256: tool_hashes(&stock(root, version))?,
            jobs,
            projects,
            inputs,
        };
        manifest.verify_graphs()?;
        Ok(manifest)
    }

    pub fn write(&self, path: &Path) -> Result<()> {
        fsutil::write_atomic(path, &serde_json::to_vec_pretty(self)?)
    }

    pub fn read(path: &Path) -> Result<Self> {
        let manifest: Self = serde_json::from_slice(&fs::read(path)?)
            .with_context(|| format!("read corpus manifest {}", path.display()))?;
        ensure!(
            manifest.format == FORMAT,
            "unsupported corpus manifest format"
        );
        Ok(manifest)
    }

    fn verify_graphs(&self) -> Result<()> {
        for project in &self.projects {
            ensure!(
                file_digest(&project.graph)? == project.graph_sha256,
                "corpus graph changed: {}; recapture the manifest",
                project.graph.display()
            );
        }
        Ok(())
    }

    pub(super) fn verify(&self, root: &Path) -> Result<()> {
        self.verify_graphs()?;
        ensure!(
            tool_hashes(&stock(root, self.version))? == self.stock_sha256,
            "stock compiler changed; recapture the corpus manifest"
        );
        for (path, expected) in &self.inputs {
            ensure!(
                input_digest(path)? == *expected,
                "corpus input changed: {}; recapture the manifest",
                path.display()
            );
        }
        Ok(())
    }
}

fn stock(root: &Path, version: Version) -> PathBuf {
    mwcc_project::compilers::stock_executable(root, version)
}

fn file_digest(path: &Path) -> Result<String> {
    Ok(sha256_hex(
        fs::read(path).with_context(|| format!("read {}", path.display()))?,
    ))
}

fn input_digest(path: &Path) -> Result<String> {
    fn part(hash: &mut Sha256, bytes: &[u8]) {
        hash.update((bytes.len() as u64).to_le_bytes());
        hash.update(bytes);
    }
    fn visit(path: &Path, hash: &mut Sha256, parents: &mut Vec<PathBuf>) -> Result<()> {
        match fs::metadata(path) {
            Ok(meta) if meta.is_dir() => {
                hash.update(b"d");
                let real = fs::canonicalize(path)?;
                ensure!(
                    !parents.contains(&real),
                    "corpus input directory cycle: {}",
                    path.display()
                );
                parents.push(real);
                let mut entries = fs::read_dir(path)?.collect::<std::io::Result<Vec<_>>>()?;
                entries.sort_by_key(|entry| entry.file_name());
                hash.update((entries.len() as u64).to_le_bytes());
                for entry in entries {
                    part(hash, entry.file_name().as_encoded_bytes());
                    visit(&entry.path(), hash, parents)?;
                }
                parents.pop();
            }
            Ok(meta) if meta.is_file() => {
                hash.update(b"f");
                // Stock supports __TIMESTAMP__ in addition to content-dependent reads.
                part(
                    hash,
                    format!("{:?}", meta.modified()?.duration_since(UNIX_EPOCH)).as_bytes(),
                );
                part(hash, &fs::read(path)?);
            }
            Err(error) if error.kind() == std::io::ErrorKind::NotFound => hash.update(b"-"),
            Err(error) => return Err(error.into()),
            Ok(_) => anyhow::bail!("unsupported corpus input: {}", path.display()),
        }
        Ok(())
    }
    let mut hash = Sha256::new();
    visit(path, &mut hash, &mut vec![]).with_context(|| format!("snapshot {}", path.display()))?;
    Ok(format!("{:x}", hash.finalize()))
}

/// An immutable copy of the compiler and its adjacent runtime files for this run.
pub struct Staged {
    _directory: tempfile::TempDir,
    pub path: PathBuf,
    pub hashes: BTreeMap<String, String>,
}

pub fn stage(path: &Path) -> Result<Staged> {
    let path = std::path::absolute(path)?;
    let dir = tempfile::Builder::new()
        .prefix("mwcc-corpus-tool-")
        .tempdir()?;
    let name = path.file_name().context("compiler path has no name")?;
    let mut hashes = BTreeMap::new();
    for source in components(&path)? {
        let name = source
            .file_name()
            .context("compiler component has no name")?;
        let data = fs::read(&source).with_context(|| format!("stage {}", source.display()))?;
        fs::write(dir.path().join(name), &data)?;
        hashes.insert(name.to_string_lossy().into_owned(), sha256_hex(data));
    }
    Ok(Staged {
        path: dir.path().join(name),
        _directory: dir,
        hashes,
    })
}

fn components(path: &Path) -> Result<Vec<PathBuf>> {
    let parent = path.parent().context("compiler path has no parent")?;
    let mut paths = vec![path.to_owned()];
    if path
        .extension()
        .is_some_and(|ext| ext.eq_ignore_ascii_case("wasm"))
    {
        paths.push(path.with_extension("version"));
    } else {
        for entry in fs::read_dir(parent)? {
            let entry = entry?.path();
            if entry
                .extension()
                .is_some_and(|ext| ext.eq_ignore_ascii_case("dll"))
            {
                paths.push(entry);
            }
        }
    }
    Ok(paths)
}

fn tool_hashes(path: &Path) -> Result<BTreeMap<String, String>> {
    components(path)?
        .iter()
        .map(|file| {
            let name = file.file_name().context("compiler component has no name")?;
            Ok((name.to_string_lossy().into_owned(), file_digest(file)?))
        })
        .collect()
}

#[derive(Debug, Deserialize, Serialize)]
pub(super) struct Captured {
    pub execution: runner::Execution,
    pub files: BTreeMap<PathBuf, Vec<u8>>,
}

impl Captured {
    pub fn collect(output: &Path, execution: runner::Execution) -> Result<Self> {
        let dir = output.parent().context("output has no directory")?;
        let files = fsutil::files(dir, true)?
            .into_iter()
            .map(|path| Ok((path.strip_prefix(dir)?.to_owned(), fs::read(path)?)))
            .collect::<Result<_>>()?;
        Ok(Self { execution, files })
    }
}

pub(super) struct Cache {
    directory: PathBuf,
    pending: tempfile::TempDir,
    _lock: fs::File,
}

impl Cache {
    pub fn open(
        directory: &Path,
        manifest: &Manifest,
        original: &Path,
        timeout: Duration,
    ) -> Result<Self> {
        let original = std::path::absolute(original)?;
        let environment: BTreeMap<_, _> = std::env::vars_os().collect();
        let mut hash = Sha256::new();
        hash.update(serde_json::to_vec(&(
            manifest,
            original,
            timeout,
            std::env::consts::OS,
        ))?);
        for (key, value) in environment {
            if runner::TRACE_ENV.iter().any(|name| key == *name) {
                continue;
            }
            for bytes in [key.as_encoded_bytes(), value.as_encoded_bytes()] {
                hash.update((bytes.len() as u64).to_le_bytes());
                hash.update(bytes);
            }
        }
        let directory = std::path::absolute(directory)?.join(format!("{:x}", hash.finalize()));
        fs::create_dir_all(&directory)?;
        let lock = fs::OpenOptions::new()
            .read(true)
            .write(true)
            .create(true)
            .truncate(false)
            .open(directory.join("lock"))?;
        lock.lock()?;
        let pending = tempfile::tempdir_in(&directory)?;
        Ok(Self {
            directory,
            pending,
            _lock: lock,
        })
    }

    pub fn work(&self) -> PathBuf {
        self.directory.join("work")
    }

    pub fn key(job: &Job) -> Result<String> {
        Ok(sha256_hex(serde_json::to_vec(job)?))
    }

    pub fn load(&self, key: &str) -> Result<Option<Captured>> {
        let path = self.directory.join(format!("{key}.json.gz"));
        let file = match fs::File::open(&path) {
            Ok(file) => file,
            Err(error) if error.kind() == std::io::ErrorKind::NotFound => return Ok(None),
            Err(error) => return Err(error.into()),
        };
        Ok(Some(
            serde_json::from_reader(GzDecoder::new(file))
                .with_context(|| format!("read cached stock result {}", path.display()))?,
        ))
    }

    pub fn store(&self, key: &str, result: &Captured) -> Result<()> {
        if !result.execution.timed_out
            && !result.execution.cancelled
            && result.execution.exit_code.is_some()
        {
            let mut writer = GzEncoder::new(Vec::new(), Compression::fast());
            serde_json::to_writer(&mut writer, result)?;
            // The pending directory is private until publication. A failed write ends
            // the run before any pending results are published.
            fs::write(
                self.pending.path().join(format!("{key}.json.gz")),
                writer.finish()?,
            )?;
        }
        Ok(())
    }

    pub fn publish(&self) -> Result<()> {
        for entry in fs::read_dir(self.pending.path())? {
            let entry = entry?;
            let target = self.directory.join(entry.file_name());
            if !target.exists() {
                fs::rename(entry.path(), target)?;
            }
        }
        Ok(())
    }
}

impl Drop for Cache {
    fn drop(&mut self) {
        let _ = fs::remove_dir_all(self.work());
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn replay_checks_header_tree_and_publishes_only_finished_stock_results() -> Result<()> {
        let temp = tempfile::tempdir()?;
        let root = temp.path();
        let original = stock(root, Version::Wii10Rc1);
        fs::create_dir_all(original.parent().unwrap())?;
        fs::write(&original, b"compiler")?;
        let source = root.join("src");
        fs::create_dir(&source)?;
        fs::write(source.join("test.c"), b"#include \"test.h\"")?;
        fs::write(source.join("test.h"), b"first revision")?;
        let job = Job {
            name: "test.c".into(),
            source: "test.c".into(),
            flags: vec![],
            directory: source.clone(),
        };
        let manifest =
            Manifest::capture(root, Version::Wii10Rc1, vec![job.clone()], vec![], vec![])?;
        manifest.verify(root)?;
        let cache_dir = root.join("cache");
        let open = || Cache::open(&cache_dir, &manifest, &original, Duration::from_secs(1));
        let key = Cache::key(&job)?;
        let result = Captured {
            files: BTreeMap::from([(PathBuf::from("result.o"), vec![1, 2, 3])]),
            execution: runner::Execution {
                exit_code: Some(0),
                timed_out: false,
                cancelled: false,
                diagnostics: String::new(),
                trace: vec![],
            },
        };
        let cache = open()?;
        cache.store(&key, &result)?;
        assert!(cache.load(&key)?.is_none());
        cache.publish()?;
        drop(cache);
        assert_eq!(open()?.load(&key)?.unwrap().files, result.files);
        // Adding a file in the search tree may shadow a header that was read before.
        fs::write(source.join("shadow.h"), b"new")?;
        assert!(
            manifest
                .verify(root)
                .unwrap_err()
                .to_string()
                .contains("input changed")
        );
        Ok(())
    }
}
