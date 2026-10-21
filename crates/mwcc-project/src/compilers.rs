//! The compilers a checkout of mwcc runs: for the versions gc2's sources build, the compiler module
//! (`build/<version>/wasm/mwcc.wasm`) runs in place of the stock compiler (`build/compilers/GC/<dir>/mwcceppc.exe`),
//! of the oracle (`build/<version>/oracle/mwcceppc.exe`) and of a project's own copy of either (known by the size and
//! SHA-1 of `config/<version>/config.json`). `MWCC_IN_PROCESS=0` turns that off. An executable without a module runs
//! as a process: natively on Windows, through wibo elsewhere ([`launcher`]).
use anyhow::Result;
use mwcc_trace::version::Version;
use sha1::{Digest, Sha1};
use std::{
    collections::HashMap,
    ffi::OsString,
    fs,
    path::{Path, PathBuf},
    process::Command,
    sync::Mutex,
    time::{Duration, SystemTime},
};

/// The versions gc2's sources build.
pub const VERSIONS: [&str; 2] = ["GC_2_6", "GC_2_7"];

/// VERSION's compiler module, in a checkout of mwcc.
pub fn module_output(version: &str) -> String {
    format!("build/{version}/wasm/mwcc.wasm")
}

/// Whether compiles run in process where a module is built (`MWCC_IN_PROCESS`, on unless `0`).
pub fn enabled() -> bool {
    std::env::var("MWCC_IN_PROCESS").map_or(true, |v| v != "0")
}

fn same(a: &Path, b: &Path) -> bool {
    match (fs::canonicalize(a), fs::canonicalize(b)) {
        (Ok(a), Ok(b)) => a == b,
        // (a compiler that is not there, but whose module is)
        _ => std::path::absolute(a).ok() == std::path::absolute(b).ok(),
    }
}

/// VERSION's stock compiler in ROOT (`build/compilers/GC/<dir>/mwcceppc.exe`).
pub fn stock_executable(root: &Path, version: Version) -> PathBuf {
    root.join(format!(
        "build/compilers/GC/{}/mwcceppc.exe",
        version.compiler_directory()
    ))
}

/// VERSION's oracle executable in ROOT (what a traced run stands for).
fn oracle_executable(root: &Path, version: &str) -> PathBuf {
    root.join(format!("build/{version}/oracle/mwcceppc.exe"))
}

/// The module that runs in EXECUTABLE's place, one of ROOT's (a checkout of mwcc) compilers, if it is built.
pub fn module(root: &Path, executable: &Path) -> Option<(Version, PathBuf)> {
    if !enabled() {
        return None;
    }
    for name in VERSIONS {
        let version = Version::parse(name).ok()?;
        let stock = stock_executable(root, version);
        if !same(executable, &stock) && !same(executable, &oracle_executable(root, name)) {
            continue;
        }
        let module = root.join(module_output(name));
        return module.is_file().then_some((version, module));
    }
    None
}

/// Whether a run in EXECUTABLE's place traces: EXECUTABLE is one of ROOT's oracles.
pub fn traces(root: &Path, executable: &Path) -> bool {
    VERSIONS
        .iter()
        .any(|name| same(executable, &oracle_executable(root, name)))
}

/// The module that runs in place of EXECUTABLE, a compiler anywhere (a project's own copy): [`module`] where it is
/// one of ROOT's, else the module of the version whose original it is: the size and SHA-1 its `config.json` gives
/// (`size`, `sha1`). Only an executable of one of those sizes is read, once while its file is unchanged.
pub fn module_of_copy(root: &Path, executable: &Path) -> Option<(Version, PathBuf)> {
    if !enabled() {
        return None;
    }
    let identity = identity(root, executable);
    [identity.own, identity.copy]
        .into_iter()
        .flatten()
        .find_map(|name| {
            let module = root.join(module_output(name));
            module
                .is_file()
                .then(|| Some((Version::parse(name).ok()?, module)))
                .flatten()
        })
}

/// Which of [`VERSIONS`] an executable is: by its path, one of ROOT's compilers (the first that is), and by its
/// size and SHA-1, a copy of the original a version's `config.json` gives (the first whose size it has).
#[derive(Clone, Copy)]
struct Identity {
    own: Option<&'static str>,
    copy: Option<&'static str>,
}

/// (ROOT, an executable) to the executable's stamp and identity.
type Identities = HashMap<(PathBuf, PathBuf), ((SystemTime, u64), Identity)>;
/// How long ago a file's last change must be for its stamp to identify it.
const SETTLED: Duration = Duration::from_secs(2);
static IDENTITIES: Mutex<Option<Identities>> = Mutex::new(None);

/// [`Identity`] of EXECUTABLE, from this process's earlier look while its file is unchanged (its modification time
/// and size). A file changed in the last [`SETTLED`] is looked at afresh, as a rewrite within its modification
/// time's resolution would keep its stamp.
fn identity(root: &Path, executable: &Path) -> Identity {
    let metadata = fs::metadata(executable).ok();
    let stamp = metadata.as_ref().and_then(|m| {
        let modified = m.modified().ok()?;
        let age = SystemTime::now().duration_since(modified).ok()?;
        (age >= SETTLED).then_some((modified, m.len()))
    });
    let key = (root.to_path_buf(), executable.to_path_buf());
    if let Some(stamp) = stamp
        && let Some((known, identity)) = IDENTITIES
            .lock()
            .unwrap()
            .as_ref()
            .and_then(|m| m.get(&key))
        && *known == stamp
    {
        return *identity;
    }
    let own = VERSIONS.into_iter().find(|name| {
        let stock = Version::parse(name).map(|version| {
            root.join(format!(
                "build/compilers/GC/{}/mwcceppc.exe",
                version.compiler_directory()
            ))
        });
        stock.is_ok_and(|stock| same(executable, &stock))
            || same(executable, &oracle_executable(root, name))
    });
    let copy = metadata.and_then(|metadata| copy_of(root, executable, metadata.len()));
    let identity = Identity { own, copy };
    if let Some(stamp) = stamp {
        IDENTITIES
            .lock()
            .unwrap()
            .get_or_insert_with(HashMap::new)
            .insert(key, (stamp, identity));
    }
    identity
}

/// The first of [`VERSIONS`] whose `config.json` gives EXECUTABLE's SIZE and SHA-1.
fn copy_of(root: &Path, executable: &Path, size: u64) -> Option<&'static str> {
    let mut digest = None;
    for name in VERSIONS {
        let Ok(config) = fs::read(root.join(format!("config/{name}/config.json"))) else {
            continue;
        };
        let Ok(config) = serde_json::from_slice::<serde_json::Value>(&config) else {
            continue;
        };
        if config["size"].as_u64() != Some(size) {
            continue;
        }
        let digest = match &digest {
            Some(digest) => digest,
            None => digest.insert(format!("{:x}", Sha1::digest(fs::read(executable).ok()?))),
        };
        if config["sha1"].as_str() == Some(digest.as_str()) {
            return Some(name);
        }
    }
    None
}

/// The command that runs EXECUTABLE, a Windows program: itself on Windows, else through wibo ([`wibo`], ROOT's).
pub fn launcher(root: &Path, executable: &Path) -> Result<Command> {
    launcher_with_override(&[root], executable, std::env::var_os("MWCC_WIBO"))
}

/// [`launcher`] for a project's build in PROJECT: the project's own wibo (`build/tools/wibo`, the one its build
/// downloads) before ROOT's.
pub fn project_launcher(project: &Path, root: &Path, executable: &Path) -> Result<Command> {
    launcher_with_override(&[project, root], executable, std::env::var_os("MWCC_WIBO"))
}

fn launcher_with_override(
    roots: &[&Path],
    executable: &Path,
    selected: Option<OsString>,
) -> Result<Command> {
    if cfg!(windows) {
        return Ok(Command::new(executable));
    }
    let mut command = Command::new(wibo(roots, selected));
    command.arg(executable);
    Ok(command)
}

/// The wibo that runs a Windows executable: SELECTED (`MWCC_WIBO`), else the first of ROOTS' `build/tools/wibo`,
/// else the one on the path.
fn wibo(roots: &[&Path], selected: Option<OsString>) -> OsString {
    selected
        .or_else(|| {
            roots
                .iter()
                .map(|root| root.join("build/tools/wibo"))
                .find(|local| local.is_file())
                .map(PathBuf::into_os_string)
        })
        .unwrap_or_else(|| OsString::from("wibo"))
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn launcher_selection_is_explicit_and_preserves_argument_boundaries() {
        let root = tempfile::tempdir().unwrap();
        let executable = root.path().join("compiler with spaces.exe");
        let override_path = root.path().join("chosen runner");
        let local = root.path().join("build/tools/wibo");
        fs::create_dir_all(local.parent().unwrap()).unwrap();
        fs::write(&local, b"local runner").unwrap();
        let selected = launcher_with_override(
            &[root.path()],
            &executable,
            Some(override_path.clone().into_os_string()),
        )
        .unwrap();
        let fallback = launcher_with_override(&[root.path()], &executable, None).unwrap();
        if cfg!(windows) {
            assert_eq!(selected.get_program(), executable.as_os_str());
            assert!(selected.get_args().next().is_none());
        } else {
            assert_eq!(selected.get_program(), override_path.as_os_str());
            assert_eq!(fallback.get_program(), local.as_os_str());
            assert_eq!(
                selected.get_args().collect::<Vec<_>>(),
                [executable.as_os_str()]
            );
        }
    }

    #[test]
    fn a_projects_own_wibo_comes_before_the_roots() {
        let project = tempfile::tempdir().unwrap();
        let root = tempfile::tempdir().unwrap();
        let wibo_of = |dir: &Path| {
            let path = dir.join("build/tools/wibo");
            fs::create_dir_all(path.parent().unwrap()).unwrap();
            fs::write(&path, b"wibo").unwrap();
            path
        };
        let roots = [project.path(), root.path()];
        assert_eq!(wibo(&roots, None), OsString::from("wibo"));
        let roots_wibo = wibo_of(root.path());
        assert_eq!(wibo(&roots, None), roots_wibo.as_os_str());
        let projects_wibo = wibo_of(project.path());
        assert_eq!(wibo(&roots, None), projects_wibo.as_os_str());
        assert_eq!(
            wibo(&roots, Some(OsString::from("chosen"))),
            OsString::from("chosen")
        );
    }
}
