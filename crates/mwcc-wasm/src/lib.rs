//! The compilers gc2's sources build (GC 2.6 and 2.7) as WebAssembly modules (`wasm/`; `mwcc-tools configure` and
//! Ninja build them: docs/wasm.md), run in process.
//!
//! The module is the compiler, MSL and its Win32 layer; what it imports is the Win32 API that layer and the driver
//! call (63 functions of kernel32 and version.dll). This crate implements them on the host: files and directories
//! (paths resolved from the run's directory), the standard handles (captured), the environment and command line the
//! caller gives, time, thread-local slots and a heap for `GlobalAlloc`. The compiler's own executable, which the
//! driver looks for and reads its version information from, is a file of the run: `program` names it, and its
//! version information is `version_info`. `ExitProcess` ends the run.
//!
//! Each run instantiates the module afresh: the compiler keeps its state in globals and is not reentrant.
use sha2::{Digest as _, Sha256};
use std::{
    collections::{BTreeMap, BTreeSet, HashMap},
    fs,
    hash::{Hash, Hasher},
    io::Write,
    path::{Path, PathBuf},
    sync::{
        Arc,
        atomic::{AtomicBool, AtomicUsize, Ordering},
    },
    time::{Duration, Instant, SystemTime, UNIX_EPOCH},
};

mod files;
mod overlay;
mod persist;
pub mod pool;
mod registry;
pub use files::SharedFiles;
pub use overlay::Overlay;
pub use persist::{Candidate, MARKER, Persistent, persistent_source, slot_text};
pub use pool::{PersistPool, PoolOptions, PoolStats, split_point};
pub use registry::{Loaded, cache_path, load, precompile, version_info_path};
use wasmtime::{
    Caller, Config, Engine, Extern, InstancePre, Linker, Memory, Module, Result, Store,
    UpdateDeadline, bail, error::Context, format_err,
};

const ERROR_FILE_NOT_FOUND: u32 = 2;
const ERROR_PATH_NOT_FOUND: u32 = 3;
const ERROR_ACCESS_DENIED: u32 = 5;
const ERROR_INVALID_HANDLE: u32 = 6;
const ERROR_NO_MORE_FILES: u32 = 18;
const ERROR_NEGATIVE_SEEK: u32 = 131;
const ERROR_FILE_EXISTS: u32 = 80;
const ERROR_ALREADY_EXISTS: u32 = 183;
const ERROR_RESOURCE_TYPE_NOT_FOUND: u32 = 1813;
const INVALID_HANDLE_VALUE: u32 = 0xFFFF_FFFF;
const GENERIC_READ: u32 = 0x8000_0000;
const FILE_ATTRIBUTE_READONLY: u32 = 0x01;
const FILE_ATTRIBUTE_DIRECTORY: u32 = 0x10;
const FILE_ATTRIBUTE_NORMAL: u32 = 0x80;
const STDIN: u32 = 0x10;
const STDOUT: u32 = 0x14;
const STDERR: u32 = 0x18;
const MODULE: u32 = 0x0040_0000;
/// 100-ns intervals from 1601 to 1970
const FILETIME_UNIX: u64 = 116_444_736_000_000_000;

/// The compiled module, ready to run (compiled once; each run instantiates it). A clone shares it.
#[derive(Clone)]
pub struct Compiler {
    engine: Engine,
    module: InstancePre<State>,
}

/// How often a run checks its timeout and cancellation.
const TICK: Duration = Duration::from_millis(10);

/// One compiler run: what a process needs. [`Run`] adds the host's options; a `Request` runs with
/// their defaults (no trace, no overlay, no Shift JIS transcoding, no read record, the command line
/// made of `program` and `args`, files of its own).
#[derive(Clone)]
pub struct Request<'a> {
    /// The compiler's arguments (without the program).
    pub args: &'a [String],
    /// The directory it runs in: relative paths are from there.
    pub directory: &'a Path,
    /// Its environment (MWCIncludes, ...).
    pub environment: &'a [(String, String)],
    /// The path it runs as (argv[0]): a file of the run, with `version_info`.
    pub program: &'a str,
    /// Its VS_VERSIONINFO resource, as GetFileVersionInfoA gives it.
    pub version_info: &'a [u8],
    /// How long it may run.
    pub timeout: Option<Duration>,
    /// Set to stop it.
    pub cancel: Option<Arc<AtomicBool>>,
}

/// A run's request: a [`Request`] or a [`Run`] ([`Compiler::run`] and [`Compiler::persistent`] take either).
pub trait AsRun {
    fn as_run(&self) -> Run<'_>;
}

impl AsRun for Request<'_> {
    fn as_run(&self) -> Run<'_> {
        Run {
            args: self.args,
            directory: self.directory,
            environment: self.environment,
            program: self.program,
            version_info: self.version_info,
            timeout: self.timeout,
            cancel: self.cancel.clone(),
            trace: false,
            overlay: None,
            transcode_sjis: false,
            record_reads: false,
            command_line: None,
            shared_files: None,
        }
    }
}

impl AsRun for Run<'_> {
    fn as_run(&self) -> Run<'_> {
        self.clone()
    }
}

/// One compiler run, with the host's options.
#[derive(Clone)]
pub struct Run<'a> {
    /// The compiler's arguments (without the program).
    pub args: &'a [String],
    /// The directory it runs in: relative paths are from there.
    pub directory: &'a Path,
    /// Its environment (MWCIncludes, ...).
    pub environment: &'a [(String, String)],
    /// The path it runs as (argv[0]): a file of the run, with `version_info`.
    pub program: &'a str,
    /// Its VS_VERSIONINFO resource, as GetFileVersionInfoA gives it.
    pub version_info: &'a [u8],
    /// How long it may run.
    pub timeout: Option<Duration>,
    /// Set to stop it.
    pub cancel: Option<Arc<AtomicBool>>,
    /// Whether the oracle's hooks trace (the oracle's module): their records are kept in memory, as
    /// [`Outcome::trace`]. A module without the hooks ignores it.
    pub trace: bool,
    /// Files it reads in place of the disk's.
    pub overlay: Option<&'a Overlay>,
    /// Whether it reads sources and headers as sjiswrap gives them: UTF-8 transcoded to Shift JIS, and a byte with
    /// its high bit set a lead byte (`IsDBCSLeadByte`).
    pub transcode_sjis: bool,
    /// Whether the outcome lists the files the compiler consulted ([`Outcome::consulted`]).
    pub record_reads: bool,
    /// The command line it gets (GetCommandLineA) as the process would, verbatim; else one made of `program` and
    /// `args`. The compilers copy it to their heap, and the heap's layout reaches their output.
    pub command_line: Option<&'a str>,
    /// The files it shares with other runs (a build's), else its own ([`SharedFiles`]).
    pub shared_files: Option<&'a Arc<SharedFiles>>,
}

/// A path the compiler consulted outside the overlay: a file it opened to read or asked about (whether it is there
/// or not), or a directory it listed.
#[derive(Clone, Debug, PartialEq, Eq, PartialOrd, Ord, Hash)]
pub enum Consulted {
    Path(PathBuf),
    Listing(PathBuf),
}

pub struct Outcome {
    pub exit_code: i32,
    pub stdout: Vec<u8>,
    pub stderr: Vec<u8>,
    /// Both, in the order written (as a console, or one file for both, has them).
    pub console: Vec<u8>,
    /// Stopped: it ran past its timeout, or was cancelled.
    pub timed_out: bool,
    pub cancelled: bool,
    /// The oracle's trace records, as written (empty unless the request traced).
    pub trace: Vec<u8>,
    /// The run's time: instantiating the module, and running it.
    pub instantiate: Duration,
    pub run: Duration,
    /// With [`Run::record_reads`], what the compiler consulted (each once, in order); the files it wrote are not
    /// among them.
    pub consulted: Vec<Consulted>,
}

enum Handle {
    /// A file open for writing (and READ, reading), in the run's files: where the next read or write is.
    File {
        key: files::Key,
        position: u64,
        read: bool,
    },
    /// A file read from memory (an overlaid file, or a transcoded one): WRITTEN, its write time.
    Memory {
        data: Arc<[u8]>,
        position: u64,
        written: u64,
    },
    Find {
        entries: Vec<Entry>,
        next: usize,
    },
}

#[derive(Clone)]
struct Entry {
    name: String,
    attributes: u32,
    size: u64,
    written: u64,
    /// The 8.3 name (`cAlternateFileName`), or empty: the drivers take it in place of a name of 64 characters or more.
    short: String,
}

struct State {
    directory: PathBuf,
    /// The run's directory as the compiler sees it (guest_path).
    guest_directory: String,
    program: String,
    version_info: Vec<u8>,
    command_line: String,
    environment: Vec<(String, String)>,
    stdout: Vec<u8>,
    stderr: Vec<u8>,
    console: Vec<u8>,
    last_error: u32,
    handles: HashMap<u32, Handle>,
    tls: Vec<u32>,
    heap: Heap,
    started: Instant,
    command_line_at: Option<u32>,
    /// The disk as the run sees it: read once, written when the run ends.
    files: files::Files,
    /// A persistent run's link to its owner (persist.rs).
    persist: Option<persist::Link>,
    /// Whether the hooks trace, and what they have traced.
    tracing: bool,
    trace: Vec<u8>,
    /// The functions whose scopes the trace keeps (MWCC_ORACLE_FUNCTIONS; empty: all), and the scopes open.
    functions: Vec<String>,
    scopes: Vec<usize>,
    overlay: Overlay,
    /// `transcode_sjis`, and each file it has transcoded (by host path).
    sjis: bool,
    encoded: HashMap<PathBuf, Arc<[u8]>>,
    /// With `record_reads`, what the compiler has consulted.
    consulted: Option<(Vec<Consulted>, std::collections::HashSet<Consulted>)>,
    /// The memory the run may grow to ([`memory_limit`]).
    limits: wasmtime::StoreLimits,
    /// The instance's memory, once it is instantiated.
    memory: Option<Memory>,
}

/// The environment's MWCC_ORACLE_FUNCTIONS: the functions a trace keeps the scopes of, one per line: a scope is kept
/// when its `ra.before` gives a link name or a name (`function=`) that has one of them in it (as the tools select a
/// function: a superset of what they then pick).
pub const TRACE_FUNCTIONS: &str = "MWCC_ORACLE_FUNCTIONS";

/// The variable that sets how much memory a run may use, in MiB.
pub const MEMORY_MB: &str = "MWCC_WASM_MEMORY_MB";

/// Seconds after which a run stops as a trap does, with the module's backtrace: for finding where a compile hangs.
pub const STOP_AFTER: &str = "MWCC_WASM_STOP_AFTER";

fn stop_after() -> Option<Duration> {
    static AFTER: std::sync::OnceLock<Option<Duration>> = std::sync::OnceLock::new();
    *AFTER.get_or_init(|| {
        std::env::var(STOP_AFTER)
            .ok()
            .and_then(|v| v.trim().parse::<f64>().ok())
            .filter(|&s| s > 0.0)
            .map(Duration::from_secs_f64)
    })
}

/// The bytes of memory a run may grow to: `MWCC_WASM_MEMORY_MB` of the host's environment, else 1 GiB.
/// A run that needs more stops with "out of memory".
pub fn memory_limit() -> usize {
    static LIMIT: std::sync::OnceLock<usize> = std::sync::OnceLock::new();
    *LIMIT.get_or_init(|| {
        std::env::var(MEMORY_MB)
            .ok()
            .and_then(|v| v.trim().parse::<usize>().ok())
            .filter(|&mb| mb > 0)
            .unwrap_or(1024)
            .saturating_mul(1 << 20)
    })
}

/// A store for STATE with its memory limit.
fn limited_store(engine: &Engine, state: State) -> Store<State> {
    let mut store = Store::new(engine, state);
    store.limiter(|state| &mut state.limits);
    store
}

fn trace_functions(environment: &[(String, String)]) -> Vec<String> {
    environment
        .iter()
        .rev()
        .find(|(key, _)| key == TRACE_FUNCTIONS)
        .map(|(_, value)| {
            value
                .lines()
                .map(str::trim)
                .filter(|name| !name.is_empty())
                .map(String::from)
                .collect()
        })
        .unwrap_or_default()
}

/// Whether the records of a function's scope, BLOCK, are for one of FUNCTIONS (see [`TRACE_FUNCTIONS`]).
fn scope_wanted(block: &[u8], functions: &[String]) -> bool {
    block
        .split(|b| *b == b'\n')
        .filter_map(|line| line.strip_prefix(b"ra.before "))
        .any(|line| {
            String::from_utf8_lossy(line)
                .split_whitespace()
                .filter_map(|field| {
                    field
                        .strip_prefix("link=")
                        .or_else(|| field.strip_prefix("function="))
                })
                .any(|name| functions.iter().any(|f| name.contains(f.as_str())))
        })
}

/// The first handle a run gives.
const FIRST_HANDLE: u32 = 0x100;

impl State {
    fn new(request: &Run) -> Self {
        State {
            directory: plain(request.directory),
            guest_directory: guest_path(&plain(request.directory).to_string_lossy()),
            program: guest_path(request.program),
            version_info: request.version_info.to_vec(),
            command_line: request.command_line.map_or_else(
                || command_line(&guest_path(request.program), request.args),
                str::to_owned,
            ),
            // (the host's own setting: the compiler's environment, and so its heap, is as without it)
            environment: request
                .environment
                .iter()
                .filter(|(key, _)| key != TRACE_FUNCTIONS)
                .cloned()
                .collect(),
            stdout: vec![],
            stderr: vec![],
            console: vec![],
            last_error: 0,
            handles: HashMap::new(),
            tls: vec![],
            heap: Heap::default(),
            started: Instant::now(),
            command_line_at: None,
            files: files::Files::new(request.shared_files.cloned()),
            persist: None,
            tracing: request.trace,
            trace: vec![],
            functions: trace_functions(request.environment),
            scopes: vec![],
            overlay: request.overlay.cloned().unwrap_or_default(),
            sjis: request.transcode_sjis,
            encoded: HashMap::new(),
            consulted: request.record_reads.then(Default::default),
            limits: wasmtime::StoreLimitsBuilder::new()
                .memory_size(memory_limit())
                .build(),
            memory: None,
        }
    }

    /// A handle for HANDLE: the lowest value not in use, as Windows reuses a closed handle's value. The compiler keeps
    /// file handles in 16-bit reference numbers, so values that only grew passed 0x8000 in a large compile and were
    /// sign-extended (a precompile of many headers failed with OSErr -51).
    fn open(&mut self, handle: Handle) -> u32 {
        let value = (FIRST_HANDLE..)
            .step_by(4)
            .find(|v| !self.handles.contains_key(v))
            .unwrap();
        self.handles.insert(value, handle);
        value
    }
}

/// The heap GlobalAlloc gives: blocks of the wasm memory above what the module uses, grown as needed. As Windows's
/// heap, it reuses freed memory: a request takes the smallest free block it fits in (its rest stays free), a freed
/// block merges with its free neighbours, and a block grows in place where the memory after it is free. (The compiler
/// grows its buffers by GlobalReAlloc in small steps: blocks that always moved left each old one behind, and a
/// compile with debugging information used almost the whole 4 GiB.)
#[derive(Clone, Default)]
struct Heap {
    /// Each block in use: its size.
    sizes: HashMap<u32, u32>,
    /// The free blocks below `top`, by address: their sizes. No two are adjacent.
    free: BTreeMap<u32, u32>,
    /// The same, by (size, address).
    by_size: BTreeSet<(u32, u32)>,
    /// The end of the blocks given out; the memory is grown to `end`.
    top: u32,
    end: u32,
    /// The highest top since the memory was last restored (persist.rs: what is to be zeroed above the snapshot).
    peak: u32,
}

/// The compiler trapped (a crash: an access out of bounds, an `abort`, a stack overflow): the trap, and what the run
/// wrote and traced before it. A run's error is this one when it trapped.
#[derive(Clone)]
pub struct Trapped {
    pub message: String,
    pub console: Vec<u8>,
    pub trace: Vec<u8>,
}

impl std::fmt::Display for Trapped {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(f, "the compiler trapped: {}", self.message)
    }
}
// (the message, not the bytes)
impl std::fmt::Debug for Trapped {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        std::fmt::Display::fmt(self, f)
    }
}
impl std::error::Error for Trapped {}

/// The run ended (ExitProcess), with this status.
#[derive(Debug)]
struct Exit(i32);

impl std::fmt::Display for Exit {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(f, "exit {}", self.0)
    }
}
impl std::error::Error for Exit {}

/// The run was stopped.
#[derive(Debug)]
enum Stopped {
    TimedOut,
    Cancelled,
}

impl std::fmt::Display for Stopped {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.write_str(match self {
            Stopped::TimedOut => "timed out",
            Stopped::Cancelled => "cancelled",
        })
    }
}
impl std::error::Error for Stopped {}

/// The engine the compiler's module is compiled for (and its cached artifacts: they load only into an engine of the
/// same settings).
fn engine() -> Result<Engine> {
    let mut config = Config::new();
    config.wasm_exceptions(true);
    config.epoch_interruption(true);
    // (the compiler recurses deeply: its own 8 MiB shadow stack, and room on the host's)
    config.max_wasm_stack(2 << 20);
    Engine::new(&config)
}

/// The name of WASM's artifact for ENGINE: a hash of the module and of the engine's settings and version.
fn artifact_name(engine: &Engine, wasm: &[u8]) -> String {
    struct Digest(Sha256);
    impl Hasher for Digest {
        fn write(&mut self, bytes: &[u8]) {
            self.0.update(bytes);
        }
        fn finish(&self) -> u64 {
            u64::from_le_bytes(self.0.clone().finalize()[..8].try_into().unwrap())
        }
    }
    let mut digest = Digest(Sha256::new());
    digest.write_usize(wasm.len());
    digest.write(wasm);
    engine.precompile_compatibility_hash().hash(&mut digest);
    let hash = digest.0.finalize();
    let hex: String = hash[..16].iter().map(|b| format!("{b:02x}")).collect();
    format!("{hex}.{ARTIFACT}")
}

const ARTIFACT: &str = "cwasm";

/// WASM compiled for ENGINE: loaded from its artifact in CACHE (true), or compiled and its artifact written there.
fn cached(engine: &Engine, wasm: &[u8], cache: &Path) -> Result<(Module, bool)> {
    let path = cache.join(artifact_name(engine, wasm));
    if path.is_file() {
        // SAFETY: the cache's artifacts are written by `store` only: whole, under another name and then renamed, and
        // never changed after; and named for this module and engine (whose settings wasmtime checks again).
        if let Ok(module) = unsafe { Module::deserialize_file(engine, &path) } {
            return Ok((module, true));
        }
    }
    let artifact = engine
        .precompile_module(wasm)
        .context("compile the compiler's module")?;
    // SAFETY: ENGINE has just compiled it.
    let module = unsafe { Module::deserialize(engine, &artifact) }?;
    // (a cache that cannot be written only means compiling again)
    let _ = store(cache, &path, &artifact);
    Ok((module, false))
}

/// Writes ARTIFACT at PATH in CACHE, and removes the cache's other artifacts (of earlier builds of the module) and
/// the temporary files of writes that did not finish.
fn store(cache: &Path, path: &Path, artifact: &[u8]) -> std::io::Result<()> {
    static WRITES: AtomicUsize = AtomicUsize::new(0);
    fs::create_dir_all(cache)?;
    let temporary = path.with_extension(format!(
        "{}-{}.{TEMPORARY}",
        std::process::id(),
        WRITES.fetch_add(1, Ordering::Relaxed)
    ));
    // (on the disk before it has its name; when another process has written and loaded it meanwhile, the rename
    // fails: its artifact stays)
    let written = fs::File::create(&temporary)
        .and_then(|mut file| {
            file.write_all(artifact)?;
            file.sync_all()
        })
        .and_then(|()| fs::rename(&temporary, path));
    if written.is_err() {
        let _ = fs::remove_file(&temporary);
        return written;
    }
    for entry in fs::read_dir(cache)?.flatten() {
        let other = entry.path();
        let unfinished = || {
            other.extension().is_some_and(|e| e == TEMPORARY)
                && entry
                    .metadata()
                    .and_then(|m| m.modified())
                    .is_ok_and(|t| t.elapsed().is_ok_and(|age| age > UNFINISHED))
        };
        // (one that is loaded stays, on Windows)
        if other != path && (other.extension().is_some_and(|e| e == ARTIFACT) || unfinished()) {
            let _ = fs::remove_file(other);
        }
    }
    Ok(())
}

const TEMPORARY: &str = "tmp";
/// How old a temporary file is before it is taken for one of a write that did not finish (and not one being written).
const UNFINISHED: Duration = Duration::from_secs(10 * 60);

impl Compiler {
    pub fn new(wasm: &[u8]) -> Result<Self> {
        let engine = engine()?;
        let module = Module::new(&engine, wasm).context("compile the compiler's module")?;
        Self::with_module(engine, &module)
    }

    /// [`Compiler::new`], with the compiled module kept in CACHE, a directory of the module's own: later processes
    /// load it from there instead of compiling it again. Its artifact is named for the module and the engine; one
    /// that does not load, or cannot be written, means compiling the module. Writing one removes the directory's
    /// others.
    pub fn new_cached(wasm: &[u8], cache: &Path) -> Result<Self> {
        let engine = engine()?;
        let (module, _) = cached(&engine, wasm, cache)?;
        Self::with_module(engine, &module)
    }

    fn with_module(engine: Engine, module: &Module) -> Result<Self> {
        let mut linker = Linker::new(&engine);
        imports(&mut linker)?;
        let module = linker.instantiate_pre(module)?;
        // (the clock of every run's deadline)
        let clock = engine.weak();
        std::thread::spawn(move || {
            while let Some(engine) = clock.upgrade() {
                engine.increment_epoch();
                drop(engine);
                std::thread::sleep(TICK);
            }
        });
        Ok(Self { engine, module })
    }

    /// A new instance in STORE, its heap above the module's memory: its entry.
    fn instantiate(&self, store: &mut Store<State>) -> Result<wasmtime::TypedFunc<(), ()>> {
        let instance = self.module.instantiate(&mut *store)?;
        let memory = instance
            .get_memory(&mut *store, "memory")
            .context("the module exports no memory")?;
        let pages = memory.size(&*store) as u32;
        store.data_mut().memory = Some(memory);
        store.data_mut().heap.top = pages << 16;
        store.data_mut().heap.end = pages << 16;
        instance.get_typed_func::<(), ()>(&mut *store, "_start")
    }

    pub fn run(&self, request: &impl AsRun) -> Result<Outcome> {
        let request = &request.as_run();
        let mut store = limited_store(&self.engine, State::new(request));
        let started = Instant::now();
        let timeout = request.timeout;
        let cancel = request.cancel.clone();
        store.set_epoch_deadline(1);
        store.epoch_deadline_callback(move |_| {
            if cancel.as_ref().is_some_and(|c| c.load(Ordering::Relaxed)) {
                return Err(Stopped::Cancelled.into());
            }
            if timeout.is_some_and(|t| started.elapsed() >= t) {
                return Err(Stopped::TimedOut.into());
            }
            if stop_after().is_some_and(|t| started.elapsed() >= t) {
                return Err(wasmtime::Error::msg(format!("stopped after {STOP_AFTER}")));
            }
            Ok(UpdateDeadline::Continue(1))
        });
        let start = self.instantiate(&mut store)?;
        let instantiate = started.elapsed();
        let (mut timed_out, mut cancelled) = (false, false);
        let exit_code = match start.call(&mut store, ()) {
            Ok(()) => 0,
            Err(error) => match (
                error.downcast_ref::<Exit>(),
                error.downcast_ref::<Stopped>(),
            ) {
                (Some(Exit(code)), _) => *code,
                (_, Some(Stopped::TimedOut)) => {
                    timed_out = true;
                    1
                }
                (_, Some(Stopped::Cancelled)) => {
                    cancelled = true;
                    1
                }
                _ => {
                    let state = store.into_data();
                    return Err(Trapped {
                        message: format!("{error:?}"),
                        console: state.console,
                        trace: state.trace,
                    }
                    .into());
                }
            },
        };
        let mut state = store.into_data();
        // (what the module wrote reaches the disk now that it has ended: as its process's would, whatever its exit
        // code. A run stopped or trapped leaves the disk as it was)
        let mut exit_code = exit_code;
        if !(timed_out || cancelled) {
            for (path, error) in state.files.flush() {
                let message = format!(
                    "could not write {}: {error}
",
                    path.display()
                );
                state.stderr.extend(message.as_bytes());
                state.console.extend(message.as_bytes());
                if exit_code == 0 {
                    exit_code = 1;
                }
            }
        }
        Ok(Outcome {
            exit_code,
            stdout: state.stdout,
            stderr: state.stderr,
            console: state.console,
            timed_out,
            cancelled,
            trace: state.trace,
            instantiate,
            run: started.elapsed() - instantiate,
            consulted: state.consulted.map(|(list, _)| list).unwrap_or_default(),
        })
    }
}
/// PATH without a verbatim prefix (`\\?\`, as canonicalized paths have on Windows): the form a process's current
/// directory takes.
pub fn plain(path: &Path) -> PathBuf {
    let text = path.to_string_lossy();
    match text.strip_prefix(r"\\?\") {
        Some(rest) => match rest.strip_prefix(r"UNC\") {
            Some(share) => PathBuf::from(format!(r"\\{share}")),
            None => PathBuf::from(rest),
        },
        None => path.to_path_buf(),
    }
}

/// The command line a Windows program gets for PROGRAM and ARGS (each quoted as CreateProcess's callers quote).
fn command_line(program: &str, args: &[String]) -> String {
    let mut out = String::new();
    for (i, arg) in std::iter::once(program)
        .chain(args.iter().map(String::as_str))
        .enumerate()
    {
        if i > 0 {
            out.push(' ');
        }
        if !arg.is_empty() && !arg.contains([' ', '\t', '"']) {
            out.push_str(arg);
            continue;
        }
        out.push('"');
        let mut backslashes = 0;
        for c in arg.chars() {
            match c {
                '\\' => backslashes += 1,
                '"' => {
                    out.extend(std::iter::repeat_n('\\', backslashes * 2 + 1));
                    out.push('"');
                    backslashes = 0;
                    continue;
                }
                _ => {}
            }
            if c != '\\' {
                out.extend(std::iter::repeat_n('\\', backslashes));
                backslashes = 0;
                out.push(c);
            }
        }
        out.extend(std::iter::repeat_n('\\', backslashes * 2));
        out.push('"');
    }
    out
}

// ------------------------------------------------------------------------------------------------ memory

fn memory(caller: &mut Caller<'_, State>) -> Result<Memory> {
    if let Some(memory) = caller.data().memory {
        return Ok(memory);
    }
    match caller.get_export("memory") {
        Some(Extern::Memory(m)) => Ok(m),
        _ => bail!("the module exports no memory"),
    }
}

fn read_bytes(caller: &mut Caller<'_, State>, at: u32, len: usize) -> Result<Vec<u8>> {
    let mut out = vec![0; len];
    memory(caller)?.read(&*caller, at as usize, &mut out)?;
    Ok(out)
}

fn write_bytes(caller: &mut Caller<'_, State>, at: u32, data: &[u8]) -> Result<()> {
    let m = memory(caller)?;
    m.write(&mut *caller, at as usize, data)?;
    Ok(())
}

fn write_u32(caller: &mut Caller<'_, State>, at: u32, value: u32) -> Result<()> {
    if at != 0 {
        write_bytes(caller, at, &value.to_le_bytes())?;
    }
    Ok(())
}

fn read_u32(caller: &mut Caller<'_, State>, at: u32) -> Result<u32> {
    let mut bytes = [0; 4];
    memory(caller)?.read(&*caller, at as usize, &mut bytes)?;
    Ok(u32::from_le_bytes(bytes))
}

/// The NUL-terminated string at AT (Windows-1252, as the compiler's char strings are).
fn read_string(caller: &mut Caller<'_, State>, at: u32) -> Result<String> {
    let m = memory(caller)?;
    let data = m.data(&*caller);
    let start = at as usize;
    let end = data
        .get(start..)
        .with_context(|| format!("string at {at:#x} is outside memory"))?
        .iter()
        .position(|b| *b == 0)
        .map(|n| start + n)
        .context("unterminated string")?;
    Ok(data[start..end].iter().map(|b| char::from(*b)).collect())
}

fn latin1(text: &str) -> Vec<u8> {
    text.chars().map(|c| c as u32 as u8).collect()
}

/// Copy TEXT and a NUL to BUFFER of SIZE bytes, as the A functions do: its length, or the size it needs.
fn give_string(caller: &mut Caller<'_, State>, text: &str, buffer: u32, size: u32) -> Result<u32> {
    let mut bytes = latin1(text);
    if bytes.len() as u32 + 1 > size {
        return Ok(bytes.len() as u32 + 1);
    }
    let len = bytes.len() as u32;
    bytes.push(0);
    write_bytes(caller, buffer, &bytes)?;
    Ok(len)
}

/// SIZE as the heap gives it: a multiple of 16, at least 16.
fn block_size(size: u32) -> u32 {
    (size.max(1) + 15) & !15
}

impl Heap {
    fn alloc(
        &mut self,
        memory: &Memory,
        store: &mut impl wasmtime::AsContextMut,
        size: u32,
    ) -> Result<u32> {
        let size = block_size(size);
        if let Some(&(free, at)) = self.by_size.range((size, 0)..).next() {
            self.take_free(at, free);
            if free > size {
                self.add_free(at + size, free - size);
            }
            self.sizes.insert(at, size);
            return Ok(at);
        }
        let at = self.top;
        self.extend(memory, store, at + size)?;
        self.sizes.insert(at, size);
        Ok(at)
    }

    /// Grow the block at AT to SIZE where it is, if the memory after it is free (or the top): whether it grew.
    fn grow_in_place(
        &mut self,
        memory: &Memory,
        store: &mut impl wasmtime::AsContextMut,
        at: u32,
        size: u32,
    ) -> Result<bool> {
        let size = block_size(size);
        let old = self.sizes[&at];
        if size <= old {
            return Ok(true);
        }
        let next = at + old;
        if next == self.top {
            self.extend(memory, store, at + size)?;
        } else if let Some(&free) = self.free.get(&next)
            && old + free >= size
        {
            self.take_free(next, free);
            if old + free > size {
                self.add_free(at + size, old + free - size);
            }
        } else {
            return Ok(false);
        }
        self.sizes.insert(at, size);
        Ok(true)
    }

    fn free(&mut self, at: u32) -> bool {
        let Some(mut size) = self.sizes.remove(&at) else {
            return false;
        };
        let mut at = at;
        if let Some(&next) = self.free.get(&(at + size)) {
            self.take_free(at + size, next);
            size += next;
        }
        if let Some((&previous, &previous_size)) = self.free.range(..at).next_back()
            && previous + previous_size == at
        {
            self.take_free(previous, previous_size);
            at = previous;
            size += previous_size;
        }
        if at + size == self.top {
            self.top = at;
        } else {
            self.add_free(at, size);
        }
        true
    }

    /// Move the top to TOP, growing the memory if it ends before.
    fn extend(
        &mut self,
        memory: &Memory,
        store: &mut impl wasmtime::AsContextMut,
        top: u32,
    ) -> Result<()> {
        if top > self.end {
            let pages = (top - self.end).div_ceil(1 << 16);
            memory.grow(&mut *store, u64::from(pages)).map_err(|_| {
                format_err!(
                    "out of memory: more than {} MiB ({MEMORY_MB})",
                    memory_limit() >> 20
                )
            })?;
            self.end += pages << 16;
        }
        self.top = top;
        self.peak = self.peak.max(top);
        Ok(())
    }

    fn add_free(&mut self, at: u32, size: u32) {
        self.free.insert(at, size);
        self.by_size.insert((size, at));
    }

    fn take_free(&mut self, at: u32, size: u32) {
        self.free.remove(&at);
        self.by_size.remove(&(size, at));
    }
}

fn heap_alloc(caller: &mut Caller<'_, State>, size: u32, zero: bool) -> Result<u32> {
    let m = memory(caller)?;
    let mut heap = std::mem::take(&mut caller.data_mut().heap);
    let at = heap.alloc(&m, caller, size);
    caller.data_mut().heap = heap;
    let at = at?;
    if zero {
        let size = caller.data().heap.sizes[&at] as usize;
        write_bytes(caller, at, &vec![0; size])?;
    }
    Ok(at)
}

// ------------------------------------------------------------------------------------------------ paths

/// How the compiler sees the host's PATH: on a Unix host, the file system is drive `Z:` (as wibo has it). Its
/// separators are Windows's (`C:/work`, which Windows takes, fails the compiler's own path checks: MacSpecs.c).
fn guest_path(path: &str) -> String {
    if cfg!(windows) {
        path.replace('/', "\\")
    } else if path.starts_with('/') {
        format!("Z:{}", path.replace('/', "\\"))
    } else {
        path.replace('/', "\\")
    }
}

/// The host path of the compiler's PATH, relative ones from the run's directory, spelled as given.
fn native_path(state: &State, path: &str) -> PathBuf {
    let native = if cfg!(windows) {
        path.to_owned()
    } else {
        let path = path.replace('\\', "/");
        match path.get(..2) {
            Some("Z:" | "z:") => path[2..].to_owned(),
            _ => path,
        }
    };
    state.directory.join(native)
}

/// The host file of the compiler's PATH: [`native_path`], and on a host other than Windows, each of its names as the
/// file system has it, matched case-insensitively as Windows (and wibo) match them ([`Listings::resolve`]).
fn host_path(state: &mut State, path: &str) -> PathBuf {
    let path = native_path(state, path);
    if cfg!(windows) {
        return path;
    }
    state.files.resolve(&path)
}

/// What the run's overlay says of the compiler's PATH: not overlaid, absent (`Some(None)`), or its bytes.
fn overlaid(state: &State, path: &str) -> Option<Option<Arc<[u8]>>> {
    if state.overlay.is_empty() {
        return None;
    }
    let plain = overlay::normal(&state.directory, &native_path(state, path));
    state.overlay.get(&plain).map(|bytes| bytes.cloned())
}

/// Note that the compiler consulted WHAT (with `record_reads`).
fn consult(state: &mut State, what: Consulted) {
    if let Some((list, seen)) = &mut state.consulted
        && seen.insert(what.clone())
    {
        list.push(what);
    }
}

/// BYTES, the file the compiler opened as NAME (at host PATH), as sjiswrap gives them (transcoded once per path).
/// sjiswrap's warning goes to the standard error; its error ends the run, as it ends the process.
fn sjis(state: &mut State, name: &str, path: &Path, bytes: &[u8]) -> Result<Arc<[u8]>> {
    if let Some(data) = state.encoded.get(path) {
        return Ok(data.clone());
    }
    match overlay::transcode(Path::new(name), bytes) {
        Ok((data, warning)) => {
            if let Some(warning) = warning {
                state.stderr.extend(warning.as_bytes());
                state.console.extend(warning.as_bytes());
            }
            let data: Arc<[u8]> = data.into();
            state.encoded.insert(path.to_path_buf(), data.clone());
            Ok(data)
        }
        Err(message) => {
            let message = format!("{message}\n");
            state.stderr.extend(message.as_bytes());
            state.console.extend(message.as_bytes());
            Err(Exit(1).into())
        }
    }
}

fn is_program(state: &State, path: &str) -> bool {
    let a = native_path(state, path);
    let b = native_path(state, &state.program);
    a == b
        || a.to_string_lossy()
            .eq_ignore_ascii_case(&b.to_string_lossy())
}

/// GetFullPathNameA's: the absolute path, its separators backslashes and its `.` and `..` resolved.
fn full_path(state: &State, path: &str) -> String {
    let path = path.replace('/', "\\");
    let absolute = if path.starts_with("\\\\") || path.get(1..3) == Some(":\\") {
        path
    } else {
        let directory = state.guest_directory.clone();
        if path.starts_with('\\') {
            format!("{}{path}", directory.get(..2).unwrap_or(""))
        } else {
            format!("{}\\{path}", directory.trim_end_matches('\\'))
        }
    };
    // (a verbatim path is taken as it is)
    if absolute.starts_with(r"\\?\") {
        return absolute;
    }
    let (root, rest) = match absolute.get(1..3) {
        Some(":\\") => absolute.split_at(3),
        _ if absolute.starts_with(r"\\") => absolute.split_at(2),
        _ => absolute.split_at(0),
    };
    let mut parts: Vec<&str> = vec![];
    for part in rest.split('\\') {
        match part {
            "" | "." => {}
            ".." => {
                parts.pop();
            }
            _ => parts.push(part),
        }
    }
    let mut out = format!("{root}{}", parts.join("\\"));
    if rest.ends_with('\\') && !parts.is_empty() {
        out.push('\\');
    }
    out
}

fn filetime(time: SystemTime) -> u64 {
    match time.duration_since(UNIX_EPOCH) {
        Ok(d) => FILETIME_UNIX + d.as_secs() * 10_000_000 + u64::from(d.subsec_nanos() / 100),
        Err(_) => FILETIME_UNIX,
    }
}

fn entry_of(name: String, metadata: &fs::Metadata) -> Entry {
    let mut attributes = if metadata.is_dir() {
        FILE_ATTRIBUTE_DIRECTORY
    } else {
        0
    };
    if metadata.permissions().readonly() {
        attributes |= FILE_ATTRIBUTE_READONLY;
    }
    if attributes == 0 {
        attributes = FILE_ATTRIBUTE_NORMAL;
    }
    Entry {
        name,
        attributes,
        size: if metadata.is_dir() { 0 } else { metadata.len() },
        written: metadata.modified().map(filetime).unwrap_or(FILETIME_UNIX),
        short: String::new(),
    }
}

fn io_error(error: &std::io::Error) -> u32 {
    use std::io::ErrorKind::*;
    match error.kind() {
        NotFound => ERROR_FILE_NOT_FOUND,
        PermissionDenied => ERROR_ACCESS_DENIED,
        AlreadyExists => ERROR_FILE_EXISTS,
        _ => error
            .raw_os_error()
            .map_or(ERROR_ACCESS_DENIED, |e| e as u32),
    }
}

/// Whether NAME matches the wildcard PATTERN (`*` and `?`, case-insensitively, as FindFirstFile matches).
fn matches(pattern: &[u8], name: &[u8]) -> bool {
    match (pattern.first(), name.first()) {
        (None, None) => true,
        (Some(b'*'), _) => {
            matches(&pattern[1..], name) || (!name.is_empty() && matches(pattern, &name[1..]))
        }
        (Some(b'?'), Some(_)) => matches(&pattern[1..], &name[1..]),
        (Some(p), Some(n)) => p.eq_ignore_ascii_case(n) && matches(&pattern[1..], &name[1..]),
        _ => false,
    }
}

// ------------------------------------------------------------------------------------------------ time

/// (year, month, day of week, day) of DAYS since 1970-01-01
fn civil(days: i64) -> (i64, u32, u32, u32) {
    let z = days + 719_468;
    let era = z.div_euclid(146_097);
    let doe = z - era * 146_097;
    let yoe = (doe - doe / 1460 + doe / 36_524 - doe / 146_096) / 365;
    let doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    let mp = (5 * doy + 2) / 153;
    let day = (doy - (153 * mp + 2) / 5 + 1) as u32;
    let month = if mp < 10 { mp + 3 } else { mp - 9 } as u32;
    let year = yoe + era * 400 + i64::from(month <= 2);
    let weekday = (days + 4).rem_euclid(7) as u32;
    (year, month, weekday, day)
}

fn days_from_civil(year: i64, month: u32, day: u32) -> i64 {
    let year = year - i64::from(month <= 2);
    let era = year.div_euclid(400);
    let yoe = year - era * 400;
    let m = i64::from(month);
    let doy = (153 * (if m > 2 { m - 3 } else { m + 9 }) + 2) / 5 + i64::from(day) - 1;
    let doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    era * 146_097 + doe - 719_468
}

/// A SYSTEMTIME of FILETIME (UTC)
fn systemtime(filetime: u64) -> [u8; 16] {
    let ticks = filetime.saturating_sub(FILETIME_UNIX);
    let seconds = (ticks / 10_000_000) as i64;
    let (year, month, weekday, day) = civil(seconds.div_euclid(86_400));
    let in_day = seconds.rem_euclid(86_400);
    let fields = [
        year as u16,
        month as u16,
        weekday as u16,
        day as u16,
        (in_day / 3600) as u16,
        (in_day / 60 % 60) as u16,
        (in_day % 60) as u16,
        (ticks / 10_000 % 1000) as u16,
    ];
    let mut out = [0; 16];
    for (i, f) in fields.iter().enumerate() {
        out[2 * i..2 * i + 2].copy_from_slice(&f.to_le_bytes());
    }
    out
}

fn now_filetime() -> u64 {
    filetime(SystemTime::now())
}

// ------------------------------------------------------------------------------------------------ version info

/// VerQueryValueA on the VS_VERSIONINFO tree BLOCK (as the module holds it, at BASE): the address of SUB-BLOCK's
/// value and its length (in characters for text, terminator included). Text values are given in ANSI, written to
/// the second half of the module's buffer (GetFileVersionInfoSizeA asks for twice the resource for that).
fn query_version(block: &[u8], sub_block: &str) -> Option<(usize, usize, bool)> {
    fn node(data: &[u8], at: usize) -> Option<(usize, usize, u16, String, usize)> {
        let length = u16::from_le_bytes(data.get(at..at + 2)?.try_into().ok()?) as usize;
        let value_length = u16::from_le_bytes(data.get(at + 2..at + 4)?.try_into().ok()?) as usize;
        let kind = u16::from_le_bytes(data.get(at + 4..at + 6)?.try_into().ok()?);
        let mut key = vec![];
        let mut p = at + 6;
        loop {
            let c = u16::from_le_bytes(data.get(p..p + 2)?.try_into().ok()?);
            p += 2;
            if c == 0 {
                break;
            }
            key.push(c);
        }
        let value = (p + 3) & !3;
        Some((
            length,
            value_length,
            kind,
            String::from_utf16_lossy(&key),
            value,
        ))
    }
    let (length, value_length, _, _, value) = node(block, 0)?;
    let parts: Vec<&str> = sub_block.split('\\').filter(|p| !p.is_empty()).collect();
    if parts.is_empty() {
        return Some((value, value_length, false));
    }
    // (the children follow the value, each aligned to 4)
    let mut start = (value + value_length + 3) & !3;
    let mut end = length;
    let mut found = None;
    for (depth, part) in parts.iter().enumerate() {
        found = None;
        let mut at = start;
        while at + 6 <= end {
            let (child_length, child_value_length, kind, key, child_value) = node(block, at)?;
            if child_length == 0 {
                break;
            }
            if key.eq_ignore_ascii_case(part) {
                let bytes = if kind == 1 {
                    child_value_length * 2
                } else {
                    child_value_length
                };
                found = Some((child_value, child_value_length, kind == 1));
                start = (child_value + bytes + 3) & !3;
                end = at + child_length;
                break;
            }
            at = (at + child_length + 3) & !3;
        }
        found?;
        let _ = depth;
    }
    found
}

// ------------------------------------------------------------------------------------------------ imports

macro_rules! import {
    ($linker:expr, $name:literal, |$caller:ident $(, $arg:ident : $ty:ty)*| -> $ret:ty $body:block) => {
        $linker.func_wrap("env", $name, |mut $caller: Caller<'_, State>, $($arg: $ty),*| -> Result<$ret> {
            #[allow(unused_mut)]
            let mut $caller = &mut $caller;
            $body
        })?;
    };
}

fn fail(caller: &mut Caller<'_, State>, error: u32) -> Result<u32> {
    caller.data_mut().last_error = error;
    Ok(0)
}

fn imports(linker: &mut Linker<State>) -> Result<()> {
    // --- the oracle's trace sink (wasm/src/oracle.c)
    import!(linker, "oracle_tracing", |c| -> i32 {
        Ok(i32::from(c.data().tracing))
    });
    import!(linker, "oracle_scope", |c, entering: i32| -> () {
        let state = c.data_mut();
        if !state.tracing || state.functions.is_empty() {
            return Ok(());
        }
        if entering != 0 {
            let at = state.trace.len();
            state.scopes.push(at);
        } else if let Some(at) = state.scopes.pop()
            && state.scopes.is_empty()
            && !scope_wanted(&state.trace[at..], &state.functions)
        {
            // (the opcodes' names are recorded once, where an opcode is first printed: the scopes kept need them)
            let names: Vec<u8> = state.trace[at..]
                .split_inclusive(|b| *b == b'\n')
                .filter(|line| line.starts_with(b"pc.opcode "))
                .flatten()
                .copied()
                .collect();
            state.trace.truncate(at);
            state.trace.extend_from_slice(&names);
        }
        Ok(())
    });
    import!(linker, "oracle_trace", |c, at: u32, size: u32| -> () {
        if c.data().tracing {
            let bytes = read_bytes(c, at, size as usize)?;
            c.data_mut().trace.extend_from_slice(&bytes);
        }
        Ok(())
    });
    // --- process, environment, errors
    import!(linker, "ExitProcess", |c, code: i32| -> () {
        let _ = c;
        Err(Exit(code).into())
    });
    import!(linker, "GetLastError", |c| -> u32 {
        Ok(c.data().last_error)
    });
    import!(linker, "GetCommandLineA", |c| -> u32 {
        if let Some(at) = c.data().command_line_at {
            return Ok(at);
        }
        let mut text = latin1(&c.data().command_line);
        text.push(0);
        let at = heap_alloc(c, text.len() as u32, false)?;
        write_bytes(c, at, &text)?;
        c.data_mut().command_line_at = Some(at);
        Ok(at)
    });
    import!(linker, "GetEnvironmentStrings", |c| -> u32 {
        let mut block = vec![];
        for (name, value) in &c.data().environment {
            block.extend(latin1(&format!("{name}={value}")));
            block.push(0);
        }
        block.push(0);
        let at = heap_alloc(c, block.len() as u32, false)?;
        write_bytes(c, at, &block)?;
        Ok(at)
    });
    import!(linker, "FreeEnvironmentStringsA", |c, at: u32| -> u32 {
        c.data_mut().heap.free(at);
        Ok(1)
    });
    import!(linker, "GetModuleHandleA", |c, name: u32| -> u32 {
        if name == 0 {
            return Ok(MODULE);
        }
        let name = read_string(c, name)?;
        if is_program(c.data(), &name) {
            Ok(MODULE)
        } else {
            fail(c, 126)
        }
    });
    import!(linker, "FindResourceA", |c,
                                      _module: u32,
                                      _name: u32,
                                      _kind: u32|
     -> u32 {
        // (the original's resources are its version information and icon: no MACRSRC)
        fail(c, ERROR_RESOURCE_TYPE_NOT_FOUND)
    });
    import!(linker, "LoadResource", |c,
                                     _module: u32,
                                     _resource: u32|
     -> u32 {
        fail(c, ERROR_INVALID_HANDLE)
    });
    import!(linker, "LockResource", |c, _resource: u32| -> u32 {
        let _ = c;
        Ok(0)
    });
    import!(linker, "SizeofResource", |c,
                                       _module: u32,
                                       _resource: u32|
     -> u32 {
        fail(c, ERROR_INVALID_HANDLE)
    });
    import!(linker, "GetSystemDefaultLangID", |c| -> u32 {
        let _ = c;
        Ok(0x409)
    });
    import!(linker, "IsDBCSLeadByte", |c, byte: u32| -> u32 {
        // (sjiswrap's: any byte with its high bit set, as Shift JIS has them)
        Ok(u32::from(c.data().sjis && byte & 0x80 != 0))
    });
    import!(linker, "FormatMessageA", |c,
                                       _flags: u32,
                                       _source: u32,
                                       id: u32,
                                       _language: u32,
                                       buffer: u32,
                                       size: u32,
                                       _args: u32|
     -> u32 {
        let text = match id {
            2 => "The system cannot find the file specified.\r\n",
            3 => "The system cannot find the path specified.\r\n",
            5 => "Access is denied.\r\n",
            32 => {
                "The process cannot access the file because it is being used by another process.\r\n"
            }
            _ => "",
        };
        if text.is_empty() {
            return fail(c, 317);
        }
        let n = give_string(c, text, buffer, size)?;
        Ok(if n > text.len() as u32 { 0 } else { n })
    });
    import!(linker, "SetConsoleCtrlHandler", |c,
                                              _handler: u32,
                                              _add: u32|
     -> u32 {
        let _ = c;
        Ok(1)
    });
    import!(linker, "GetConsoleScreenBufferInfo", |c,
                                                   _handle: u32,
                                                   _info: u32|
     -> u32 {
        fail(c, ERROR_INVALID_HANDLE)
    });
    import!(linker, "CreateProcessA", |c,
                                       _1: u32,
                                       _2: u32,
                                       _3: u32,
                                       _4: u32,
                                       _5: u32,
                                       _6: u32,
                                       _7: u32,
                                       _8: u32,
                                       _9: u32,
                                       _10: u32|
     -> u32 {
        fail(c, ERROR_FILE_NOT_FOUND)
    });
    import!(linker, "WaitForSingleObject", |c,
                                            _handle: u32,
                                            _ms: u32|
     -> u32 {
        let _ = c;
        Ok(0xFFFF_FFFF)
    });
    import!(linker, "GetExitCodeProcess", |c,
                                           _handle: u32,
                                           _code: u32|
     -> u32 {
        fail(c, ERROR_INVALID_HANDLE)
    });
    import!(linker, "GetSystemDirectoryA", |c,
                                            buffer: u32,
                                            size: u32|
     -> u32 {
        give_string(c, "C:\\WINDOWS\\system32", buffer, size)
    });
    import!(linker, "GetWindowsDirectoryA", |c,
                                             buffer: u32,
                                             size: u32|
     -> u32 {
        give_string(c, "C:\\WINDOWS", buffer, size)
    });
    // --- threads (one): local storage and critical sections
    import!(linker, "TlsAlloc", |c| -> u32 {
        c.data_mut().tls.push(0);
        Ok(c.data().tls.len() as u32 - 1)
    });
    import!(linker, "TlsFree", |c, _index: u32| -> u32 {
        let _ = c;
        Ok(1)
    });
    import!(linker, "TlsSetValue", |c, index: u32, value: u32| -> u32 {
        match c.data_mut().tls.get_mut(index as usize) {
            Some(slot) => {
                *slot = value;
                Ok(1)
            }
            None => fail(c, 87),
        }
    });
    import!(linker, "TlsGetValue", |c, index: u32| -> u32 {
        c.data_mut().last_error = 0;
        Ok(c.data().tls.get(index as usize).copied().unwrap_or(0))
    });
    for name in [
        "InitializeCriticalSection",
        "DeleteCriticalSection",
        "EnterCriticalSection",
        "LeaveCriticalSection",
    ] {
        linker.func_wrap("env", name, |_: Caller<'_, State>, _section: u32| {})?;
    }
    // --- memory
    import!(linker, "GlobalAlloc", |c, flags: u32, size: u32| -> u32 {
        heap_alloc(c, size, flags & 0x40 != 0)
    });
    import!(linker, "GlobalReAlloc", |c,
                                      at: u32,
                                      size: u32,
                                      flags: u32|
     -> u32 {
        let old = c.data().heap.sizes.get(&at).copied();
        let Some(old) = old else {
            return fail(c, ERROR_INVALID_HANDLE);
        };
        let zero = flags & 0x40 != 0;
        let m = memory(c)?;
        let mut heap = std::mem::take(&mut c.data_mut().heap);
        let grown = heap.grow_in_place(&m, c, at, size);
        c.data_mut().heap = heap;
        if grown? {
            let new = c.data().heap.sizes[&at];
            if zero && new > old {
                write_bytes(c, at + old, &vec![0; (new - old) as usize])?;
            }
            return Ok(at);
        }
        let new = heap_alloc(c, size, zero)?;
        let data = read_bytes(c, at, old as usize)?;
        write_bytes(c, new, &data)?;
        c.data_mut().heap.free(at);
        Ok(new)
    });
    import!(linker, "GlobalFlags", |c, _at: u32| -> u32 {
        let _ = c;
        Ok(0)
    });
    import!(linker, "GlobalFree", |c, at: u32| -> u32 {
        if c.data_mut().heap.free(at) {
            Ok(0)
        } else {
            c.data_mut().last_error = ERROR_INVALID_HANDLE;
            Ok(at)
        }
    });
    // --- time
    import!(linker, "GetTickCount", |c| -> u32 {
        Ok(c.data().started.elapsed().as_millis() as u32)
    });
    import!(linker, "GetSystemTime", |c, at: u32| -> () {
        write_bytes(c, at, &systemtime(now_filetime()))
    });
    import!(linker, "GetLocalTime", |c, at: u32| -> () {
        write_bytes(c, at, &systemtime(now_filetime()))
    });
    import!(linker, "GetTimeZoneInformation", |c, at: u32| -> u32 {
        write_bytes(c, at, &[0; 172])?;
        Ok(0)
    });
    import!(linker, "SystemTimeToFileTime", |c,
                                             system: u32,
                                             file: u32|
     -> u32 {
        let b = read_bytes(c, system, 16)?;
        let f = |i: usize| u16::from_le_bytes([b[2 * i], b[2 * i + 1]]);
        let days = days_from_civil(i64::from(f(0)), u32::from(f(1)), u32::from(f(3)));
        let seconds =
            days * 86_400 + i64::from(f(4)) * 3600 + i64::from(f(5)) * 60 + i64::from(f(6));
        let ticks = (seconds * 10_000_000 + i64::from(f(7)) * 10_000) as u64 + FILETIME_UNIX;
        write_bytes(c, file, &ticks.to_le_bytes())?;
        Ok(1)
    });
    import!(linker, "FileTimeToSystemTime", |c,
                                             file: u32,
                                             system: u32|
     -> u32 {
        let b = read_bytes(c, file, 8)?;
        write_bytes(
            c,
            system,
            &systemtime(u64::from_le_bytes(b.try_into().unwrap())),
        )?;
        Ok(1)
    });
    // --- version information (of the program)
    import!(linker, "GetFileVersionInfoSizeA", |c,
                                                path: u32,
                                                handle: u32|
     -> u32 {
        write_u32(c, handle, 0)?;
        let path = read_string(c, path)?;
        if !is_program(c.data(), &path) || c.data().version_info.is_empty() {
            return fail(c, 1812);
        }
        Ok(c.data().version_info.len() as u32 * 2)
    });
    import!(linker, "GetFileVersionInfoA", |c,
                                            path: u32,
                                            _handle: u32,
                                            size: u32,
                                            at: u32|
     -> u32 {
        let path = read_string(c, path)?;
        if !is_program(c.data(), &path) {
            return fail(c, 1812);
        }
        let info = c.data().version_info.clone();
        let n = info.len().min(size as usize);
        write_bytes(c, at, &info[..n])?;
        Ok(1)
    });
    import!(linker, "VerQueryValueA", |c,
                                       block: u32,
                                       sub_block: u32,
                                       value: u32,
                                       length: u32|
     -> u32 {
        let sub_block = read_string(c, sub_block)?;
        let size = c.data().version_info.len();
        let data = read_bytes(c, block, size)?;
        let Some((at, len, text)) = query_version(&data, &sub_block) else {
            return Ok(0);
        };
        if !text {
            write_u32(c, value, block + at as u32)?;
            write_u32(c, length, len as u32)?;
            return Ok(1);
        }
        // (the text in ANSI, in the buffer's second half at the same offset)
        let ansi: Vec<u8> = data[at..at + 2 * len]
            .chunks(2)
            .map(|w| u16::from_le_bytes([w[0], w[1]]) as u8)
            .collect();
        let target = block + (size + at) as u32;
        write_bytes(c, target, &ansi)?;
        write_u32(c, value, target)?;
        write_u32(c, length, len as u32)?;
        Ok(1)
    });
    // --- standard handles
    import!(linker, "GetStdHandle", |c, which: u32| -> u32 {
        let _ = c;
        Ok(match which as i32 {
            -10 => STDIN,
            -11 => STDOUT,
            -12 => STDERR,
            _ => INVALID_HANDLE_VALUE,
        })
    });
    import!(linker, "SetStdHandle", |c,
                                     _which: u32,
                                     _handle: u32|
     -> u32 {
        let _ = c;
        Ok(1)
    });
    // --- files
    import!(linker, "CreateFileA", |c,
                                    path: u32,
                                    access: u32,
                                    _share: u32,
                                    _security: u32,
                                    creation: u32,
                                    _flags: u32,
                                    _template: u32|
     -> u32 {
        let name = read_string(c, path)?;
        let path = host_path(c.data_mut(), &name);
        let writing = access & 0x4000_0000 != 0 || matches!(creation, 1 | 2 | 4 | 5);
        // (sjiswrap transcodes a source or header opened only for reading)
        let transcoded =
            c.data().sjis && access == GENERIC_READ && overlay::is_text_file(Path::new(&name));
        match overlaid(c.data(), &name) {
            Some(_) if writing => {
                c.data_mut().last_error = ERROR_ACCESS_DENIED;
                return Ok(INVALID_HANDLE_VALUE);
            }
            Some(None) => {
                c.data_mut().last_error = ERROR_FILE_NOT_FOUND;
                return Ok(INVALID_HANDLE_VALUE);
            }
            Some(Some(bytes)) => {
                let written = c
                    .data_mut()
                    .files
                    .stat(&path)
                    .1
                    .entry()
                    .map_or(overlay::OVERLAY_ONLY_WRITTEN, |e| e.written);
                let data = if transcoded {
                    sjis(c.data_mut(), &name, &path, &bytes)?
                } else {
                    bytes
                };
                let handle = c.data_mut().open(Handle::Memory {
                    data,
                    position: 0,
                    written,
                });
                c.data_mut().last_error = 0;
                return Ok(handle);
            }
            None => {}
        }
        if !writing {
            consult(c.data_mut(), Consulted::Path(path.clone()));
        }
        let (key, found) = c.data_mut().files.stat(&path);
        let refused = match &found {
            files::Found::Directory(_) => Some(ERROR_ACCESS_DENIED),
            files::Found::File(e) if writing && e.attributes & FILE_ATTRIBUTE_READONLY != 0 => {
                Some(ERROR_ACCESS_DENIED)
            }
            _ => None,
        };
        if let Some(error) = refused {
            c.data_mut().last_error = error;
            return Ok(INVALID_HANDLE_VALUE);
        }
        let empty = match files::disposition(&found, creation) {
            Ok(empty) => empty,
            Err(error) => {
                c.data_mut().last_error = error;
                return Ok(INVALID_HANDLE_VALUE);
            }
        };
        let handle = if writing {
            if empty {
                c.data_mut().files.create(&path, &found);
            }
            Handle::File {
                key,
                position: 0,
                read: access & GENERIC_READ != 0 || access & 0x4000_0000 == 0,
            }
        } else {
            let written = found.entry().map_or(FILETIME_UNIX, |e| e.written);
            let cached = transcoded
                .then(|| c.data().encoded.get(&path).cloned())
                .flatten();
            let data = match cached {
                Some(data) => data,
                None => match c.data_mut().files.contents(&key) {
                    Ok(bytes) if transcoded => sjis(c.data_mut(), &name, &path, &bytes)?,
                    Ok(bytes) => bytes,
                    Err(error) => {
                        c.data_mut().last_error = error;
                        return Ok(INVALID_HANDLE_VALUE);
                    }
                },
            };
            Handle::Memory {
                data,
                position: 0,
                written,
            }
        };
        let handle = c.data_mut().open(handle);
        // (CREATE_ALWAYS and OPEN_ALWAYS on an existing file succeed and say so)
        c.data_mut().last_error =
            if matches!(found, files::Found::File(_)) && matches!(creation, 2 | 4) {
                ERROR_ALREADY_EXISTS
            } else {
                0
            };
        Ok(handle)
    });
    import!(linker, "CloseHandle", |c, handle: u32| -> u32 {
        if c.data_mut().handles.remove(&handle).is_some()
            || matches!(handle, STDIN | STDOUT | STDERR)
        {
            Ok(1)
        } else {
            fail(c, ERROR_INVALID_HANDLE)
        }
    });
    import!(linker, "ReadFile", |c,
                                 handle: u32,
                                 buffer: u32,
                                 count: u32,
                                 read: u32,
                                 _overlapped: u32|
     -> u32 {
        if handle == STDIN {
            write_u32(c, read, 0)?;
            return Ok(1);
        }
        // (read into the buffer in the guest's memory)
        let memory = memory(c)?;
        let (guest, state) = memory.data_and_store_mut(&mut *c);
        let target = guest
            .get_mut(buffer as usize..)
            .and_then(|rest| rest.get_mut(..count as usize))
            .with_context(|| format!("ReadFile's buffer at {buffer:#x} is outside memory"));
        let result = match state.handles.get_mut(&handle) {
            Some(Handle::Memory {
                data: contents,
                position,
                ..
            }) => {
                let data = target?;
                let start = (*position as usize).min(contents.len());
                let n = (contents.len() - start).min(data.len());
                data[..n].copy_from_slice(&contents[start..start + n]);
                *position += n as u64;
                Ok(n)
            }
            Some(Handle::File {
                key,
                position,
                read: true,
            }) => {
                let data = target?;
                let (key, at) = (key.clone(), *position);
                let read = state.files.read(&key, at, data);
                if let (Ok(n), Some(Handle::File { position, .. })) =
                    (&read, state.handles.get_mut(&handle))
                {
                    *position = at + *n as u64;
                }
                read
            }
            Some(Handle::File { .. }) => Err(ERROR_ACCESS_DENIED),
            _ => Err(ERROR_INVALID_HANDLE),
        };
        match result {
            Ok(n) => {
                write_u32(c, read, n as u32)?;
                Ok(1)
            }
            Err(e) => fail(c, e),
        }
    });
    import!(linker, "WriteFile", |c,
                                  handle: u32,
                                  buffer: u32,
                                  count: u32,
                                  written: u32,
                                  _overlapped: u32|
     -> u32 {
        let data = read_bytes(c, buffer, count as usize)?;
        let result = match handle {
            STDOUT => {
                c.data_mut().stdout.extend(&data);
                c.data_mut().console.extend(&data);
                Ok(())
            }
            STDERR => {
                c.data_mut().stderr.extend(&data);
                c.data_mut().console.extend(&data);
                Ok(())
            }
            _ => {
                let state = c.data_mut();
                match state.handles.get_mut(&handle) {
                    Some(Handle::File { key, position, .. }) => {
                        let (key, at) = (key.clone(), *position);
                        state.files.write(&key, at, &data).map(|end| {
                            if let Some(Handle::File { position, .. }) =
                                state.handles.get_mut(&handle)
                            {
                                *position = end;
                            }
                        })
                    }
                    Some(Handle::Memory { .. }) => Err(ERROR_ACCESS_DENIED),
                    _ => Err(ERROR_INVALID_HANDLE),
                }
            }
        };
        match result {
            Ok(()) => {
                write_u32(c, written, count)?;
                Ok(1)
            }
            Err(e) => fail(c, e),
        }
    });
    import!(linker, "SetFilePointer", |c,
                                       handle: u32,
                                       low: i32,
                                       high: u32,
                                       method: u32|
     -> u32 {
        let distance = if high != 0 {
            i64::from(read_u32(c, high)? as i32) << 32 | i64::from(low as u32)
        } else {
            i64::from(low)
        };
        let state = c.data_mut();
        let result = match state.handles.get_mut(&handle) {
            // (past the end is allowed: a write there fills the gap with zeros)
            Some(Handle::File { key, position, .. }) => match state.files.entry(key) {
                Ok(entry) => {
                    let to = match method {
                        0 => distance,
                        1 => *position as i64 + distance,
                        _ => entry.size as i64 + distance,
                    };
                    if to < 0 {
                        Err(ERROR_NEGATIVE_SEEK)
                    } else {
                        *position = to as u64;
                        Ok(*position)
                    }
                }
                Err(e) => Err(e),
            },
            // (within the file, as sjiswrap moves in its transcoded ones)
            Some(Handle::Memory { data, position, .. }) => {
                let end = data.len() as i64;
                let to = match method {
                    0 => distance,
                    1 => *position as i64 + distance,
                    _ => end + distance,
                };
                *position = to.clamp(0, end) as u64;
                Ok(*position)
            }
            _ => Err(ERROR_INVALID_HANDLE),
        };
        match result {
            Ok(position) => {
                if high != 0 {
                    write_u32(c, high, (position >> 32) as u32)?;
                }
                c.data_mut().last_error = 0;
                Ok(position as u32)
            }
            Err(e) => {
                c.data_mut().last_error = e;
                Ok(INVALID_HANDLE_VALUE)
            }
        }
    });
    import!(linker, "GetFileSize", |c, handle: u32, high: u32| -> u32 {
        let state = c.data();
        let size = match state.handles.get(&handle) {
            Some(Handle::File { key, .. }) => state.files.entry(key).ok().map(|e| e.size),
            Some(Handle::Memory { data, .. }) => Some(data.len() as u64),
            _ => None,
        };
        match size {
            Some(size) => {
                write_u32(c, high, (size >> 32) as u32)?;
                Ok(size as u32)
            }
            None => {
                c.data_mut().last_error = ERROR_INVALID_HANDLE;
                Ok(INVALID_HANDLE_VALUE)
            }
        }
    });
    import!(linker, "SetEndOfFile", |c, handle: u32| -> u32 {
        let state = c.data_mut();
        let result = match state.handles.get(&handle) {
            Some(Handle::File { key, position, .. }) => state.files.set_len(key, *position),
            Some(Handle::Memory { .. }) => Err(ERROR_ACCESS_DENIED),
            _ => Err(ERROR_INVALID_HANDLE),
        };
        match result {
            Ok(()) => Ok(1),
            Err(e) => fail(c, e),
        }
    });
    import!(linker, "GetFileTime", |c,
                                    handle: u32,
                                    created: u32,
                                    accessed: u32,
                                    written: u32|
     -> u32 {
        let state = c.data();
        let time = match state.handles.get(&handle) {
            Some(Handle::File { key, .. }) => state.files.entry(key).ok().map(|e| e.written),
            Some(Handle::Memory { written, .. }) => Some(*written),
            _ => None,
        };
        let Some(time) = time else {
            return fail(c, ERROR_INVALID_HANDLE);
        };
        for at in [created, accessed, written] {
            if at != 0 {
                write_bytes(c, at, &time.to_le_bytes())?;
            }
        }
        Ok(1)
    });
    import!(linker, "SetFileTime", |c,
                                    _handle: u32,
                                    _created: u32,
                                    _accessed: u32,
                                    _written: u32|
     -> u32 {
        let _ = c;
        Ok(1)
    });
    import!(linker, "GetFileAttributesA", |c, path: u32| -> u32 {
        let name = read_string(c, path)?;
        if is_program(c.data(), &name) {
            return Ok(FILE_ATTRIBUTE_NORMAL);
        }
        match overlaid(c.data(), &name) {
            Some(Some(_)) => return Ok(FILE_ATTRIBUTE_NORMAL),
            Some(None) => {
                c.data_mut().last_error = ERROR_FILE_NOT_FOUND;
                return Ok(INVALID_HANDLE_VALUE);
            }
            None => {}
        }
        let path = host_path(c.data_mut(), &name);
        consult(c.data_mut(), Consulted::Path(path.clone()));
        let (_, found) = c.data_mut().files.stat(&path);
        match found.entry() {
            Some(e) => Ok(e.attributes),
            None => {
                c.data_mut().last_error = files::not_found(&found);
                Ok(INVALID_HANDLE_VALUE)
            }
        }
    });
    import!(linker, "SetFileAttributesA", |c,
                                           _path: u32,
                                           _attributes: u32|
     -> u32 {
        let _ = c;
        Ok(1)
    });
    import!(linker, "DeleteFileA", |c, path: u32| -> u32 {
        let name = read_string(c, path)?;
        if overlaid(c.data(), &name).is_some() {
            return fail(c, ERROR_ACCESS_DENIED);
        }
        let path = host_path(c.data_mut(), &name);
        match c.data_mut().files.delete(&path) {
            Ok(()) => Ok(1),
            Err(e) => fail(c, e),
        }
    });
    import!(linker, "CreateDirectoryA", |c,
                                         path: u32,
                                         _security: u32|
     -> u32 {
        let name = read_string(c, path)?;
        let path = host_path(c.data_mut(), &name);
        match c.data_mut().files.create_directory(&path) {
            Ok(()) => Ok(1),
            Err(e) => fail(c, e),
        }
    });
    import!(linker, "RemoveDirectoryA", |c, path: u32| -> u32 {
        let name = read_string(c, path)?;
        let path = host_path(c.data_mut(), &name);
        match c.data_mut().files.remove_directory(&path) {
            Ok(()) => Ok(1),
            Err(e) => fail(c, e),
        }
    });
    import!(linker, "GetCurrentDirectoryA", |c,
                                             size: u32,
                                             buffer: u32|
     -> u32 {
        let directory = c.data().guest_directory.clone();
        give_string(c, &directory, buffer, size)
    });
    import!(linker, "GetFullPathNameA", |c,
                                         path: u32,
                                         size: u32,
                                         buffer: u32,
                                         file_part: u32|
     -> u32 {
        let name = read_string(c, path)?;
        let full = full_path(c.data(), &name);
        let n = give_string(c, &full, buffer, size)?;
        if n <= full.len() as u32 && file_part != 0 {
            let at = match full.rfind('\\') {
                Some(i) if i + 1 < full.len() => buffer + i as u32 + 1,
                _ => 0,
            };
            write_u32(c, file_part, at)?;
        }
        Ok(n)
    });
    import!(linker, "GetShortPathNameA", |c,
                                          path: u32,
                                          buffer: u32,
                                          size: u32|
     -> u32 {
        let name = read_string(c, path)?;
        let mut short = None;
        let there = match overlaid(c.data(), &name) {
            Some(bytes) => bytes.is_some(),
            None if is_program(c.data(), &name) => true,
            None => {
                let path = host_path(c.data_mut(), &name);
                consult(c.data_mut(), Consulted::Path(path.clone()));
                // (the last component's 8.3 name, where Windows has one: the 2.7 drivers give it in place of a name
                // of 64 characters or more; the rest of the path as the run spelled it)
                let last = name.trim_end_matches(['\\', '/']);
                let start = last.rfind(['\\', '/']).map_or(0, |i| i + 1);
                if last.len() - start >= 64 {
                    let shortened = short_name(&path);
                    if !shortened.is_empty() {
                        short = Some(format!(
                            "{}{shortened}{}",
                            &name[..start],
                            &name[last.len()..]
                        ));
                    }
                }
                c.data_mut().files.stat(&path).1.entry().is_some()
            }
        };
        if !there {
            return fail(c, ERROR_FILE_NOT_FOUND);
        }
        give_string(c, short.as_deref().unwrap_or(&name), buffer, size)
    });
    import!(linker, "FindFirstFileA", |c,
                                       pattern: u32,
                                       data: u32|
     -> u32 {
        let pattern = read_string(c, pattern)?;
        let (directory, wildcard) = match pattern.rfind(['\\', '/']) {
            Some(i) => (pattern[..i + 1].to_owned(), pattern[i + 1..].to_owned()),
            None => (String::new(), pattern.clone()),
        };
        let mut entries = vec![];
        if is_program(c.data(), &pattern) {
            entries.push(Entry {
                name: wildcard.clone(),
                attributes: FILE_ATTRIBUTE_NORMAL,
                size: 0,
                written: FILETIME_UNIX,
                short: String::new(),
            });
        } else if let Some(bytes) = overlaid(c.data(), &pattern) {
            if let Some(bytes) = bytes {
                let path = host_path(c.data_mut(), &pattern);
                let found = c.data_mut().files.stat(&path).1;
                let (name, written) = match found.entry() {
                    Some(e) => (e.name.clone(), e.written),
                    None => (wildcard.clone(), overlay::OVERLAY_ONLY_WRITTEN),
                };
                entries.push(Entry {
                    name,
                    attributes: FILE_ATTRIBUTE_NORMAL,
                    size: bytes.len() as u64,
                    written,
                    short: String::new(),
                });
            }
        } else if !wildcard.contains(['*', '?']) {
            let path = host_path(c.data_mut(), &pattern);
            consult(c.data_mut(), Consulted::Path(path.clone()));
            // (the name as the directory has it)
            if let Some(e) = c.data_mut().files.stat(&path).1.entry() {
                entries.push(e.clone());
            }
        } else {
            let directory = if directory.is_empty() {
                "."
            } else {
                &directory
            };
            let path = host_path(c.data_mut(), directory);
            consult(c.data_mut(), Consulted::Listing(path.clone()));
            let listing = c.data_mut().files.entries(&path);
            let plain = overlay::normal(&c.data().directory, &native_path(c.data(), directory));
            // (the overlay's files of the directory: an absent one is not listed, an overlaid one has its size, and
            // one only the overlay has is added)
            let files = c.data().overlay.in_directory(&plain);
            if listing.is_none() && files.is_empty() {
                c.data_mut().last_error = ERROR_FILE_NOT_FOUND;
                return Ok(INVALID_HANDLE_VALUE);
            }
            let mut found: Vec<Entry> = listing.unwrap_or_default();
            for (name, bytes) in files {
                let shadowed = found
                    .iter()
                    .position(|e| e.name.eq_ignore_ascii_case(&name))
                    .map(|i| found.remove(i));
                if let Some(bytes) = bytes {
                    found.push(Entry {
                        name: shadowed.as_ref().map_or(name, |e| e.name.clone()),
                        attributes: FILE_ATTRIBUTE_NORMAL,
                        size: bytes.len() as u64,
                        written: shadowed.map_or(overlay::OVERLAY_ONLY_WRITTEN, |e| e.written),
                        short: String::new(),
                    });
                }
            }
            entries.extend(
                found
                    .into_iter()
                    .filter(|e| matches(wildcard.as_bytes(), e.name.as_bytes()))
                    .map(|mut e| {
                        if e.name.len() >= 64 {
                            e.short = short_name(&path.join(&e.name));
                        }
                        e
                    }),
            );
            if matches(wildcard.as_bytes(), b".") {
                entries.insert(
                    0,
                    Entry {
                        name: ".".into(),
                        attributes: FILE_ATTRIBUTE_DIRECTORY,
                        size: 0,
                        written: FILETIME_UNIX,
                        short: String::new(),
                    },
                );
                entries.insert(
                    1,
                    Entry {
                        name: "..".into(),
                        attributes: FILE_ATTRIBUTE_DIRECTORY,
                        size: 0,
                        written: FILETIME_UNIX,
                        short: String::new(),
                    },
                );
            }
            entries[if matches(wildcard.as_bytes(), b".") {
                2
            } else {
                0
            }..]
                .sort_by_key(|e| e.name.to_uppercase());
        }
        if entries.is_empty() {
            c.data_mut().last_error = ERROR_FILE_NOT_FOUND;
            return Ok(INVALID_HANDLE_VALUE);
        }
        write_find_data(c, data, &entries[0])?;
        let handle = c.data_mut().open(Handle::Find { entries, next: 1 });
        Ok(handle)
    });
    import!(linker, "FindNextFileA", |c,
                                      handle: u32,
                                      data: u32|
     -> u32 {
        let entry = match c.data_mut().handles.get_mut(&handle) {
            Some(Handle::Find { entries, next }) => {
                let entry = entries.get(*next).cloned();
                *next += 1;
                entry
            }
            _ => return fail(c, ERROR_INVALID_HANDLE),
        };
        match entry {
            Some(entry) => {
                write_find_data(c, data, &entry)?;
                Ok(1)
            }
            None => fail(c, ERROR_NO_MORE_FILES),
        }
    });
    import!(linker, "FindClose", |c, handle: u32| -> u32 {
        Ok(u32::from(c.data_mut().handles.remove(&handle).is_some()))
    });
    persist::imports(linker)
}

/// WIN32_FIND_DATAA: attributes, three FILETIMEs, the size (high, low), two reserved words, the name and the 8.3
/// name.
/// PATH's 8.3 name, as Windows gives it (empty where it has none, or elsewhere).
#[cfg(windows)]
fn short_name(path: &Path) -> String {
    use std::os::windows::ffi::{OsStrExt, OsStringExt};
    #[link(name = "kernel32")]
    unsafe extern "system" {
        fn GetShortPathNameW(long: *const u16, short: *mut u16, size: u32) -> u32;
    }
    let wide: Vec<u16> = path.as_os_str().encode_wide().chain([0]).collect();
    let mut buffer = vec![0u16; 1024];
    // (SAFETY: WIDE ends with a NUL, and BUFFER's size is given)
    let n = unsafe { GetShortPathNameW(wide.as_ptr(), buffer.as_mut_ptr(), buffer.len() as u32) }
        as usize;
    if n == 0 || n >= buffer.len() {
        return String::new();
    }
    let short = PathBuf::from(std::ffi::OsString::from_wide(&buffer[..n]));
    match short.file_name().and_then(|f| f.to_str()) {
        Some(name)
            if name.len() <= 12 && Some(name) != path.file_name().and_then(|f| f.to_str()) =>
        {
            name.into()
        }
        _ => String::new(),
    }
}

#[cfg(not(windows))]
fn short_name(_path: &Path) -> String {
    String::new()
}

fn write_find_data(caller: &mut Caller<'_, State>, at: u32, entry: &Entry) -> Result<()> {
    let mut data = vec![0u8; 320];
    data[0..4].copy_from_slice(&entry.attributes.to_le_bytes());
    for i in 0..3 {
        data[4 + 8 * i..12 + 8 * i].copy_from_slice(&entry.written.to_le_bytes());
    }
    data[28..32].copy_from_slice(&((entry.size >> 32) as u32).to_le_bytes());
    data[32..36].copy_from_slice(&(entry.size as u32).to_le_bytes());
    let name = latin1(&entry.name);
    let n = name.len().min(259);
    data[44..44 + n].copy_from_slice(&name[..n]);
    let short = latin1(&entry.short);
    let n = short.len().min(13);
    data[304..304 + n].copy_from_slice(&short[..n]);
    write_bytes(caller, at, &data[..318])
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn command_lines_quote_as_windows_parses_them() {
        let args = [
            "-c".to_owned(),
            "a b.c".to_owned(),
            "x\\\"y".to_owned(),
            String::new(),
        ];
        assert_eq!(
            command_line("mwcceppc.exe", &args),
            "mwcceppc.exe -c \"a b.c\" \"x\\\\\\\"y\" \"\""
        );
    }

    #[test]
    fn wildcards_match_case_insensitively() {
        assert!(matches(b"*.C", b"foo.c"));
        assert!(matches(b"f?o.*", b"FOO.h"));
        assert!(!matches(b"*.c", b"foo.h"));
    }

    fn state(directory: &Path) -> State {
        State::new(&Run {
            args: &[],
            directory,
            environment: &[],
            program: "mwcceppc.exe",
            version_info: &[],
            timeout: None,
            cancel: None,
            trace: false,
            overlay: None,
            transcode_sjis: false,
            record_reads: false,
            command_line: None,
            shared_files: None,
        })
    }

    #[test]
    fn handles_reuse_closed_values() {
        let mut state = state(Path::new("."));
        let find = || Handle::Find {
            entries: vec![],
            next: 0,
        };
        let kept = state.open(find());
        for _ in 0..10_000 {
            let handle = state.open(find());
            assert!(handle < 0x8000);
            state.handles.remove(&handle);
        }
        let (a, b) = (state.open(find()), state.open(find()));
        state.handles.remove(&a);
        assert_eq!(state.open(find()), a);
        assert_eq!(
            (kept, a, b),
            (FIRST_HANDLE, FIRST_HANDLE + 4, FIRST_HANDLE + 8)
        );
    }

    /// A heap of its own memory (no module): the engine, a store and a memory of one page.
    fn heap() -> (Store<()>, Memory, Heap) {
        let engine = Engine::default();
        let mut store = Store::new(&engine, ());
        let memory = Memory::new(&mut store, wasmtime::MemoryType::new(1, None)).unwrap();
        let heap = Heap {
            top: 1 << 16,
            end: 1 << 16,
            ..Heap::default()
        };
        (store, memory, heap)
    }

    #[test]
    fn heap_reuses_and_merges_freed_blocks() {
        let (mut store, memory, mut heap) = heap();
        let base = heap.top;
        let a = heap.alloc(&memory, &mut store, 100).unwrap();
        let b = heap.alloc(&memory, &mut store, 100).unwrap();
        let c = heap.alloc(&memory, &mut store, 100).unwrap();
        assert_eq!((a, b, c), (base, base + 112, base + 224));
        // (a and b merge: a block of both sizes fits where they were)
        assert!(heap.free(a) && heap.free(b));
        assert_eq!(heap.alloc(&memory, &mut store, 200).unwrap(), a);
        // (a smaller one takes the start of a free block, and leaves the rest free)
        assert!(heap.free(a));
        assert_eq!(heap.alloc(&memory, &mut store, 10).unwrap(), a);
        assert_eq!(heap.free.get(&(a + 16)), Some(&208));
        // (freeing the last block lowers the top)
        assert!(heap.free(c) && heap.free(a));
        assert_eq!(
            (heap.top, heap.free.len(), heap.by_size.len()),
            (base, 0, 0)
        );
        assert!(!heap.free(a));
    }

    #[test]
    fn heap_grows_blocks_in_place() {
        let (mut store, memory, mut heap) = heap();
        // (a buffer grown in small steps stays where it is, at the top or into the free memory after it)
        let buffer = heap.alloc(&memory, &mut store, 16).unwrap();
        for size in (32..1 << 20).step_by(4096) {
            assert!(
                heap.grow_in_place(&memory, &mut store, buffer, size)
                    .unwrap()
            );
        }
        assert!(heap.end - heap.top < 1 << 16);
        let a = heap.alloc(&memory, &mut store, 64).unwrap();
        let b = heap.alloc(&memory, &mut store, 64).unwrap();
        let _c = heap.alloc(&memory, &mut store, 64).unwrap();
        assert!(heap.free(b));
        assert!(heap.grow_in_place(&memory, &mut store, a, 128).unwrap());
        assert!(!heap.grow_in_place(&memory, &mut store, a, 129).unwrap());
    }

    #[test]
    fn modules_load_from_the_cache() {
        let cache = std::env::temp_dir().join(format!("mwcc-wasm-cache-{}", std::process::id()));
        let _ = fs::remove_dir_all(&cache);
        let engine = engine().unwrap();
        let artifacts = || -> Vec<String> {
            fs::read_dir(&cache)
                .unwrap()
                .map(|e| e.unwrap().file_name().into_string().unwrap())
                .collect()
        };
        let empty = b"\0asm\x01\0\0\0";
        // (the first compiles it and writes its artifact; the next load it)
        assert!(!cached(&engine, empty, &cache).unwrap().1);
        let written = artifacts();
        assert_eq!(written, [artifact_name(&engine, empty)]);
        assert!(cached(&engine, empty, &cache).unwrap().1);
        assert!(cached(&engine, empty, &cache).unwrap().1);
        // (an artifact that does not load is compiled again, and replaced)
        fs::write(cache.join(&written[0]), b"not an artifact").unwrap();
        assert!(!cached(&engine, empty, &cache).unwrap().1);
        assert!(cached(&engine, empty, &cache).unwrap().1);
        // (another build of the module replaces it)
        let named = b"\0asm\x01\0\0\0\0\x02\x01x";
        assert!(!cached(&engine, named, &cache).unwrap().1);
        assert_eq!(artifacts(), [artifact_name(&engine, named)]);
        assert!(Compiler::new_cached(named, &cache).is_ok());
        // (and so do the temporary files of writes that did not finish, once they are old)
        let (old, new) = (cache.join("a.1-0.tmp"), cache.join("a.1-1.tmp"));
        fs::write(&old, "").unwrap();
        fs::File::options()
            .write(true)
            .open(&old)
            .unwrap()
            .set_modified(SystemTime::now() - Duration::from_secs(3600))
            .unwrap();
        fs::write(&new, "").unwrap();
        assert!(!cached(&engine, empty, &cache).unwrap().1);
        assert!(!old.exists() && new.exists());
        // (a cache that cannot be written still compiles)
        let file = cache.join("file");
        fs::write(&file, "").unwrap();
        assert!(!cached(&engine, empty, &file).unwrap().1);
        fs::remove_dir_all(&cache).unwrap();
    }

    #[test]
    fn dates_round_trip() {
        for days in [-1, 0, 11_016, 20_000, 30_000] {
            let (y, m, _, d) = civil(days);
            assert_eq!(days_from_civil(y, m, d), days);
        }
        assert_eq!(civil(0), (1970, 1, 4, 1));
    }
}
