//! The released WebAssembly compilers and linkers, downloaded where no checkout has them built.
//!
//! Without `MWCC_ORACLE_ROOT` or a checkout around this executable, `mwcc-tools run` (and `run ninja`, and the
//! methods, through [`fetch`]) use a cache laid out as a checkout: `MWCC_WASM_CACHE`, else the user's cache directory
//! (`%LOCALAPPDATA%`, `$XDG_CACHE_HOME` or `~/.cache`, `~/Library/Caches`) under `mwcc-tools/v<version>`. Its `config/<version>/config.json` are this
//! executable's own copies. A compiler or linker that is a copy of a released version's original (by size and SHA-1)
//! and whose module the cache does not have gets its version's archive (`mwcc-wasm-<version>-<release>.zip`, which
//! the release workflow publishes) from this executable's release, checked against its `.sha256`. The archive is
//! unpacked beside the cache, each module compiled for this host, and moved in, under a lock that other processes
//! (a build's parallel compiles) wait on. `MWCC_WASM_DOWNLOAD=0` turns downloading off; `MWCC_WASM_RELEASE` (a
//! version, such as 0.3.0) and `MWCC_WASM_RELEASE_URL` (the directory of its assets) choose another release.
//! `mwcc-tools download wasm` downloads them ahead of time, into the cache or any root.
use anyhow::{Context, Result, bail, ensure};
use mwcc_oracle_core::download::{Fetch, HttpFetcher, checked_relative};
use sha1::Sha1;
use sha2::{Digest, Sha256};
use std::{
    collections::HashSet,
    env,
    fs::{self, File},
    io::{Cursor, Read},
    path::{Path, PathBuf},
    sync::Mutex,
    time::Duration,
};

/// A version whose module a release archive holds, with its `config.json`.
struct Member {
    version: &'static str,
    /// The module's path in a checkout.
    module: &'static str,
    config: &'static str,
}

/// A release archive (`mwcc-wasm-NAME-<release>.zip`): the build workflow's `wasm` job.
struct Archive {
    name: &'static str,
    members: &'static [Member],
}

const ARCHIVES: [Archive; 4] = [
    Archive {
        name: "GC_2_7",
        members: &[
            Member {
                version: "GC_2_7",
                module: "build/GC_2_7/wasm/mwcc.wasm",
                config: include_str!("../../../config/GC_2_7/config.json"),
            },
            Member {
                version: "LD_2_7",
                module: "build/LD_2_7/wasm/mwldeppc.wasm",
                config: include_str!("../../../config/LD_2_7/config.json"),
            },
        ],
    },
    Archive {
        name: "GC_2_6",
        members: &[Member {
            version: "GC_2_6",
            module: "build/GC_2_6/wasm/mwcc.wasm",
            config: include_str!("../../../config/GC_2_6/config.json"),
        }],
    },
    Archive {
        name: "GC_1_3_2",
        members: &[
            Member {
                version: "GC_1_3_2",
                module: "build/GC_1_3_2/wasm/mwcc.wasm",
                config: include_str!("../../../config/GC_1_3_2/config.json"),
            },
            Member {
                version: "LD_1_3_2",
                module: "build/LD_1_3_2/wasm/mwldeppc.wasm",
                config: include_str!("../../../config/LD_1_3_2/config.json"),
            },
        ],
    },
    Archive {
        name: "GC_1_2_5n",
        members: &[Member {
            version: "GC_1_2_5n",
            module: "build/GC_1_2_5n/wasm/mwcc.wasm",
            config: include_str!("../../../config/GC_1_2_5n/config.json"),
        }],
    },
];

const REPOSITORY: &str = "PrimeDecomp/mwcc-tools";

/// The cache's root: `MWCC_WASM_CACHE`, else the user's cache directory's `mwcc-tools/v<version>`.
pub fn cache_root() -> Result<PathBuf> {
    if let Some(dir) = env::var_os("MWCC_WASM_CACHE").filter(|v| !v.is_empty()) {
        return Ok(dir.into());
    }
    let home = || {
        env::var_os("HOME")
            .filter(|v| !v.is_empty())
            .map(PathBuf::from)
    };
    let base = if cfg!(windows) {
        env::var_os("LOCALAPPDATA")
            .filter(|v| !v.is_empty())
            .map(PathBuf::from)
    } else if cfg!(target_os = "macos") {
        home().map(|home| home.join("Library/Caches"))
    } else {
        env::var_os("XDG_CACHE_HOME")
            .filter(|v| !v.is_empty())
            .map(PathBuf::from)
            .or_else(|| home().map(|home| home.join(".cache")))
    };
    Ok(base
        .context("no cache directory for the WebAssembly modules: set MWCC_WASM_CACHE or MWCC_ORACLE_ROOT")?
        .join("mwcc-tools")
        .join(format!("v{}", env!("CARGO_PKG_VERSION"))))
}

/// ROOT (the cache) with this executable's `config/<version>/config.json`, which identify the originals.
pub fn prepare(root: &Path) -> Result<()> {
    for member in ARCHIVES.iter().flat_map(|a| a.members) {
        let path = root.join(format!("config/{}/config.json", member.version));
        if fs::read(&path).is_ok_and(|old| old == member.config.as_bytes()) {
            continue;
        }
        fs::create_dir_all(path.parent().unwrap())?;
        let staged = path.with_extension(format!("json.{}", std::process::id()));
        fs::write(&staged, member.config)?;
        fs::rename(&staged, &path).with_context(|| format!("write {}", path.display()))?;
    }
    Ok(())
}

/// Whether modules are downloaded (`MWCC_WASM_DOWNLOAD`, on unless `0`, and only where modules run in process).
fn enabled() -> bool {
    env::var("MWCC_WASM_DOWNLOAD").map_or(true, |v| v != "0") && mwcc_project::compilers::enabled()
}

/// The release the modules come from: `MWCC_WASM_RELEASE`, else this executable's version.
fn release() -> String {
    env::var("MWCC_WASM_RELEASE")
        .ok()
        .filter(|v| !v.is_empty())
        .map(|v| v.trim_start_matches('v').to_owned())
        .unwrap_or_else(|| env!("CARGO_PKG_VERSION").to_owned())
}

/// The directory of RELEASE's assets: `MWCC_WASM_RELEASE_URL`, else its GitHub release.
fn release_url(release: &str) -> String {
    env::var("MWCC_WASM_RELEASE_URL")
        .ok()
        .filter(|v| !v.is_empty())
        .unwrap_or_else(|| format!("https://github.com/{REPOSITORY}/releases/download/v{release}"))
        .trim_end_matches('/')
        .to_owned()
}

fn fetcher() -> Result<HttpFetcher> {
    HttpFetcher::new(Duration::from_secs(300))
}

/// A `config.json`'s size and SHA-1 of its original.
fn identity(config: &str) -> Option<(u64, String)> {
    let config: serde_json::Value = serde_json::from_str(config).ok()?;
    Some((
        config["size"].as_u64()?,
        config["sha1"].as_str()?.to_ascii_lowercase(),
    ))
}

/// The download cache, prepared the first time.
pub fn cache() -> Result<&'static Path> {
    static CACHE: std::sync::OnceLock<Result<PathBuf, String>> = std::sync::OnceLock::new();
    let cache = CACHE.get_or_init(|| {
        (|| {
            let root = cache_root()?;
            prepare(&root)?;
            Ok::<_, anyhow::Error>(root)
        })()
        .map_err(|e| format!("{e:#}"))
    });
    match cache {
        Ok(root) => Ok(root),
        Err(e) => Err(anyhow::anyhow!("{e}")),
    }
}

/// EXECUTABLE's module (a compiler's or a linker's) and the root it is in: CHECKOUT's when it has it, else the
/// download cache's, downloaded when EXECUTABLE is a copy of a released version's original. None: EXECUTABLE runs as
/// a process.
pub fn module_for(
    checkout: Option<&Path>,
    executable: &Path,
) -> Result<Option<(PathBuf, mwcc_oracle_core::version::Version, PathBuf)>> {
    use mwcc_oracle_core::in_process::{linker_module_of_copy, module_of_copy};
    let lookup = |root: &Path| {
        module_of_copy(root, executable)
            .or_else(|| linker_module_of_copy(root, executable))
            .map(|(version, module)| (root.to_path_buf(), version, module))
    };
    if let Some(found) = checkout.and_then(lookup) {
        return Ok(Some(found));
    }
    // (a checkout without the module, such as one with only GC 2.7 built: the released one)
    if checkout.is_some() && !enabled() {
        return Ok(None);
    }
    let cache = match cache() {
        Ok(cache) => cache,
        // (no cache directory: the checkout's modules only)
        Err(_) if checkout.is_some() => return Ok(None),
        Err(e) => return Err(e),
    };
    ensure_for(cache, executable)?;
    Ok(lookup(cache))
}

/// The executables this process has looked at already (a build runs each many times). Its lock is held while one is
/// looked at, so the build's other runs of it wait for its download instead of running the executable.
static LOOKED_AT: Mutex<Option<HashSet<PathBuf>>> = Mutex::new(None);

/// EXECUTABLE's module in ROOT (the cache), downloaded when EXECUTABLE is a copy of a released version's original
/// and ROOT does not have it. Anything else is left as it is: its run finds no module and runs the executable.
pub fn ensure_for(root: &Path, executable: &Path) -> Result<()> {
    if !enabled() {
        return Ok(());
    }
    let key = std::path::absolute(executable).unwrap_or_else(|_| executable.to_path_buf());
    let mut looked_at = LOOKED_AT.lock().unwrap_or_else(|e| e.into_inner());
    let looked_at = looked_at.get_or_insert_with(HashSet::new);
    if looked_at.contains(&key) {
        return Ok(());
    }
    if let Some(archive) = archive_of(root, executable)? {
        let fallback = format!("{} runs as a process", executable.display());
        fetch_archive(root, archive, &fallback)?;
    }
    looked_at.insert(key);
    Ok(())
}

/// ARCHIVE installed in ROOT from the release, unless the release is known to lack it. A fetch that fails is a
/// warning that ends with FALLBACK (what happens instead).
fn fetch_archive(root: &Path, archive: &Archive, fallback: &str) -> Result<()> {
    let release = release();
    let url = archive_url(archive, &release);
    let unavailable = unavailable_path(root, archive);
    // (a release without the archive, known from an earlier look: not asked again)
    if fs::read_to_string(&unavailable).is_ok_and(|known| known == url) {
        return Ok(());
    }
    match install(root, archive, &mut fetcher()?, &release) {
        Ok(()) => Ok(()),
        Err(e) if e.downcast_ref::<Unfetched>().is_some() => {
            let missing = e.downcast_ref::<Unfetched>().unwrap().missing;
            eprintln!("mwcc-tools: {e:#}; {fallback} (MWCC_WASM_DOWNLOAD=0 does not download)");
            if missing {
                let _ = fs::write(&unavailable, &url);
            }
            Ok(())
        }
        Err(e) => Err(e),
    }
}

/// The versions whose archive this process has looked for already.
static FETCHED: Mutex<Option<HashSet<&'static str>>> = Mutex::new(None);

/// VERSION's module (a compiler's or a linker's: `GC_2_7`, `LD_2_7`...) downloaded into ROOT when ROOT is the
/// download cache and lacks it: what the oracle's methods look for (`mwcc_project::compilers::set_fetch`). A checkout
/// gets nothing: its modules are the ones it builds or unpacks.
pub fn fetch(root: &Path, version: &str) {
    // (ROOT may be the cache's canonical path: the oracle's runner canonicalizes it)
    let canonical = |path: &Path| fs::canonicalize(path).ok();
    if !enabled()
        || cache()
            .ok()
            .and_then(canonical)
            .is_none_or(|c| Some(c) != canonical(root))
    {
        return;
    }
    let Some(archive) = ARCHIVES
        .iter()
        .find(|a| a.members.iter().any(|m| m.version == version))
    else {
        return;
    };
    let mut fetched = FETCHED.lock().unwrap_or_else(|e| e.into_inner());
    if !fetched
        .get_or_insert_with(HashSet::new)
        .insert(archive.name)
        || complete(root, archive)
    {
        return;
    }
    let fallback = format!("{version} has no module");
    if let Err(e) = fetch_archive(root, archive, &fallback) {
        eprintln!("mwcc-tools: {e:#}; {fallback}");
    }
}

/// A download that failed: the build runs the executable instead. MISSING: the server has no such file (an HTTP
/// client error), so the release has no archive of the version.
#[derive(Debug)]
struct Unfetched {
    url: String,
    missing: bool,
    /// The fetch's error.
    detail: String,
}

impl std::fmt::Display for Unfetched {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        if self.missing {
            write!(f, "the release has no {}", self.url)
        } else {
            write!(f, "could not download {}: {}", self.url, self.detail)
        }
    }
}

impl std::error::Error for Unfetched {}

/// Whether ERROR is a server's client error (such as 404: no such file).
fn client_error(error: &anyhow::Error) -> bool {
    error.chain().any(|cause| {
        matches!(cause.downcast_ref::<ureq::Error>(), Some(ureq::Error::StatusCode(code)) if *code < 500)
    })
}

/// ARCHIVE's file in RELEASE.
fn archive_url(archive: &Archive, release: &str) -> String {
    format!(
        "{}/mwcc-wasm-{}-{release}.zip",
        release_url(release),
        archive.name
    )
}

/// Where ROOT keeps the URL of ARCHIVE's file when its release has none.
fn unavailable_path(root: &Path, archive: &Archive) -> PathBuf {
    root.join(format!(".{}.unavailable", archive.name))
}

/// The archive EXECUTABLE's module is in, when EXECUTABLE is a copy of its version's original and ROOT lacks it.
fn archive_of(root: &Path, executable: &Path) -> Result<Option<&'static Archive>> {
    let Ok(metadata) = fs::metadata(executable) else {
        return Ok(None);
    };
    let mut digest = None;
    for archive in &ARCHIVES {
        for member in archive.members {
            let Some((size, sha1)) = identity(member.config) else {
                continue;
            };
            if size != metadata.len() {
                continue;
            }
            let digest = match &digest {
                Some(digest) => digest,
                None => digest.insert(format!("{:x}", Sha1::digest(fs::read(executable)?))),
            };
            if *digest != sha1 {
                continue;
            }
            return Ok((!root.join(member.module).is_file()).then_some(archive));
        }
    }
    Ok(None)
}

fn complete(root: &Path, archive: &Archive) -> bool {
    archive.members.iter().all(|member| {
        let module = root.join(member.module);
        module.is_file() && mwcc_wasm::version_info_path(&module).is_file()
    })
}

/// ARCHIVE of RELEASE, fetched by FETCH, installed in ROOT (unless it is there, or another process installs it while
/// this one waits for the lock).
fn install(root: &Path, archive: &Archive, fetch: &mut dyn Fetch, release: &str) -> Result<()> {
    if complete(root, archive) {
        return Ok(());
    }
    fs::create_dir_all(root)?;
    let lock = File::create(root.join(format!(".{}.lock", archive.name)))?;
    lock.lock()?;
    if complete(root, archive) {
        return Ok(());
    }
    let file = format!("mwcc-wasm-{}-{release}.zip", archive.name);
    let url = archive_url(archive, release);
    eprintln!("mwcc-tools: downloading {url}");
    let mut fetched = |url: String| {
        fetch.fetch(&url, None).map_err(|error| {
            let missing = client_error(&error);
            let detail = format!("{error:#}");
            anyhow::Error::new(Unfetched {
                url,
                missing,
                detail,
            })
        })
    };
    let bytes = fetched(url.clone())?;
    let sidecar = fetched(format!("{url}.sha256"))?;
    let expected = String::from_utf8_lossy(&sidecar)
        .split_whitespace()
        .next()
        .map(str::to_ascii_lowercase)
        .context("empty checksum file")?;
    let actual = format!("{:x}", Sha256::digest(&bytes));
    ensure!(
        actual == expected,
        "{file}: SHA-256 {actual}, but its .sha256 gives {expected}"
    );

    let staging = tempfile::Builder::new()
        .prefix(".download-")
        .tempdir_in(root)?;
    let mut zip =
        zip::ZipArchive::new(Cursor::new(bytes)).with_context(|| format!("read {file}"))?;
    for i in 0..zip.len() {
        let mut entry = zip.by_index(i)?;
        if entry.is_dir() {
            continue;
        }
        let name = entry.name().to_owned();
        let relative = checked_relative(&name)?;
        let allowed = relative == Path::new("LICENSE")
            || archive
                .members
                .iter()
                .any(|member| relative.starts_with(Path::new(member.module).parent().unwrap()));
        ensure!(
            allowed,
            "{file} has {name}, outside its modules' directories"
        );
        let target = staging.path().join(&relative);
        fs::create_dir_all(target.parent().unwrap())?;
        let mut data = vec![];
        entry
            .by_ref()
            .take(mwcc_oracle_core::download::MAX_BYTES)
            .read_to_end(&mut data)?;
        fs::write(&target, data)?;
    }
    for member in archive.members {
        let module = staging.path().join(member.module);
        ensure!(
            module.is_file() && mwcc_wasm::version_info_path(&module).is_file(),
            "{file} lacks {} or its .version",
            member.module
        );
        // (compiled for this host now, so the build's processes load it instead of each compiling it)
        mwcc_wasm::precompile(&module)
            .map_err(|e| anyhow::anyhow!("{e:?}"))
            .with_context(|| {
            format!(
                "compile {} of release {release}: a module of another release than this executable's \
                 ({}) may not run in it; MWCC_WASM_RELEASE and MWCC_WASM_RELEASE_URL choose the release",
                member.module,
                env!("CARGO_PKG_VERSION")
            )
        })?;
    }
    for member in archive.members {
        let directory = Path::new(member.module).parent().unwrap();
        let target = root.join(directory);
        if target.exists() {
            fs::remove_dir_all(&target).with_context(|| format!("replace {}", target.display()))?;
        }
        fs::create_dir_all(target.parent().unwrap())?;
        fs::rename(staging.path().join(directory), &target)
            .with_context(|| format!("move {} into place", target.display()))?;
    }
    if staging.path().join("LICENSE").is_file() {
        fs::copy(staging.path().join("LICENSE"), root.join("LICENSE"))?;
    }
    let _ = fs::remove_file(unavailable_path(root, archive));
    eprintln!(
        "mwcc-tools: {} installed in {}",
        archive.name,
        root.display()
    );
    Ok(())
}

const USAGE: &str = "download wasm [VERSION...] [--root DIR]
    downloads the released WebAssembly compilers and linkers (by default all: GC_2_7, GC_2_6, GC_1_3_2,
    GC_1_2_5n; each with its linker, where there is one) from this executable's release into DIR, by default the cache `mwcc-tools \
run` uses without MWCC_ORACLE_ROOT. MWCC_WASM_RELEASE (a version) and MWCC_WASM_RELEASE_URL choose another release";

/// `mwcc-tools download wasm ARGS`.
pub fn command(args: &[String]) -> Result<()> {
    let mut root = None;
    let mut names = vec![];
    let mut i = 0;
    while i < args.len() {
        match args[i].as_str() {
            "--help" | "-h" => {
                println!("{USAGE}");
                return Ok(());
            }
            "--root" => root = Some(PathBuf::from(crate::option_value(args, &mut i)?)),
            value if !value.starts_with('-') => names.push(value.to_owned()),
            value => bail!("unknown download wasm argument {value}; see download wasm --help"),
        }
        i += 1;
    }
    let root = match root {
        Some(root) => root,
        None => {
            let root = cache_root()?;
            prepare(&root)?;
            root
        }
    };
    let chosen: Vec<&Archive> = if names.is_empty() {
        ARCHIVES.iter().collect()
    } else {
        names
            .iter()
            .map(|name| {
                ARCHIVES
                    .iter()
                    .find(|a| a.members.iter().any(|m| m.version == name))
                    .with_context(|| {
                        format!("{name} has no released module (GC_2_7, LD_2_7, GC_2_6, GC_1_3_2, LD_1_3_2, GC_1_2_5n)")
                    })
            })
            .collect::<Result<_>>()?
    };
    let release = release();
    let mut fetch = fetcher()?;
    for archive in chosen {
        if complete(&root, archive) {
            println!("{}: already in {}", archive.name, root.display());
            continue;
        }
        install(&root, archive, &mut fetch, &release)?;
    }
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::io::Write;

    /// Serves an archive and its checksum file, counting the fetches.
    struct Served {
        zip: Vec<u8>,
        sha256: String,
        fetches: usize,
    }

    impl Fetch for Served {
        fn fetch(&mut self, url: &str, _: Option<(u64, u64)>) -> Result<Vec<u8>> {
            self.fetches += 1;
            if url.ends_with(".sha256") {
                Ok(self.sha256.clone().into_bytes())
            } else {
                Ok(self.zip.clone())
            }
        }
    }

    fn archive(files: &[(&str, &[u8])]) -> Served {
        let mut zip = zip::ZipWriter::new(Cursor::new(vec![]));
        for (name, data) in files {
            zip.start_file(*name, zip::write::SimpleFileOptions::default())
                .unwrap();
            zip.write_all(data).unwrap();
        }
        let zip = zip.finish().unwrap().into_inner();
        let sha256 = format!("{:x}  archive.zip\n", Sha256::digest(&zip));
        Served {
            zip,
            sha256,
            fetches: 0,
        }
    }

    /// The smallest module: compiling it checks only that it is WebAssembly.
    const EMPTY_MODULE: &[u8] = b"\0asm\x01\0\0\0";

    fn gc_1_2_5n() -> &'static Archive {
        ARCHIVES.iter().find(|a| a.name == "GC_1_2_5n").unwrap()
    }

    #[test]
    fn installs_once_and_checks_the_archive() {
        let root = tempfile::tempdir().unwrap();
        let mut served = archive(&[
            ("LICENSE", b"licence"),
            ("build/GC_1_2_5n/wasm/mwcc.wasm", EMPTY_MODULE),
            ("build/GC_1_2_5n/wasm/mwcc.version", b"version"),
        ]);
        install(root.path(), gc_1_2_5n(), &mut served, "0.0.0").unwrap();
        let module = root.path().join("build/GC_1_2_5n/wasm/mwcc.wasm");
        assert_eq!(fs::read(&module).unwrap(), EMPTY_MODULE);
        assert!(mwcc_wasm::cache_path(&module).exists());
        assert_eq!(served.fetches, 2);
        // (installed: nothing fetched again)
        install(root.path(), gc_1_2_5n(), &mut served, "0.0.0").unwrap();
        assert_eq!(served.fetches, 2);
    }

    #[test]
    fn refuses_a_wrong_checksum() {
        let root = tempfile::tempdir().unwrap();
        let mut served = archive(&[
            ("build/GC_1_2_5n/wasm/mwcc.wasm", EMPTY_MODULE),
            ("build/GC_1_2_5n/wasm/mwcc.version", b"version"),
        ]);
        served.sha256 = format!("{}  archive.zip", "0".repeat(64));
        let error = install(root.path(), gc_1_2_5n(), &mut served, "0.0.0").unwrap_err();
        assert!(format!("{error:#}").contains("SHA-256"), "{error:#}");
        assert!(!root.path().join("build/GC_1_2_5n").exists());
    }

    #[test]
    fn refuses_files_outside_the_modules() {
        let root = tempfile::tempdir().unwrap();
        for name in [
            "build/GC_2_7/wasm/mwcc.wasm",
            "config/GC_1_2_5n/config.json",
            "../escape",
        ] {
            let mut served = archive(&[
                ("build/GC_1_2_5n/wasm/mwcc.wasm", EMPTY_MODULE),
                ("build/GC_1_2_5n/wasm/mwcc.version", b"version"),
                (name, b"x"),
            ]);
            assert!(
                install(root.path(), gc_1_2_5n(), &mut served, "0.0.0").is_err(),
                "{name}"
            );
        }
        assert!(!root.path().join("build/GC_1_2_5n").exists());
    }

    /// A server without the files: an HTTP 404.
    struct Missing;

    impl Fetch for Missing {
        fn fetch(&mut self, _: &str, _: Option<(u64, u64)>) -> Result<Vec<u8>> {
            Err(ureq::Error::StatusCode(404).into())
        }
    }

    #[test]
    fn a_release_without_the_archive_is_unfetched() {
        let root = tempfile::tempdir().unwrap();
        let error = install(root.path(), gc_1_2_5n(), &mut Missing, "0.0.0").unwrap_err();
        let unfetched = error
            .downcast_ref::<Unfetched>()
            .expect("an Unfetched error");
        assert!(unfetched.missing);
        assert!(
            unfetched.url.ends_with("/mwcc-wasm-GC_1_2_5n-0.0.0.zip"),
            "{}",
            unfetched.url
        );
    }

    #[test]
    fn refuses_an_archive_without_its_module() {
        let root = tempfile::tempdir().unwrap();
        let mut served = archive(&[("build/GC_1_2_5n/wasm/mwcc.wasm", EMPTY_MODULE)]);
        let error = install(root.path(), gc_1_2_5n(), &mut served, "0.0.0").unwrap_err();
        assert!(format!("{error:#}").contains(".version"), "{error:#}");
    }

    #[test]
    fn identifies_released_originals_only() {
        let root = tempfile::tempdir().unwrap();
        prepare(root.path()).unwrap();
        let config = root.path().join("config/GC_1_2_5n/config.json");
        assert_eq!(
            fs::read_to_string(&config).unwrap(),
            gc_1_2_5n().members[0].config
        );
        let other = root.path().join("other.exe");
        fs::write(&other, b"not a compiler").unwrap();
        assert!(archive_of(root.path(), &other).unwrap().is_none());
        // (every member's config gives an identity)
        for member in ARCHIVES.iter().flat_map(|a| a.members) {
            assert!(identity(member.config).is_some(), "{}", member.version);
        }
    }
}
