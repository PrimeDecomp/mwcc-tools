//! Parse once, compile many: a persistent run of the compiler built with `MWCC_PERSIST` (`persist.wasm`).
//!
//! The source is a context (the headers and declarations every candidate shares), the marker line
//! `#define MWCC_PERSIST_MARKER 1` and a slot of `capacity` bytes, whose lines are numbered as if it followed the
//! context. The compiler parses the context once. At the marker it calls `persist_checkpoint`, and the host snapshots the module: its memory, its stack pointer (its one mutable
//! global) and what the host keeps of the run (the heap of GlobalAlloc, TLS slots, last error, the console). Each
//! candidate is then written into the slot of the source's text in memory, compiled from there, and handed back by
//! `persist_done` (exit code, object, messages); the next `persist_checkpoint` restores the snapshot.
//!
//! The module's call stack is wasmtime's, on the host's stack: no copy of the memory brings frames back. gc2 keeps
//! the frames live at the checkpoint live and unchanged instead. The checkpoint is in `C_Compiler` (gc2's CPrep.c),
//! the one frame live both at the marker and after the object is built: the marker unwinds the parser to it, and
//! each candidate ends by jumping back to it, never by returning through it. The driver's tail (writing the object,
//! exiting) never runs for a candidate.
//!
//! The run lives on a thread of its own, which owns the store; the imports block on channels between candidates.
use super::{Compiler, Handle, Heap, Request, State, Stopped, memory, read_bytes};
use std::{
    path::{Path, PathBuf},
    sync::mpsc::{Receiver, Sender, channel},
    thread::JoinHandle,
    time::{Duration, Instant},
};
use wasmtime::{Caller, Extern, Linker, Result, UpdateDeadline, bail, format_err};

/// The macro whose `#define` in the main file, at the top level, is the checkpoint.
pub const MARKER: &str = "MWCC_PERSIST_MARKER";
const SENTINEL: &str = ";";

/// The source of a persistent compile: CONTEXT, the marker line and a slot of CAPACITY bytes (`\n` line ends).
pub fn persistent_source(context: &str, capacity: usize) -> Vec<u8> {
    let mut out = context.replace("\r\n", "\n").into_bytes();
    if !out.ends_with(b"\n") {
        out.push(b'\n');
    }
    // (a declaration of its own before the marker: after some declarations, a function's definition among them, the
    // parser reads the next token ahead, and the marker is taken only by the top level's lex())
    // (these two lines are taken off the line counter at the marker, gc2's CPrep_PersistMarker: the slot's lines are
    // numbered as if it followed the context, so `-g` and `-sym on` objects and the messages equal a normal compile's)
    out.extend(format!("{SENTINEL}\n#define {MARKER} 1\n").bytes());
    out.extend(slot_text(b"", capacity).unwrap());
    out
}

/// The slot holding CANDIDATE, as the source has it (`\n` line ends): the candidate, then spaces with a line end
/// every 128 bytes. A compile of the persistent source with this slot is what a persistent compile of CANDIDATE gives.
/// None: it does not fit.
pub fn slot_text(candidate: &[u8], capacity: usize) -> Option<Vec<u8>> {
    let mut out: Vec<u8> = Vec::with_capacity(capacity);
    let mut i = 0;
    while i < candidate.len() {
        if candidate[i] == b'\r' && candidate.get(i + 1) == Some(&b'\n') {
            i += 1;
            continue;
        }
        out.push(if candidate[i] == b'\r' {
            b'\n'
        } else {
            candidate[i]
        });
        i += 1;
    }
    if out.len() > capacity {
        return None;
    }
    // (a line end after the candidate: its last line is not continued by the padding)
    for k in out.len()..capacity {
        out.push(if k % 128 == 127 || k + 1 == capacity {
            b'\n'
        } else {
            b' '
        });
    }
    Some(out)
}

/// A candidate's result.
pub struct Candidate {
    /// The status the executable exits with: 0 compiled, 1 failed, 2 cancelled (a user break: too many errors).
    pub exit_code: i32,
    /// The object, when it compiled.
    pub object: Option<Vec<u8>>,
    /// The messages written while it compiled (stdout and stderr, in order), ending as the driver ends a failure.
    pub console: Vec<u8>,
    /// The restore of the snapshot before it (done while the caller was busy).
    pub restore: Duration,
    /// The memory above the snapshot that the restore zeroed (what the previous candidate's heap grew into).
    pub zeroed: usize,
    /// From its slot written to its object handed back.
    pub compile: Duration,
    /// The oracle's trace records written while it compiled (the run traces: [`Request::trace`]).
    pub trace: Vec<u8>,
}

enum Job {
    /// A candidate's slot, its timeout, and whether it traces.
    Compile(Vec<u8>, Option<Duration>, bool),
    Stop,
}

enum Event {
    Ready {
        snapshot_bytes: usize,
    },
    Done(Candidate),
    /// The run ended (exited, trapped or timed out): its console.
    Ended(String, Vec<u8>),
}

/// A persistent compile, ready for candidates.
pub struct Persistent {
    jobs: Sender<Job>,
    events: Receiver<Event>,
    thread: Option<JoinHandle<()>>,
    capacity: usize,
    dead: bool,
    /// Whether candidates trace unless asked otherwise (the run's [`Request::trace`]).
    trace: bool,
    /// Whether candidates are read as sjiswrap gives them (the run's [`Request::transcode_sjis`]).
    transcode_sjis: bool,
    /// The memory copied by the snapshot.
    pub snapshot_bytes: usize,
    /// From the start to the snapshot.
    pub start_time: Duration,
}

/// What the store's State keeps of a persistent run.
pub(crate) struct Link {
    jobs: Receiver<Job>,
    events: Sender<Event>,
    capacity: usize,
    snapshot: Option<Snapshot>,
    /// Where the slot starts in memory, and the line end the compiler's text has there.
    slot: u32,
    line_end: u8,
    /// The current candidate's start and timeout.
    started: Instant,
    timeout: Option<Duration>,
    restore: Duration,
    zeroed: usize,
}

struct Snapshot {
    memory: Vec<u8>,
    stack_pointer: i32,
    heap: Heap,
    last_error: u32,
    tls: Vec<u32>,
    command_line_at: Option<u32>,
    handles: Vec<(u32, Vec<super::Entry>, usize)>,
    stdout: usize,
    stderr: usize,
    console: usize,
    trace: usize,
    ticks: Duration,
}

/// The owned parts of a Request, for the run's thread.
struct Owned {
    args: Vec<String>,
    directory: PathBuf,
    environment: Vec<(String, String)>,
    program: String,
    version_info: Vec<u8>,
    trace: bool,
    overlay: Option<super::Overlay>,
    transcode_sjis: bool,
}

impl Compiler {
    /// Start a persistent compile of REQUEST (a compile of a source made by [`persistent_source`] with CAPACITY), and
    /// return once the context is parsed and snapshotted. TIMEOUT bounds the start.
    pub fn persistent(&self, request: &Request, capacity: usize) -> Result<Persistent> {
        let started = Instant::now();
        let owned = Owned {
            args: request.args.to_vec(),
            directory: request.directory.to_path_buf(),
            environment: request.environment.to_vec(),
            program: request.program.to_owned(),
            version_info: request.version_info.to_vec(),
            trace: request.trace,
            overlay: request.overlay.cloned(),
            transcode_sjis: request.transcode_sjis,
        };
        let timeout = request.timeout;
        let compiler = self.clone();
        let (jobs, job_rx) = channel();
        let (event_tx, events) = channel();
        let thread = std::thread::Builder::new()
            .stack_size(64 << 20)
            .name("mwcc-persist".into())
            .spawn(move || {
                let request = Request {
                    args: &owned.args,
                    directory: &owned.directory,
                    environment: &owned.environment,
                    program: &owned.program,
                    version_info: &owned.version_info,
                    timeout,
                    cancel: None,
                    trace: owned.trace,
                    overlay: owned.overlay.as_ref(),
                    transcode_sjis: owned.transcode_sjis,
                    record_reads: false,
                    command_line: None,
                    shared_files: None,
                };
                let mut state = State::new(&request);
                state.persist = Some(Link {
                    jobs: job_rx,
                    events: event_tx.clone(),
                    capacity,
                    snapshot: None,
                    slot: 0,
                    line_end: b'\n',
                    started: Instant::now(),
                    timeout,
                    restore: Duration::ZERO,
                    zeroed: 0,
                });
                let mut store = super::limited_store(&compiler.engine, state);
                store.set_epoch_deadline(1);
                store.epoch_deadline_callback(|context| {
                    if let Some(link) = &context.data().persist
                        && link.timeout.is_some_and(|t| link.started.elapsed() >= t)
                    {
                        return Err(Stopped::TimedOut.into());
                    }
                    Ok(UpdateDeadline::Continue(1))
                });
                let result = compiler
                    .instantiate(&mut store)
                    .and_then(|start| start.call(&mut store, ()));
                let why = match result {
                    Ok(()) => "the compiler returned".to_owned(),
                    Err(e) => format!("{e:?}"),
                };
                let _ = event_tx.send(Event::Ended(why, store.into_data().console));
            })?;
        let mut persistent = Persistent {
            jobs,
            events,
            thread: Some(thread),
            capacity,
            dead: false,
            trace: request.trace,
            transcode_sjis: request.transcode_sjis,
            snapshot_bytes: 0,
            start_time: Duration::ZERO,
        };
        match persistent.events.recv() {
            Ok(Event::Ready { snapshot_bytes }) => {
                persistent.snapshot_bytes = snapshot_bytes;
                persistent.start_time = started.elapsed();
                Ok(persistent)
            }
            Ok(Event::Ended(why, console)) => {
                persistent.dead = true;
                bail!(
                    "the compile ended before the marker ({why}): {}",
                    String::from_utf8_lossy(&console)
                )
            }
            _ => bail!("the persistent compiler's thread ended"),
        }
    }
}

impl Persistent {
    /// Compile CANDIDATE (the text after the context) in TIMEOUT; it traces as the run does.
    pub fn compile(&mut self, candidate: &[u8], timeout: Option<Duration>) -> Result<Candidate> {
        self.compile_traced(candidate, timeout, self.trace)
    }

    /// [`Persistent::compile`], tracing or not (TRACE): the oracle's flag is set after the restore. A run started
    /// untraced has no frontend records for its context, but its backend streams (those its environment names) trace
    /// a candidate.
    pub fn compile_traced(
        &mut self,
        candidate: &[u8],
        timeout: Option<Duration>,
        trace: bool,
    ) -> Result<Candidate> {
        if self.dead {
            bail!("the persistent compiler has ended");
        }
        // (the slot is written as the source's text: transcoded as the run reads its source, its warning before the
        // candidate's messages, and a candidate that is not UTF-8 failing as sjiswrap fails)
        let (encoded, warning);
        let candidate = if self.transcode_sjis {
            match super::overlay::transcode(Path::new("the candidate"), candidate) {
                Ok((text, message)) => {
                    (encoded, warning) = (text, message);
                    &encoded[..]
                }
                Err(message) => {
                    return Ok(Candidate {
                        exit_code: 1,
                        object: None,
                        console: format!(
                            "{message}
"
                        )
                        .into_bytes(),
                        restore: Duration::ZERO,
                        zeroed: 0,
                        compile: Duration::ZERO,
                        trace: vec![],
                    });
                }
            }
        } else {
            warning = None;
            candidate
        };
        let Some(slot) = slot_text(candidate, self.capacity) else {
            bail!(
                "the candidate ({} bytes) does not fit the slot ({} bytes)",
                candidate.len(),
                self.capacity
            );
        };
        if self.jobs.send(Job::Compile(slot, timeout, trace)).is_err() {
            self.dead = true;
            bail!("the persistent compiler's thread ended");
        }
        match self.events.recv() {
            Ok(Event::Done(mut candidate)) => {
                if let Some(warning) = warning {
                    candidate.console.splice(0..0, warning.into_bytes());
                }
                Ok(candidate)
            }
            Ok(Event::Ended(why, console)) => {
                self.dead = true;
                bail!(
                    "the persistent compiler ended ({why}): {}",
                    String::from_utf8_lossy(&console)
                )
            }
            _ => {
                self.dead = true;
                bail!("the persistent compiler's thread ended")
            }
        }
    }

    /// Whether it can still compile.
    pub fn alive(&self) -> bool {
        !self.dead
    }
}

// (a Persistent may move between threads: it holds its run's channels and thread)
const _: fn() = || {
    fn send<T: Send>() {}
    send::<Persistent>();
};

impl Drop for Persistent {
    fn drop(&mut self) {
        let _ = self.jobs.send(Job::Stop);
        if let Some(thread) = self.thread.take() {
            let _ = thread.join();
        }
    }
}

fn stack_pointer(caller: &mut Caller<'_, State>) -> Result<i32> {
    match caller.get_export("__stack_pointer") {
        Some(Extern::Global(g)) => Ok(g.get(&mut *caller).unwrap_i32()),
        _ => bail!("the module exports no __stack_pointer (not a persist.wasm?)"),
    }
}

/// The oracle's flag (`OracleTrace_On`, wasm/src/oracle.c), which the combined module exports, and the host's: whether
/// the candidate traces. A module without the flag (no hooks) only has the host's.
fn set_tracing(caller: &mut Caller<'_, State>, trace: bool) -> Result<()> {
    caller.data_mut().tracing = trace;
    if let Some(Extern::Global(g)) = caller.get_export("OracleTrace_On") {
        let at = g.get(&mut *caller).unwrap_i32() as u32;
        let m = memory(caller)?;
        m.write(&mut *caller, at as usize, &i32::from(trace).to_le_bytes())?;
    }
    Ok(())
}

fn take(caller: &mut Caller<'_, State>) -> Result<Link> {
    caller
        .data_mut()
        .persist
        .take()
        .ok_or_else(|| format_err!("persist_checkpoint outside a persistent run"))
}

/// The first checkpoint: find the slot and take the snapshot.
fn snapshot(caller: &mut Caller<'_, State>, link: &mut Link, pos: u32, end: u32) -> Result<()> {
    let capacity = link.capacity as u32;
    let Some(slot) = end.checked_sub(capacity).filter(|s| *s >= pos && *s > 0) else {
        bail!(
            "the slot ({capacity} bytes) is not after the marker: text at {pos:#x}, its end at {end:#x}"
        );
    };
    // (between the marker's definition and the slot: its line end)
    let line_end = read_bytes(caller, slot - 1, 1)?[0];
    let between = read_bytes(caller, pos, (slot - pos) as usize)?;
    if !matches!(line_end, b'\r' | b'\n') || between.iter().any(|b| !matches!(b, b'\r' | b'\n')) {
        bail!("unexpected text before the slot: {between:?}");
    }
    let m = memory(caller)?;
    let data = m.data(&*caller).to_vec();
    let sp = stack_pointer(caller)?;
    let state = caller.data();
    let mut handles = vec![];
    for (value, handle) in &state.handles {
        match handle {
            Handle::Find { entries, next } => handles.push((*value, entries.clone(), *next)),
            Handle::File { .. } | Handle::Memory { .. } => {
                bail!("a file is open at the checkpoint (handle {value:#x})")
            }
        }
    }
    link.slot = slot;
    link.line_end = line_end;
    let snapshot_bytes = data.len();
    link.snapshot = Some(Snapshot {
        memory: data,
        stack_pointer: sp,
        heap: state.heap.clone(),
        last_error: state.last_error,
        tls: state.tls.clone(),
        command_line_at: state.command_line_at,
        handles,
        stdout: state.stdout.len(),
        stderr: state.stderr.len(),
        console: state.console.len(),
        trace: state.trace.len(),
        ticks: state.started.elapsed(),
    });
    let _ = link.events.send(Event::Ready { snapshot_bytes });
    Ok(())
}

/// Restore the snapshot: the bytes zeroed above it.
fn restore(caller: &mut Caller<'_, State>, link: &Link) -> Result<usize> {
    let snapshot = link.snapshot.as_ref().unwrap();
    let sp = stack_pointer(caller)?;
    if sp != snapshot.stack_pointer {
        bail!(
            "the stack pointer at the checkpoint moved: {sp:#x}, {:#x} at the snapshot",
            snapshot.stack_pointer
        );
    }
    let m = memory(caller)?;
    let peak = caller.data().heap.peak as usize;
    let data = m.data_mut(&mut *caller);
    let n = snapshot.memory.len();
    data[..n].copy_from_slice(&snapshot.memory);
    // (above the snapshot, what this candidate's heap used: fresh memory reads as zero)
    let top = peak.min(data.len());
    let zeroed = top.saturating_sub(n);
    if top > n {
        data[n..top].fill(0);
    }
    let size = data.len() as u32;
    let state = caller.data_mut();
    state.heap = Heap {
        end: size,
        peak: 0,
        ..snapshot.heap.clone()
    };
    // (a candidate reads files again, as a run does; none writes, since the driver's tail never runs)
    if state.files.changed() {
        bail!("a candidate changed files, which a persistent run does not write");
    }
    state.files.forget_files();
    state.last_error = snapshot.last_error;
    state.tls.clone_from(&snapshot.tls);
    state.command_line_at = snapshot.command_line_at;
    state.handles = snapshot
        .handles
        .iter()
        .map(|(value, entries, next)| {
            (
                *value,
                Handle::Find {
                    entries: entries.clone(),
                    next: *next,
                },
            )
        })
        .collect();
    state.stdout.truncate(snapshot.stdout);
    state.stderr.truncate(snapshot.stderr);
    state.console.truncate(snapshot.console);
    state.trace.truncate(snapshot.trace);
    state.started = Instant::now() - snapshot.ticks;
    Ok(zeroed)
}

pub(crate) fn imports(linker: &mut Linker<State>) -> Result<()> {
    linker.func_wrap(
        "env",
        "persist_checkpoint",
        |mut caller: Caller<'_, State>, pos: u32, end: u32| -> Result<()> {
            let c = &mut caller;
            let mut link = take(c)?;
            let result = (|| {
                if link.snapshot.is_none() {
                    snapshot(c, &mut link, pos, end)?;
                } else {
                    let started = Instant::now();
                    link.zeroed = restore(c, &link)?;
                    link.restore = started.elapsed();
                }
                let (slot, timeout, trace) = match link.jobs.recv() {
                    Ok(Job::Compile(slot, timeout, trace)) => (slot, timeout, trace),
                    Ok(Job::Stop) | Err(_) => return Err(Stopped::Cancelled.into()),
                };
                let line_end = link.line_end;
                let text: Vec<u8> = slot
                    .iter()
                    .map(|b| if *b == b'\n' { line_end } else { *b })
                    .collect();
                let m = memory(c)?;
                m.write(&mut *c, link.slot as usize, &text)?;
                set_tracing(c, trace)?;
                link.started = Instant::now();
                link.timeout = timeout;
                Ok(())
            })();
            c.data_mut().persist = Some(link);
            result
        },
    )?;
    linker.func_wrap(
        "env",
        "persist_done",
        |mut caller: Caller<'_, State>, status: i32, object: u32, size: i32| -> Result<()> {
            let c = &mut caller;
            let object = if status == 0 && object != 0 {
                Some(read_bytes(c, object, size as usize)?)
            } else {
                None
            };
            let link = take(c)?;
            let snapshot = link.snapshot.as_ref().unwrap();
            let mut console = c.data().console[snapshot.console..].to_vec();
            // (the driver's last line, which a candidate never reaches: CLStaticMain's)
            match status {
                1 => console.extend(b"\r\nErrors caused tool to abort.\r\n"),
                2 => console.extend(b"\r\nUser break, cancelled...\r\n"),
                _ => {}
            }
            let trace = c.data().trace[snapshot.trace..].to_vec();
            let _ = link.events.send(Event::Done(Candidate {
                exit_code: status,
                object,
                console,
                restore: link.restore,
                zeroed: link.zeroed,
                compile: link.started.elapsed(),
                trace,
            }));
            c.data_mut().persist = Some(link);
            Ok(())
        },
    )?;
    Ok(())
}
