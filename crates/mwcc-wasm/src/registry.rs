//! The modules a process runs, each loaded once (and again when its file changes): the compiled module, from the
//! artifact beside it (`<module>.cache/`, which the build writes, [`precompile`]), and the version information of the
//! executable it stands for (`<module>.version`, the driver's VS_VERSIONINFO, which the build writes beside it).
//! A run executes on a thread of its own, with room for the compiler's recursion.
use super::{Compiler, Outcome, Request};
use std::{
    collections::HashMap,
    fs,
    path::{Path, PathBuf},
    sync::{
        Arc, Mutex,
        atomic::{AtomicBool, Ordering},
    },
    time::{Duration, SystemTime},
};
use wasmtime::{Result, error::Context, format_err};

/// A module ready to run.
pub struct Loaded {
    pub compiler: Compiler,
    /// What `GetFileVersionInfoA` gives for the executable the module stands for.
    pub version_info: Vec<u8>,
    stamp: (SystemTime, u64),
}

/// MODULE's version information (`mwcc.wasm`: `mwcc.version`).
pub fn version_info_path(module: &Path) -> PathBuf {
    module.with_extension("version")
}

/// MODULE's directory of compiled artifacts (`mwcc.wasm`: `mwcc.cache/`).
pub fn cache_path(module: &Path) -> PathBuf {
    module.with_extension("cache")
}

static LOADED: Mutex<Option<HashMap<PathBuf, Arc<Loaded>>>> = Mutex::new(None);

/// MODULE, loaded: from this process's earlier load while its file is unchanged, else from its artifact (or compiled,
/// its artifact written), with its version information.
pub fn load(module: &Path) -> Result<Arc<Loaded>> {
    let metadata = fs::metadata(module).with_context(|| format!("read {}", module.display()))?;
    let stamp = (metadata.modified()?, metadata.len());
    let mut loaded = LOADED.lock().unwrap();
    let loaded = loaded.get_or_insert_with(HashMap::new);
    if let Some(it) = loaded.get(module)
        && it.stamp == stamp
    {
        return Ok(it.clone());
    }
    let version = version_info_path(module);
    let version_info = fs::read(&version).with_context(|| {
        format!(
            "read {} (the module's version information, written by its build)",
            version.display()
        )
    })?;
    let compiler = Compiler::new_cached(&fs::read(module)?, &cache_path(module))
        .with_context(|| format!("load {}", module.display()))?;
    let it = Arc::new(Loaded {
        compiler,
        version_info,
        stamp,
    });
    loaded.insert(module.to_path_buf(), it.clone());
    Ok(it)
}

/// Write MODULE's compiled artifact into its cache (the build does, after linking it): a process then loads it in
/// milliseconds.
pub fn precompile(module: &Path) -> Result<()> {
    Compiler::new_cached(&fs::read(module)?, &cache_path(module)).map(drop)
}

impl Loaded {
    /// REQUEST run on a thread of its own, while this one polls CANCEL (its own `cancel` is replaced). REQUEST's
    /// `version_info` is ignored: the module's is given.
    pub fn run(&self, request: &Request, cancel: &dyn Fn() -> bool) -> Result<Outcome> {
        let stop = Arc::new(AtomicBool::new(false));
        let caller = std::thread::current();
        std::thread::scope(|scope| -> Result<Outcome> {
            let thread = std::thread::Builder::new()
                .stack_size(64 << 20)
                .name("mwcc".into())
                .spawn_scoped(scope, || {
                    let outcome = self.compiler.run(&Request {
                        version_info: &self.version_info,
                        cancel: Some(stop.clone()),
                        ..*request
                    });
                    caller.unpark();
                    outcome
                })?;
            while !thread.is_finished() {
                if cancel() {
                    stop.store(true, Ordering::Relaxed);
                }
                std::thread::park_timeout(Duration::from_millis(10));
            }
            thread
                .join()
                .map_err(|_| format_err!("the in-process compiler panicked"))?
        })
    }
}
