//! `mwcc-tools self update`: an installed release (the directory with `release.json` above `bin/`, as the release
//! archives and `install.sh`/`install.ps1` lay it out) replaced in place by another release, by default the latest.
//!
//! Each file is renamed aside before its replacement is moved in, so a running `mwcc-tools` (this one, or a
//! `serve`) keeps its executable; the files set aside are removed by the next update. A failed update puts the
//! previous files back.
use anyhow::{Context, Result, bail, ensure};
use mwcc_oracle_core::download::{Fetch, HttpFetcher, extract_zip, zip_member};
use sha2::{Digest, Sha256};
use std::{
    collections::BTreeSet,
    env, fs,
    io::Cursor,
    path::{Path, PathBuf},
    time::Duration,
};

const REPOSITORY: &str = "PrimeDecomp/mwcc-tools";
/// The suffix of the files an update set aside.
const OLD: &str = ".mwcc-old";

const USAGE: &str = "self update [--version VERSION] [--check] [--force] [--archive FILE] [--root DIR]
    replaces this installed release with the latest release (or VERSION) for its system: the executables, the
    Python package and the documents. --check only says whether there is a newer one; --archive installs a
    downloaded mwcc-tools-VERSION-TARGET.zip (its .sha256 beside it); --root names the installation, by default
    the one this executable is in. MWCC_TOOLS_LATEST_URL (the latest release's JSON) and MWCC_TOOLS_RELEASE_URL
    (the directory of the releases' v<VERSION>/ asset directories) replace GitHub's";

/// `mwcc-tools self ARGS`.
pub fn command(args: &[String]) -> Result<()> {
    match args.first().map(String::as_str) {
        Some("update") => update(&args[1..]),
        Some("--help" | "-h") | None => {
            println!("{USAGE}");
            Ok(())
        }
        Some(other) => bail!("unknown self command {other}\n{USAGE}"),
    }
}

#[derive(Default)]
struct Options {
    version: Option<String>,
    check: bool,
    force: bool,
    archive: Option<PathBuf>,
    root: Option<PathBuf>,
}

fn update(args: &[String]) -> Result<()> {
    let mut options = Options::default();
    let mut i = 0;
    while i < args.len() {
        match args[i].as_str() {
            "--check" => options.check = true,
            "--force" => options.force = true,
            "--version" => options.version = Some(crate::option_value(args, &mut i)?.clone()),
            "--archive" => options.archive = Some(crate::option_value(args, &mut i)?.into()),
            "--root" => options.root = Some(crate::option_value(args, &mut i)?.into()),
            "--help" | "-h" => {
                println!("{USAGE}");
                return Ok(());
            }
            other => bail!("unknown self update option {other}\n{USAGE}"),
        }
        i += 1;
    }
    let root = match &options.root {
        Some(root) => root.clone(),
        None => installed_root()?,
    };
    let mut fetch = HttpFetcher::new(Duration::from_secs(300))?;
    run(&root, &options, &mut fetch, &Urls::from_env())
}

struct Urls {
    latest: String,
    releases: String,
}

impl Urls {
    fn from_env() -> Self {
        let var = |name| env::var(name).ok().filter(|v| !v.is_empty());
        Self {
            latest: var("MWCC_TOOLS_LATEST_URL").unwrap_or_else(|| {
                format!("https://api.github.com/repos/{REPOSITORY}/releases/latest")
            }),
            releases: var("MWCC_TOOLS_RELEASE_URL")
                .unwrap_or_else(|| format!("https://github.com/{REPOSITORY}/releases/download"))
                .trim_end_matches('/')
                .to_owned(),
        }
    }
}

/// The installation this executable is in: `ROOT/bin/mwcc-tools` with `ROOT/release.json`.
fn installed_root() -> Result<PathBuf> {
    let executable = env::current_exe()?.canonicalize()?;
    let root = executable
        .parent()
        .filter(|bin| bin.file_name().is_some_and(|n| n == "bin"))
        .and_then(Path::parent)
        .filter(|root| root.join("release.json").is_file());
    root.map(Path::to_owned).with_context(|| {
        format!(
            "{} is not an installed release (no release.json above its bin/): update a checkout with git, \
             or install a release (README.md, Installation)",
            shown(&executable)
        )
    })
}

/// A path as the user wrote it (without Windows' `\\?\` of a canonical path).
fn shown(path: &Path) -> String {
    let text = path.display().to_string();
    text.strip_prefix(r"\\?\")
        .map(str::to_owned)
        .unwrap_or(text)
}

struct Installed {
    version: String,
    target: String,
    files: BTreeSet<String>,
}

fn installed(root: &Path) -> Result<Installed> {
    let path = root.join("release.json");
    let metadata: serde_json::Value =
        serde_json::from_slice(&fs::read(&path).with_context(|| format!("read {}", shown(&path)))?)
            .with_context(|| format!("read {}", shown(&path)))?;
    ensure!(
        metadata["package"] == "mwcc-tools",
        "{} is not an mwcc-tools release's",
        shown(&path)
    );
    let text = |key: &str| {
        metadata[key]
            .as_str()
            .map(str::to_owned)
            .with_context(|| format!("{} has no {key}", shown(&path)))
    };
    let files = metadata["files"]
        .as_object()
        .map(|files| files.keys().cloned().collect())
        .unwrap_or_default();
    Ok(Installed {
        version: text("version")?,
        target: text("target")?,
        files,
    })
}

fn run(root: &Path, options: &Options, fetch: &mut dyn Fetch, urls: &Urls) -> Result<()> {
    let current = installed(root)?;
    remove_old(root);
    let (bytes, sidecar) = if let Some(archive) = &options.archive {
        let sidecar = PathBuf::from(format!("{}.sha256", archive.display()));
        (
            fs::read(archive).with_context(|| format!("read {}", archive.display()))?,
            fs::read(&sidecar).with_context(|| format!("read {}", sidecar.display()))?,
        )
    } else {
        let version = match &options.version {
            Some(version) => version.trim_start_matches('v').to_owned(),
            None => latest(fetch, &urls.latest)?,
        };
        if version == current.version && !options.force {
            if options.version.is_some() {
                println!("mwcc-tools {version} is installed");
            } else {
                println!("mwcc-tools {version} is installed, the latest release");
            }
            return Ok(());
        }
        if options.check {
            println!(
                "mwcc-tools {version} is available (installed: {}); `mwcc-tools self update` installs it",
                current.version
            );
            return Ok(());
        }
        let url = format!(
            "{}/v{version}/mwcc-tools-{version}-{}.zip",
            urls.releases, current.target
        );
        eprintln!("mwcc-tools: downloading {url}");
        (
            fetch.fetch(&url, None)?,
            fetch.fetch(&format!("{url}.sha256"), None)?,
        )
    };
    if options.check {
        println!("{} would be installed", archive_version(&bytes)?);
        return Ok(());
    }
    let version = apply(root, &current, &bytes, &sidecar)?;
    println!(
        "mwcc-tools {version} installed in {} (was {})",
        shown(root),
        current.version
    );
    Ok(())
}

/// The latest release's version, from its JSON (GitHub's API skips prereleases).
fn latest(fetch: &mut dyn Fetch, url: &str) -> Result<String> {
    let release: serde_json::Value = serde_json::from_slice(&fetch.fetch(url, None)?)
        .with_context(|| format!("read the latest release from {url}"))?;
    let tag = release["tag_name"]
        .as_str()
        .with_context(|| format!("{url} names no tag_name"))?;
    Ok(tag.trim_start_matches('v').to_owned())
}

fn archive_version(bytes: &[u8]) -> Result<String> {
    let metadata: serde_json::Value =
        serde_json::from_slice(&zip_member(Cursor::new(bytes), "release.json")?)?;
    Ok(metadata["version"]
        .as_str()
        .context("the archive's release.json has no version")?
        .to_owned())
}

/// The release archive BYTES (checked against SIDECAR, a `.sha256`) installed over CURRENT in ROOT; its version.
fn apply(root: &Path, current: &Installed, bytes: &[u8], sidecar: &[u8]) -> Result<String> {
    let expected = String::from_utf8_lossy(sidecar)
        .split_whitespace()
        .next()
        .map(str::to_ascii_lowercase)
        .context("empty checksum file")?;
    let actual = format!("{:x}", Sha256::digest(bytes));
    ensure!(
        actual == expected,
        "the archive's SHA-256 is {actual}, but its .sha256 gives {expected}"
    );
    let metadata: serde_json::Value =
        serde_json::from_slice(&zip_member(Cursor::new(bytes), "release.json")?)
            .context("the archive's release.json")?;
    ensure!(
        metadata["package"] == "mwcc-tools",
        "the archive is not an mwcc-tools release"
    );
    ensure!(
        metadata["target"] == current.target.as_str(),
        "the archive is for {}, but this installation is for {}",
        metadata["target"],
        current.target
    );
    let version = metadata["version"]
        .as_str()
        .context("the archive's release.json has no version")?
        .to_owned();

    let staging = tempfile::Builder::new()
        .prefix(".update-")
        .tempdir_in(root)?;
    extract_zip(Cursor::new(bytes), staging.path())?;
    let mut programs: Vec<String> = metadata["programs"]
        .as_array()
        .map(|p| {
            p.iter()
                .filter_map(|p| p.as_str().map(str::to_owned))
                .collect()
        })
        .unwrap_or_default();
    if let Some(executable) = metadata["executable"].as_str() {
        programs.push(executable.to_owned());
    }
    #[cfg(unix)]
    for program in &programs {
        use std::os::unix::fs::PermissionsExt;
        let path = staging.path().join(program);
        if path.is_file() {
            fs::set_permissions(&path, fs::Permissions::from_mode(0o755))?;
        }
    }
    let mut files = vec![];
    walk(staging.path(), "", &mut files)?;
    // (release.json last: it says which release is installed)
    files.sort_by_key(|f| f == "release.json");
    let incoming: BTreeSet<_> = files.iter().cloned().collect();

    let mut moves: Vec<(PathBuf, Option<PathBuf>)> = vec![];
    let result = (|| -> Result<()> {
        for file in &files {
            let target = root.join(file);
            fs::create_dir_all(target.parent().unwrap())?;
            let aside = set_aside(&target)?;
            moves.push((target.clone(), aside));
            fs::rename(staging.path().join(file), &target)
                .with_context(|| format!("move {} into place", shown(&target)))?;
        }
        // (the files of the installed release that the new one does not have)
        for file in current.files.difference(&incoming) {
            let target = root.join(file);
            if let Some(aside) = set_aside(&target)? {
                moves.push((target, Some(aside)));
            }
        }
        Ok(())
    })();
    if let Err(error) = result {
        for (target, aside) in moves.into_iter().rev() {
            let _ = fs::remove_file(&target);
            if let Some(aside) = aside {
                let _ = fs::rename(aside, &target);
            }
        }
        return Err(error.context("the update failed; the installed release is unchanged"));
    }
    drop(staging);
    remove_old(root);
    Ok(version)
}

/// TARGET renamed aside (to be removed by [`remove_old`]), where it exists.
fn set_aside(target: &Path) -> Result<Option<PathBuf>> {
    if !target.exists() {
        return Ok(None);
    }
    let name = target.file_name().unwrap().to_string_lossy().into_owned();
    for n in 0.. {
        let aside = target.with_file_name(format!("{name}.{n}{OLD}"));
        if aside.exists() && fs::remove_file(&aside).is_err() {
            continue;
        }
        fs::rename(target, &aside).with_context(|| format!("set {} aside", shown(target)))?;
        return Ok(Some(aside));
    }
    unreachable!()
}

/// The files earlier updates set aside, and their unfinished staging directories, removed where they can be (a
/// program still running keeps its file until the next update).
fn remove_old(root: &Path) {
    fn visit(directory: &Path, top: bool) {
        let Ok(entries) = fs::read_dir(directory) else {
            return;
        };
        for entry in entries.flatten() {
            let path = entry.path();
            let name = entry.file_name().to_string_lossy().into_owned();
            if path.is_dir() {
                if top && name.starts_with(".update-") {
                    let _ = fs::remove_dir_all(&path);
                } else {
                    visit(&path, false);
                }
            } else if name.ends_with(OLD) {
                let _ = fs::remove_file(&path);
            }
        }
    }
    visit(root, true);
}

fn walk(root: &Path, relative: &str, files: &mut Vec<String>) -> Result<()> {
    for entry in fs::read_dir(root.join(relative))? {
        let entry = entry?;
        let name = entry.file_name().to_string_lossy().into_owned();
        let child = if relative.is_empty() {
            name
        } else {
            format!("{relative}/{name}")
        };
        if entry.file_type()?.is_dir() {
            walk(root, &child, files)?;
        } else {
            files.push(child);
        }
    }
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::{collections::BTreeMap, io::Write};

    fn archive(version: &str, target: &str, files: &[(&str, &str)]) -> Vec<u8> {
        let mut zip = zip::ZipWriter::new(Cursor::new(Vec::new()));
        let options = zip::write::SimpleFileOptions::default();
        let mut listed = serde_json::Map::new();
        for (name, text) in files {
            zip.start_file(*name, options).unwrap();
            zip.write_all(text.as_bytes()).unwrap();
            listed.insert((*name).into(), serde_json::json!({}));
        }
        zip.start_file("release.json", options).unwrap();
        let metadata = serde_json::json!({
            "package": "mwcc-tools", "version": version, "target": target,
            "executable": "bin/mwcc-tools", "programs": ["bin/mwcc-tools", "bin/mw-ninja"], "files": listed,
        });
        zip.write_all(&serde_json::to_vec(&metadata).unwrap())
            .unwrap();
        zip.finish().unwrap().into_inner()
    }

    fn sidecar(bytes: &[u8]) -> Vec<u8> {
        format!("{:x}  archive.zip\n", Sha256::digest(bytes)).into_bytes()
    }

    /// An installation of BYTES (an archive), as the installers lay it out.
    fn install(bytes: &[u8]) -> tempfile::TempDir {
        let root = tempfile::tempdir().unwrap();
        extract_zip(Cursor::new(bytes), root.path()).unwrap();
        root
    }

    struct Served(BTreeMap<String, Vec<u8>>);
    impl Fetch for Served {
        fn fetch(&mut self, url: &str, _: Option<(u64, u64)>) -> Result<Vec<u8>> {
            self.0
                .get(url)
                .cloned()
                .ok_or_else(|| anyhow::anyhow!("404 {url}"))
        }
    }

    const TARGET: &str = "x86_64-unknown-linux-gnu";

    fn urls() -> Urls {
        Urls {
            latest: "https://api/latest".into(),
            releases: "https://releases".into(),
        }
    }

    fn served(version: &str, bytes: &[u8]) -> Served {
        let url = format!("https://releases/v{version}/mwcc-tools-{version}-{TARGET}.zip");
        Served(BTreeMap::from([
            (
                "https://api/latest".into(),
                format!(r#"{{"tag_name":"v{version}"}}"#).into_bytes(),
            ),
            (format!("{url}.sha256"), sidecar(bytes)),
            (url, bytes.to_vec()),
        ]))
    }

    fn read(root: &Path, file: &str) -> String {
        fs::read_to_string(root.join(file)).unwrap()
    }

    #[test]
    fn updates_to_the_latest_release() {
        let old = archive(
            "0.1.0",
            TARGET,
            &[
                ("bin/mwcc-tools", "old tools"),
                ("python/mwcc_tools/gone.py", "removed"),
                ("README.md", "old"),
            ],
        );
        let root = install(&old);
        fs::write(root.path().join("bin/stale.0.mwcc-old"), "").unwrap();
        let new = archive(
            "0.2.0",
            TARGET,
            &[
                ("bin/mwcc-tools", "new tools"),
                ("bin/mw-ninja", "launcher"),
                ("README.md", "new"),
            ],
        );
        run(
            root.path(),
            &Options::default(),
            &mut served("0.2.0", &new),
            &urls(),
        )
        .unwrap();
        assert_eq!(read(root.path(), "bin/mwcc-tools"), "new tools");
        assert_eq!(read(root.path(), "bin/mw-ninja"), "launcher");
        assert_eq!(read(root.path(), "README.md"), "new");
        assert!(!root.path().join("python/mwcc_tools/gone.py").exists());
        assert_eq!(installed(root.path()).unwrap().version, "0.2.0");
        let mut left = vec![];
        walk(root.path(), "", &mut left).unwrap();
        assert!(
            left.iter()
                .all(|f| !f.ends_with(OLD) && !f.contains(".update-")),
            "{left:?}"
        );
        #[cfg(unix)]
        {
            use std::os::unix::fs::PermissionsExt;
            let mode = fs::metadata(root.path().join("bin/mw-ninja"))
                .unwrap()
                .permissions()
                .mode();
            assert_eq!(mode & 0o111, 0o111);
        }
    }

    #[test]
    fn an_installed_latest_release_and_check_change_nothing() {
        let old = archive("0.2.0", TARGET, &[("bin/mwcc-tools", "tools")]);
        let root = install(&old);
        // (the latest is installed: nothing is downloaded)
        let mut nothing = Served(BTreeMap::from([(
            "https://api/latest".into(),
            br#"{"tag_name":"v0.2.0"}"#.to_vec(),
        )]));
        run(root.path(), &Options::default(), &mut nothing, &urls()).unwrap();
        let new = archive("0.3.0", TARGET, &[("bin/mwcc-tools", "new")]);
        let check = Options {
            check: true,
            ..Default::default()
        };
        let mut fetch = served("0.3.0", &new);
        fetch.0.retain(|url, _| url.contains("latest"));
        run(root.path(), &check, &mut fetch, &urls()).unwrap();
        assert_eq!(read(root.path(), "bin/mwcc-tools"), "tools");
    }

    #[test]
    fn refuses_a_wrong_checksum_or_another_system() {
        let root = install(&archive("0.1.0", TARGET, &[("bin/mwcc-tools", "old")]));
        let current = installed(root.path()).unwrap();
        let new = archive("0.2.0", TARGET, &[("bin/mwcc-tools", "new")]);
        let error = apply(root.path(), &current, &new, &sidecar(b"other")).unwrap_err();
        assert!(error.to_string().contains("SHA-256"), "{error:#}");
        let other = archive(
            "0.2.0",
            "aarch64-apple-darwin",
            &[("bin/mwcc-tools", "new")],
        );
        let error = apply(root.path(), &current, &other, &sidecar(&other)).unwrap_err();
        assert!(
            error.to_string().contains("aarch64-apple-darwin"),
            "{error:#}"
        );
        assert_eq!(read(root.path(), "bin/mwcc-tools"), "old");
    }

    #[test]
    fn a_running_program_is_set_aside_not_overwritten() {
        let root = install(&archive("0.1.0", TARGET, &[("bin/mwcc-tools", "old")]));
        // (an earlier update's file that cannot be removed yet, as a running program's on Windows)
        let blocker = root.path().join("bin/mwcc-tools.0.mwcc-old");
        fs::create_dir(&blocker).unwrap();
        let aside = set_aside(&root.path().join("bin/mwcc-tools"))
            .unwrap()
            .unwrap();
        assert_eq!(aside.file_name().unwrap(), "mwcc-tools.1.mwcc-old");
        assert_eq!(fs::read_to_string(aside).unwrap(), "old");
    }
}
