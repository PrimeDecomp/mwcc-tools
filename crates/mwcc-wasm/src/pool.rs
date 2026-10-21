//! Persistent compiles shared by many callers ([`PersistPool`]): each candidate of a source is compiled after its
//! context (the text before the candidate's first difference from a reference, [`split_point`]) by a persistent
//! compiler that parsed that context once, and the caller's normal compile is the reference for anything the pool
//! does not trust. Ported from mwdec's fast path (demwcc-echoes' `fast.rs`, CC0 1.0): routing by context, a start
//! only after a few requests, LRU snapshots, and trusting failures that carry a compiler diagnostic.
//!
//! - **Routing:** a request goes to an idle persistent compiler of its context. A context gets its first one in the
//!   background from its [`PoolOptions::start_after`]th request on, while requests compile normally; a context in
//!   demand gets more, up to its share of [`PoolOptions::max_live`] among the contexts in use. A request waits for a
//!   busy compiler of its context rather than compiling normally.
//! - **Snapshots:** at most `max_live` persistent compilers (each a snapshot and an idle module) exist at once; a start
//!   drops the least recently used idle one of another context when there is no room.
//! - **Failures:** a candidate that fails with a compiler diagnostic in its console is reported as it is (its messages
//!   equal a normal compile's), except every [`PoolOptions::recheck_every`]th, and with `confirm_failures`, which are
//!   compiled normally. A trap, a timeout or a failure without a diagnostic is compiled normally. When the normal
//!   compile accepts what the persistent one rejected, that persistent compiler is dropped and counted
//!   ([`PoolStats::mismatches`]).
//! - `MWCC_PERSIST_LOG` in the environment: why a start or a persistent compile failed, on stderr.
use super::{Loaded, Overlay, Persistent, Request, persistent_source};
use std::{
    collections::{HashMap, HashSet},
    hash::{Hash, Hasher},
    path::PathBuf,
    sync::{Arc, Condvar, Mutex, MutexGuard},
    time::{Duration, Instant},
};

/// What a persistent compiler is started on: a unit's compile, with its source's text before the candidates.
pub struct Context {
    pub module: Arc<Loaded>,
    /// The compiler's arguments, as a normal compile of the unit has them.
    pub args: Vec<String>,
    /// The argument naming the object (left out of the context's identity: a persistent compile never writes it).
    pub output: String,
    pub directory: PathBuf,
    pub environment: Vec<(String, String)>,
    pub program: String,
    /// The source the arguments compile (absolute), and its text before the candidates (ending with a line end).
    pub source: PathBuf,
    pub text: String,
    /// The other files read in place of the disk's.
    pub overlay: Overlay,
    pub transcode_sjis: bool,
}

/// A compile's result, persistent or normal.
#[derive(Clone, Debug, Default)]
pub struct Compiled {
    pub exit_code: i32,
    pub object: Option<Vec<u8>>,
    /// Its messages, as a normal compile gives them.
    pub console: Vec<u8>,
    pub timed_out: bool,
    pub cancelled: bool,
    /// A persistent compiler gave it.
    pub fast: bool,
}

#[derive(Clone, Copy, Debug)]
pub struct PoolOptions {
    /// Persistent compilers alive at once.
    pub max_live: usize,
    /// A context's request that starts its first persistent compiler.
    pub start_after: u32,
    /// Every this many trusted failures, one is compiled normally.
    pub recheck_every: u32,
    /// Compile every candidate normally too, and compare (a check of the pool).
    pub verify: bool,
    /// How long a start may take.
    pub start_timeout: Duration,
    /// A persistent compiler idle this long is dropped (each holds its snapshot and its module's memory: tens of
    /// MB).
    pub idle_timeout: Option<Duration>,
}

impl Default for PoolOptions {
    fn default() -> Self {
        Self {
            max_live: 8,
            start_after: 3,
            recheck_every: 8,
            verify: false,
            start_timeout: Duration::from_secs(300),
            idle_timeout: Some(Duration::from_secs(120)),
        }
    }
}

/// What a pool did.
#[derive(Clone, Copy, Debug, Default)]
pub struct PoolStats {
    /// Candidates a persistent compiler compiled (and the time it took), and failures reported from one.
    pub fast: u64,
    pub fast_ms: f64,
    pub trusted_failures: u64,
    /// Persistent failures compiled normally (rechecks, `confirm_failures`, no diagnostic, traps and timeouts), and
    /// of those, the ones the normal compile accepted.
    pub rechecked: u64,
    pub mismatches: u64,
    /// With [`PoolOptions::verify`], candidates compared with a normal compile.
    pub verified: u64,
    /// Candidates compiled normally as no persistent compiler of their context was ready.
    pub not_ready: u64,
    /// Persistent compilers started (and the time it took), and starts that failed (their context compiles
    /// normally from then on: its text does not end at the top level, say).
    pub starts: u64,
    pub start_ms: f64,
    pub start_failures: u64,
    /// Persistent compilers alive now, and at most.
    pub live: usize,
    pub live_max: usize,
}

/// A context's requests count it as in use for this long (its share of the persistent compilers).
const ACTIVE: Duration = Duration::from_secs(5);
/// A request waits at most this long for a busy persistent compiler of its context.
const MAX_WAIT: Duration = Duration::from_secs(20);

struct Instance {
    key: u64,
    persistent: Persistent,
    used: Instant,
}

#[derive(Default)]
struct State {
    idle: Vec<Instance>,
    /// Per context: persistent compilers checked out, and starting.
    busy: HashMap<u64, usize>,
    starting: HashMap<u64, usize>,
    /// Contexts whose start failed.
    failed: HashSet<u64>,
    requests: HashMap<u64, u32>,
    last_use: HashMap<u64, Instant>,
    /// Idle, busy and starting.
    live: usize,
    /// Trusted failures since the last recheck.
    unchecked: u32,
    stats: PoolStats,
}

struct Shared {
    options: PoolOptions,
    state: Mutex<State>,
    changed: Condvar,
}

/// Persistent compilers for many contexts (see the module's documentation).
pub struct PersistPool {
    shared: Arc<Shared>,
}

/// A slot of CAPACITY bytes that holds LEN: 64 KiB, doubled until it does.
fn capacity(len: usize) -> usize {
    let mut capacity = 64 << 10;
    while capacity < len {
        capacity *= 2;
    }
    capacity
}

/// Whether `MWCC_PERSIST_LOG` is in the environment: why a start or a persistent compile failed is written to stderr.
pub fn persist_log() -> bool {
    static LOG: std::sync::OnceLock<bool> = std::sync::OnceLock::new();
    *LOG.get_or_init(|| std::env::var_os("MWCC_PERSIST_LOG").is_some())
}

fn key(context: &Context, capacity: usize) -> u64 {
    let mut h = std::collections::hash_map::DefaultHasher::new();
    (Arc::as_ptr(&context.module) as usize).hash(&mut h);
    for arg in &context.args {
        if *arg == context.output {
            "<output>".hash(&mut h);
        } else {
            arg.hash(&mut h);
        }
    }
    context.directory.hash(&mut h);
    let mut environment: Vec<_> = context.environment.iter().collect();
    environment.sort();
    environment.hash(&mut h);
    context.program.hash(&mut h);
    context
        .overlay
        .fingerprint_without(Some(&context.source))
        .hash(&mut h);
    super::overlay::key(&context.source).hash(&mut h);
    context.text.hash(&mut h);
    (context.transcode_sjis, capacity).hash(&mut h);
    h.finish()
}

/// A console holding a compiler's error (CError's report: `#   Error:` under the offending line).
fn has_diagnostic(console: &[u8]) -> bool {
    console.windows(10).any(|w| w == b"#   Error:")
}

/// A persistent compiler checked out of the pool: back in it when dropped, unless it ended or is retired.
struct Lease<'a> {
    shared: &'a Shared,
    instance: Option<Instance>,
    retire: bool,
}

impl Drop for Lease<'_> {
    fn drop(&mut self) {
        let Some(mut instance) = self.instance.take() else {
            return;
        };
        let mut st = self.shared.state.lock().unwrap();
        if let Some(n) = st.busy.get_mut(&instance.key) {
            *n -= 1;
        }
        if self.retire || !instance.persistent.alive() {
            st.live -= 1;
            drop(st);
            drop(instance);
        } else {
            instance.used = Instant::now();
            st.idle.push(instance);
            drop(st);
        }
        self.shared.changed.notify_all();
    }
}

impl PersistPool {
    pub fn new(options: PoolOptions) -> Self {
        let shared = Arc::new(Shared {
            options: PoolOptions {
                max_live: options.max_live.max(1),
                recheck_every: options.recheck_every.max(1),
                ..options
            },
            state: Mutex::new(State::default()),
            changed: Condvar::new(),
        });
        // (the idle ones dropped as they expire, until the pool is)
        if let Some(idle) = options.idle_timeout {
            let weak = Arc::downgrade(&shared);
            let _ = std::thread::Builder::new()
                .name("mwcc-pool-idle".into())
                .spawn(move || {
                    loop {
                        std::thread::sleep(
                            (idle / 4).clamp(Duration::from_millis(10), Duration::from_secs(30)),
                        );
                        let Some(shared) = weak.upgrade() else { return };
                        let mut st = shared.state.lock().unwrap();
                        let (expired, kept): (Vec<_>, Vec<_>) = std::mem::take(&mut st.idle)
                            .into_iter()
                            .partition(|i| i.used.elapsed() >= idle);
                        st.idle = kept;
                        st.live -= expired.len();
                        drop(st);
                        drop(expired);
                    }
                });
        }
        Self { shared }
    }

    pub fn stats(&self) -> PoolStats {
        let st = self.shared.state.lock().unwrap();
        PoolStats {
            live: st.live,
            ..st.stats
        }
    }

    /// CANDIDATE (the source's text after CONTEXT's) compiled in TIMEOUT: by a persistent compiler of CONTEXT where
    /// one is ready and its result is trusted, else by NORMAL (a normal compile of the context and the candidate,
    /// which the caller does). CONFIRM_FAILURES: a persistent failure is always compiled normally.
    pub fn compile<E>(
        &self,
        context: &Arc<Context>,
        candidate: &[u8],
        timeout: Duration,
        confirm_failures: bool,
        normal: &dyn Fn() -> Result<Compiled, E>,
    ) -> Result<Compiled, E> {
        let shared = &*self.shared;
        let capacity = capacity(candidate.len());
        let key = key(context, capacity);
        let Some(instance) = self.checkout(context, key, capacity) else {
            shared.state.lock().unwrap().stats.not_ready += 1;
            return normal();
        };
        let mut lease = Lease {
            shared,
            instance: Some(instance),
            retire: false,
        };
        let started = Instant::now();
        let compiled = lease
            .instance
            .as_mut()
            .unwrap()
            .persistent
            .compile(candidate, Some(timeout));
        let ms = started.elapsed().as_secs_f64() * 1000.0;
        let fast = match compiled {
            Ok(c) => Compiled {
                exit_code: c.exit_code,
                object: c.object,
                console: c.console,
                fast: true,
                ..Compiled::default()
            },
            // (a trap or a timeout: the persistent compiler has ended)
            Err(e) => {
                if persist_log() {
                    eprintln!("mwcc-wasm: a persistent compile failed: {e:?}");
                }
                drop(lease);
                shared.state.lock().unwrap().stats.rechecked += 1;
                return normal();
            }
        };
        let options = shared.options;
        let succeeded = fast.exit_code == 0 && fast.object.is_some();
        if options.verify {
            let checked = normal()?;
            let mut st = shared.state.lock().unwrap();
            st.stats.verified += 1;
            if (checked.exit_code, &checked.object) != (fast.exit_code, &fast.object) {
                st.stats.mismatches += 1;
                lease.retire = true;
            }
            return Ok(checked);
        }
        if succeeded {
            let mut st = shared.state.lock().unwrap();
            st.stats.fast += 1;
            st.stats.fast_ms += ms;
            return Ok(fast);
        }
        let trusted = !confirm_failures
            && matches!(fast.exit_code, 1 | 2)
            && has_diagnostic(&fast.console)
            && {
                let mut st = shared.state.lock().unwrap();
                st.unchecked += 1;
                if st.unchecked >= options.recheck_every {
                    st.unchecked = 0;
                    false
                } else {
                    st.stats.fast += 1;
                    st.stats.fast_ms += ms;
                    st.stats.trusted_failures += 1;
                    true
                }
            };
        if trusted {
            return Ok(fast);
        }
        let checked = normal()?;
        let mut st = shared.state.lock().unwrap();
        st.stats.rechecked += 1;
        if checked.exit_code == 0 && checked.object.is_some() {
            st.stats.mismatches += 1;
            lease.retire = true;
        }
        Ok(checked)
    }

    /// Start persistent compilers of CONTEXT (for candidates of up to 64 KiB) until N of them exist, WAIT has passed
    /// or a start failed, before a loop of candidates: how many exist.
    pub fn warm(&self, context: &Arc<Context>, n: usize, wait: Duration) -> usize {
        let shared = &*self.shared;
        let capacity = capacity(0);
        let key = key(context, capacity);
        let deadline = Instant::now() + wait;
        let n = n.clamp(1, shared.options.max_live);
        let mut st = shared.state.lock().unwrap();
        loop {
            let holders = st.idle.iter().filter(|i| i.key == key).count()
                + st.busy.get(&key).copied().unwrap_or(0);
            if holders >= n || st.failed.contains(&key) || Instant::now() >= deadline {
                return holders;
            }
            st.last_use.insert(key, Instant::now());
            let starting = st.starting.get(&key).copied().unwrap_or(0);
            if holders + starting < n {
                let live = st.live;
                st = self.start(st, context, key, capacity);
                if st.live != live {
                    continue;
                }
            }
            st = shared
                .changed
                .wait_timeout(st, Duration::from_millis(100))
                .unwrap()
                .0;
        }
    }

    /// An idle persistent compiler of KEY (waiting for a busy one), or None: compile normally (a start of one may be
    /// under way).
    fn checkout(&self, context: &Arc<Context>, key: u64, capacity: usize) -> Option<Instance> {
        let shared = &*self.shared;
        let mut st = shared.state.lock().unwrap();
        if st.failed.contains(&key) {
            return None;
        }
        let now = Instant::now();
        let deadline = now + MAX_WAIT;
        st.last_use.insert(key, now);
        if st.last_use.len() > 64 {
            st.last_use.retain(|_, t| now.duration_since(*t) < ACTIVE);
        }
        if st.requests.len() > 4096 {
            st.requests.clear();
        }
        let requests = {
            let n = st.requests.entry(key).or_default();
            *n = n.saturating_add(1);
            *n
        };
        loop {
            if let Some(at) = st.idle.iter().position(|i| i.key == key) {
                let instance = st.idle.remove(at);
                *st.busy.entry(key).or_default() += 1;
                return Some(instance);
            }
            let busy = st.busy.get(&key).copied().unwrap_or(0);
            let starting = st.starting.get(&key).copied().unwrap_or(0);
            let active = st
                .last_use
                .values()
                .filter(|t| now.duration_since(**t) < ACTIVE)
                .count()
                .max(1);
            let share = shared.options.max_live.div_ceil(active).max(1);
            if starting == 0
                && busy + starting < share
                && (requests >= shared.options.start_after || busy > 0)
            {
                st = self.start(st, context, key, capacity);
            }
            if busy == 0 || Instant::now() >= deadline {
                return None;
            }
            st = shared
                .changed
                .wait_timeout(st, Duration::from_millis(250))
                .unwrap()
                .0;
            if st.failed.contains(&key) {
                return None;
            }
        }
    }

    /// Start a persistent compiler of CONTEXT in the background, if there is room (made by dropping the least
    /// recently used idle one of another context).
    fn start<'a>(
        &self,
        mut st: MutexGuard<'a, State>,
        context: &Arc<Context>,
        key: u64,
        capacity: usize,
    ) -> MutexGuard<'a, State> {
        if st.live >= self.shared.options.max_live {
            let Some(at) = st
                .idle
                .iter()
                .enumerate()
                .filter(|(_, i)| i.key != key)
                .min_by_key(|(_, i)| i.used)
                .map(|(at, _)| at)
            else {
                return st;
            };
            let evicted = st.idle.remove(at);
            st.live -= 1;
            // (its run's thread ends as it is dropped)
            std::thread::spawn(move || drop(evicted));
        }
        st.live += 1;
        st.stats.live_max = st.stats.live_max.max(st.live);
        *st.starting.entry(key).or_default() += 1;
        let shared = self.shared.clone();
        let context = context.clone();
        let spawned = std::thread::Builder::new()
            .name("mwcc-pool-start".into())
            .spawn(move || {
                let started = Instant::now();
                let mut overlay = context.overlay.clone();
                overlay.insert(&context.source, persistent_source(&context.text, capacity));
                let persistent = context.module.compiler.persistent(
                    &Request {
                        args: &context.args,
                        directory: &context.directory,
                        environment: &context.environment,
                        program: &context.program,
                        version_info: &context.module.version_info,
                        timeout: Some(shared.options.start_timeout),
                        cancel: None,
                        trace: false,
                        overlay: Some(&overlay),
                        transcode_sjis: context.transcode_sjis,
                        record_reads: false,
                    },
                    capacity,
                );
                let mut st = shared.state.lock().unwrap();
                if let Some(n) = st.starting.get_mut(&key) {
                    *n -= 1;
                }
                match persistent {
                    Ok(persistent) => {
                        st.stats.starts += 1;
                        st.stats.start_ms += started.elapsed().as_secs_f64() * 1000.0;
                        st.idle.push(Instance {
                            key,
                            persistent,
                            used: Instant::now(),
                        });
                    }
                    Err(e) => {
                        if persist_log() {
                            eprintln!(
                                "mwcc-wasm: a persistent compile of {} did not start: {e:?}",
                                context.source.display()
                            );
                        }
                        st.stats.start_failures += 1;
                        st.live -= 1;
                        if st.failed.len() > 1024 {
                            st.failed.clear();
                        }
                        st.failed.insert(key);
                    }
                }
                drop(st);
                shared.changed.notify_all();
            });
        if spawned.is_err() {
            st.live -= 1;
            if let Some(n) = st.starting.get_mut(&key) {
                *n -= 1;
            }
        }
        st
    }
}

/// Where to cut a candidate source into the context of a persistent compile and the text after it: the start of the
/// last line at or before its first difference from REFERENCE (another candidate of the same source, the unmodified
/// one, say) that begins a top-level declaration, as far as a scan of the text can tell: outside braces,
/// parentheses, comments and literals, after a `;` or `}` (or a preprocessor line), and not itself a closing brace.
/// A context that does not end at the top level only fails to start; the candidates then compile normally. 0: no
/// such line.
pub fn split_point(reference: &[u8], candidate: &[u8]) -> usize {
    // (where it differs, a `\r\n` line end and a `\n` one being the same)
    let differs = {
        let (mut i, mut j) = (0, 0);
        loop {
            if candidate.get(i) == Some(&b'\r') && candidate.get(i + 1) == Some(&b'\n') {
                i += 1;
            }
            if reference.get(j) == Some(&b'\r') && reference.get(j + 1) == Some(&b'\n') {
                j += 1;
            }
            match (candidate.get(i), reference.get(j)) {
                (Some(a), Some(b)) if a == b => (i, j) = (i + 1, j + 1),
                _ => break i,
            }
        }
    };
    let (mut depth, mut parens) = (0i32, 0i32);
    let mut best = 0;
    // (the last significant character before the line: a declaration ended)
    let mut ended = true;
    let mut i = 0;
    let mut line_start = true;
    while i < candidate.len() && i <= differs {
        let c = candidate[i];
        if line_start {
            line_start = false;
            let first = candidate[i..]
                .iter()
                .position(|b| !matches!(b, b' ' | b'\t'))
                .map(|k| candidate[i + k]);
            if depth == 0
                && parens == 0
                && ended
                && i > 0
                && !matches!(first, Some(b' ' | b'\t' | b'}' | b'\r' | b'\n' | b')'))
                && !matches!(candidate[i], b' ' | b'\t')
            {
                best = i;
            }
            if first == Some(b'#') {
                // (a preprocessor line, with its continuations: a declaration ends with it)
                while i < candidate.len() && candidate[i] != b'\n' {
                    if candidate[i] == b'\\' && candidate.get(i + 1) == Some(&b'\n') {
                        i += 1;
                    }
                    i += 1;
                }
                ended = true;
                line_start = true;
                i += 1;
                continue;
            }
        }
        match c {
            b'\n' => line_start = true,
            b'/' if candidate.get(i + 1) == Some(&b'/') => {
                while i < candidate.len() && candidate[i] != b'\n' {
                    i += 1;
                }
                continue;
            }
            b'/' if candidate.get(i + 1) == Some(&b'*') => {
                i += 2;
                while i + 1 < candidate.len() && !(candidate[i] == b'*' && candidate[i + 1] == b'/')
                {
                    i += 1;
                }
                i += 2;
                continue;
            }
            b'"' | b'\'' => {
                i += 1;
                while i < candidate.len() && candidate[i] != c && candidate[i] != b'\n' {
                    if candidate[i] == b'\\' {
                        i += 1;
                    }
                    i += 1;
                }
                ended = false;
            }
            b'{' => {
                depth += 1;
                ended = false;
            }
            b'}' => {
                depth -= 1;
                ended = true;
            }
            b'(' => {
                parens += 1;
                ended = false;
            }
            b')' => {
                parens -= 1;
                ended = false;
            }
            b';' => ended = true,
            b' ' | b'\t' | b'\r' => {}
            _ => ended = false,
        }
        i += 1;
    }
    best
}

#[cfg(test)]
mod tests {
    use super::split_point;

    #[test]
    fn splits_before_the_changed_declaration() {
        let reference = b"#include \"a.h\"\nint g;\nint f(void) {\n    return 1;\n}\nint h;\n";
        let candidate = b"#include \"a.h\"\nint g;\nint f(void) {\n    return 2;\n}\nint h;\n";
        assert_eq!(split_point(reference, candidate), 22);
        assert_eq!(&candidate[..22], b"#include \"a.h\"\nint g;\n");
    }

    #[test]
    fn never_inside_braces_comments_or_a_continued_declaration() {
        let reference =
            b"struct S {\n    int a;\n};\n/* x;\n}\nint q; */\nint\nf(void) { return 0; }\n";
        let mut candidate = reference.to_vec();
        let at = candidate.len() - 4;
        candidate[at] = b'1';
        let split = split_point(reference, &candidate);
        assert_eq!(&candidate[split..split + 4], b"int\n");
        // (before the first declaration: nothing to keep)
        assert_eq!(split_point(b"int a;\n", b"int b;\n"), 0);
    }

    #[test]
    fn line_ends_do_not_count_as_a_difference() {
        let reference = b"int a;\r\nint b;\r\nint f(void) { return 1; }\r\n";
        let candidate = b"int a;\nint b;\nint f(void) { return 2; }\n";
        assert_eq!(
            &candidate[..split_point(reference, candidate)],
            b"int a;\nint b;\n"
        );
    }

    #[test]
    fn a_namespace_is_not_split() {
        let reference = b"int a;\nnamespace n {\nint b;\nint c;\n}\n";
        let candidate = b"int a;\nnamespace n {\nint b;\nint d;\n}\n";
        assert_eq!(split_point(reference, candidate), 7);
    }
}
