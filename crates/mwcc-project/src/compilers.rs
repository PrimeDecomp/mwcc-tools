//! The compilers a checkout of mwcc runs: for the versions gc2's sources build, the compiler module
//! (`build/<version>/wasm/mwcc.wasm`) runs in place of the stock compiler (`build/compilers/GC/<dir>/mwcceppc.exe`),
//! of the oracle (`build/<version>/oracle/mwcceppc.exe`) and of a project's own copy of either (known by the size and
//! SHA-1 of `config/<version>/config.json`). `MWCC_IN_PROCESS=0` turns that off. An executable without a module runs
//! as a process: natively on Windows, through wibo elsewhere ([`launcher`]).
use anyhow::{Context, Result, bail, ensure};
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
    module_of_copy_if(enabled(), root, executable)
}

fn module_of_copy_if(enabled: bool, root: &Path, executable: &Path) -> Option<(Version, PathBuf)> {
    if !enabled {
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

/// How a traced compile of a project's compiler runs, in a checkout of mwcc.
#[derive(Clone, Debug, PartialEq, Eq)]
pub enum TraceRoute {
    /// In process: the compiler's module ([`module_of_copy`]), which traces.
    Module { version: Version, module: PathBuf },
    /// As a process: the version's native oracle (`build/<version>/oracle/mwcceppc.exe`), the original's code with
    /// the hooks linked in.
    Native { version: Version, oracle: PathBuf },
}

/// How ROOT (a checkout of mwcc) traces a compile by COMPILER, a project's compiler of VERSION (its `mw_version`,
/// MW_VERSION as the project names it): through its module where one runs in its place, else through the
/// version's native oracle, where its `config.json` has an oracle, COMPILER is the original it gives (its SHA-1)
/// and the oracle is built. Each other case is an error that says which it is.
pub fn trace_route(
    root: &Path,
    compiler: &Path,
    version: Option<Version>,
    mw_version: Option<&str>,
) -> Result<TraceRoute> {
    trace_route_if(enabled(), root, compiler, version, mw_version)
}

fn trace_route_if(
    in_process: bool,
    root: &Path,
    compiler: &Path,
    version: Option<Version>,
    mw_version: Option<&str>,
) -> Result<TraceRoute> {
    if let Some((version, module)) = module_of_copy_if(in_process, root, compiler) {
        return Ok(TraceRoute::Module { version, module });
    }
    let named = mw_version.map_or_else(|| compiler.display().to_string(), str::to_owned);
    let Some(version) = version else {
        bail!(
            "no oracle for {named}: mwcc knows no such compiler version (the oracles are of {})",
            oracle_versions(root).join(", ")
        );
    };
    let name = version.name();
    let config = root.join(format!("config/{name}/config.json"));
    let config: serde_json::Value = serde_json::from_slice(
        &fs::read(&config).with_context(|| format!("read {}", config.display()))?,
    )
    .with_context(|| format!("parse {}", config.display()))?;
    ensure!(
        config.get("oracle").is_some(),
        "no oracle for {named}: {name} has none (the oracles are of {}), and no module runs it in process",
        oracle_versions(root).join(", ")
    );
    let digest = format!(
        "{:x}",
        Sha1::digest(fs::read(compiler).with_context(|| format!("read {}", compiler.display()))?)
    );
    ensure!(
        config["sha1"].as_str() == Some(digest.as_str()),
        "no oracle for {}: it is not {name}'s original (SHA-1 {digest}, not {}), which the oracle is built from",
        compiler.display(),
        config["sha1"].as_str().unwrap_or("?")
    );
    let oracle = oracle_executable(root, name);
    ensure!(
        oracle.is_file(),
        "{name}'s oracle is not built: in {}, `mwcc-tools configure --version {name} --oracle`, then `ninja build/{name}/oracle/mwcceppc.exe`",
        root.display()
    );
    Ok(TraceRoute::Native { version, oracle })
}

/// The versions whose `config.json` in ROOT has an oracle.
fn oracle_versions(root: &Path) -> Vec<String> {
    let mut versions: Vec<String> = fs::read_dir(root.join("config"))
        .into_iter()
        .flatten()
        .flatten()
        .filter(|entry| {
            fs::read(entry.path().join("config.json"))
                .ok()
                .and_then(|b| serde_json::from_slice::<serde_json::Value>(&b).ok())
                .is_some_and(|c| c.get("oracle").is_some())
        })
        .map(|entry| entry.file_name().to_string_lossy().into_owned())
        .collect();
    versions.sort();
    versions
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

    /// A checkout of mwcc with VERSION's config (its original's SHA-1 that of ORIGINAL, with an oracle or not).
    fn configured(root: &Path, version: &str, original: &[u8], oracle: bool) {
        let mut config = serde_json::json!({"sha1": format!("{:x}", Sha1::digest(original))});
        if oracle {
            config["oracle"] = serde_json::json!({});
        }
        let path = root.join(format!("config/{version}/config.json"));
        fs::create_dir_all(path.parent().unwrap()).unwrap();
        fs::write(path, config.to_string()).unwrap();
    }

    fn file(path: &Path, bytes: &[u8]) {
        fs::create_dir_all(path.parent().unwrap()).unwrap();
        fs::write(path, bytes).unwrap();
    }

    #[test]
    fn a_trace_runs_through_the_module_else_the_native_oracle() {
        let root = tempfile::tempdir().unwrap();
        let root = root.path();
        let project = tempfile::tempdir().unwrap();
        let route = |compiler: &Path, mw_version: &str| {
            trace_route_if(
                true,
                root,
                compiler,
                Version::parse(mw_version).ok(),
                Some(mw_version),
            )
        };
        // (GC 2.7: the module runs in the stock compiler's place)
        configured(root, "GC_2_7", b"2.7", true);
        let stock = root.join("build/compilers/GC/2.7/mwcceppc.exe");
        file(&stock, b"2.7");
        let module = root.join(module_output("GC_2_7"));
        file(&module, b"module");
        assert_eq!(
            route(&stock, "GC/2.7").unwrap(),
            TraceRoute::Module {
                version: Version::Gc27,
                module: module.clone()
            }
        );
        // (with modules turned off, its oracle, once built)
        let off = trace_route_if(false, root, &stock, Some(Version::Gc27), Some("GC/2.7"));
        assert!(off.unwrap_err().to_string().contains("not built"));
        // (GC 1.3.2: no module; the project's copy of the original runs through the native oracle)
        configured(root, "GC_1_3_2", b"1.3.2", true);
        let copy = project.path().join("build/compilers/GC/1.3.2/mwcceppc.exe");
        file(&copy, b"1.3.2");
        let error = route(&copy, "GC/1.3.2").unwrap_err().to_string();
        assert!(
            error.contains("configure --version GC_1_3_2 --oracle")
                && error.contains("ninja build/GC_1_3_2/oracle/mwcceppc.exe"),
            "{error}"
        );
        let oracle = root.join("build/GC_1_3_2/oracle/mwcceppc.exe");
        file(&oracle, b"oracle");
        assert_eq!(
            route(&copy, "GC/1.3.2").unwrap(),
            TraceRoute::Native {
                version: Version::Gc132,
                oracle
            }
        );
        // (a compiler that is not the original the oracle is built from)
        let other = project.path().join("other/mwcceppc.exe");
        file(&other, b"patched 1.3.2");
        let error = route(&other, "GC/1.3.2").unwrap_err().to_string();
        assert!(error.contains("not GC_1_3_2's original"), "{error}");
        // (a version without an oracle, and one mwcc does not know)
        configured(root, "GC_1_2_5n", b"1.2.5n", false);
        let sdk = project
            .path()
            .join("build/compilers/GC/1.2.5n/mwcceppc.exe");
        file(&sdk, b"1.2.5n");
        let error = route(&sdk, "GC/1.2.5n").unwrap_err().to_string();
        assert!(
            error.contains("no oracle for GC/1.2.5n: GC_1_2_5n has none")
                && error.contains("GC_1_3_2, GC_2_7"),
            "{error}"
        );
        let unknown = project
            .path()
            .join("build/compilers/GC/1.3.2r/mwcceppc.exe");
        file(&unknown, b"1.3.2r");
        let error = route(&unknown, "GC/1.3.2r").unwrap_err().to_string();
        assert!(
            error.contains("no oracle for GC/1.3.2r: mwcc knows no such compiler version"),
            "{error}"
        );
    }
}
