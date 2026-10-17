//! The GC 2.7 compiler built from gc2's sources as WebAssembly (`wasm/`, `mwcc-tools wasm build`), run in process.
//!
//! The module is the compiler, MSL and its Win32 layer; what it imports is the Win32 API that layer and the driver
//! call (63 functions of kernel32 and version.dll). This crate implements them on the host: files and directories
//! (paths resolved from the run's directory), the standard handles (captured), the environment and command line the
//! caller gives, time, thread-local slots and a heap for `GlobalAlloc`. The compiler's own executable, which the
//! driver looks for and reads its version information from, is a file of the run: `program` names it, and its
//! version information is `version_info`. `ExitProcess` ends the run.
//!
//! Each run instantiates the module afresh: the compiler keeps its state in globals and is not reentrant.
use std::{
    collections::{BTreeMap, HashMap},
    fs,
    io::{Read, Seek, SeekFrom, Write},
    path::{Path, PathBuf},
    time::{Instant, SystemTime, UNIX_EPOCH},
};
use wasmtime::{
    Caller, Config, Engine, Extern, Linker, Memory, Module, Result, Store, bail, error::Context,
    format_err,
};

const ERROR_FILE_NOT_FOUND: u32 = 2;
const ERROR_PATH_NOT_FOUND: u32 = 3;
const ERROR_ACCESS_DENIED: u32 = 5;
const ERROR_INVALID_HANDLE: u32 = 6;
const ERROR_NO_MORE_FILES: u32 = 18;
const ERROR_FILE_EXISTS: u32 = 80;
const ERROR_ALREADY_EXISTS: u32 = 183;
const ERROR_RESOURCE_TYPE_NOT_FOUND: u32 = 1813;
const INVALID_HANDLE_VALUE: u32 = 0xFFFF_FFFF;
const FILE_ATTRIBUTE_READONLY: u32 = 0x01;
const FILE_ATTRIBUTE_DIRECTORY: u32 = 0x10;
const FILE_ATTRIBUTE_NORMAL: u32 = 0x80;
const STDIN: u32 = 0x10;
const STDOUT: u32 = 0x14;
const STDERR: u32 = 0x18;
const MODULE: u32 = 0x0040_0000;
/// 100-ns intervals from 1601 to 1970
const FILETIME_UNIX: u64 = 116_444_736_000_000_000;

/// The compiled module, ready to run (compiled once; each run instantiates it).
pub struct Compiler {
    engine: Engine,
    module: Module,
}

/// One compiler run.
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
}

pub struct Outcome {
    pub exit_code: i32,
    pub stdout: Vec<u8>,
    pub stderr: Vec<u8>,
    /// Both, in the order written (as a console, or one file for both, has them).
    pub console: Vec<u8>,
}

enum Handle {
    File { file: fs::File },
    Find { entries: Vec<Entry>, next: usize },
}

#[derive(Clone)]
struct Entry {
    name: String,
    attributes: u32,
    size: u64,
    written: u64,
}

struct State {
    directory: PathBuf,
    program: String,
    version_info: Vec<u8>,
    command_line: String,
    environment: Vec<(String, String)>,
    stdout: Vec<u8>,
    stderr: Vec<u8>,
    console: Vec<u8>,
    last_error: u32,
    handles: HashMap<u32, Handle>,
    next_handle: u32,
    tls: Vec<u32>,
    heap: Heap,
    started: Instant,
    command_line_at: Option<u32>,
}

/// The heap GlobalAlloc gives: blocks of the wasm memory above what the module uses, grown as needed. A freed block
/// is reused for a request of its size (MSL's pools ask for the same sizes again).
#[derive(Default)]
struct Heap {
    sizes: HashMap<u32, u32>,
    free: BTreeMap<u32, Vec<u32>>,
    top: u32,
    end: u32,
}

/// The run ended (ExitProcess), with this status.
#[derive(Debug)]
struct Exit(i32);

impl std::fmt::Display for Exit {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(f, "exit {}", self.0)
    }
}
impl std::error::Error for Exit {}

impl Compiler {
    pub fn new(wasm: &[u8]) -> Result<Self> {
        let mut config = Config::new();
        config.wasm_exceptions(true);
        // (the compiler recurses deeply: its own 8 MiB shadow stack, and room on the host's)
        config.max_wasm_stack(2 << 20);
        let engine = Engine::new(&config)?;
        let module = Module::new(&engine, wasm).context("compile the compiler's module")?;
        Ok(Self { engine, module })
    }

    pub fn run(&self, request: &Request) -> Result<Outcome> {
        let state = State {
            directory: plain(request.directory),
            program: request.program.into(),
            version_info: request.version_info.to_vec(),
            command_line: command_line(request.program, request.args),
            environment: request.environment.to_vec(),
            stdout: vec![],
            stderr: vec![],
            console: vec![],
            last_error: 0,
            handles: HashMap::new(),
            next_handle: 0x100,
            tls: vec![],
            heap: Heap::default(),
            started: Instant::now(),
            command_line_at: None,
        };
        let mut store = Store::new(&self.engine, state);
        let mut linker = Linker::new(&self.engine);
        imports(&mut linker)?;
        let instance = linker.instantiate(&mut store, &self.module)?;
        let memory = instance
            .get_memory(&mut store, "memory")
            .context("the module exports no memory")?;
        let pages = memory.size(&store) as u32;
        store.data_mut().heap.top = pages << 16;
        store.data_mut().heap.end = pages << 16;
        let start = instance.get_typed_func::<(), ()>(&mut store, "_start")?;
        let exit_code = match start.call(&mut store, ()) {
            Ok(()) => 0,
            Err(error) => match error.downcast_ref::<Exit>() {
                Some(Exit(code)) => *code,
                None => return Err(error.context("the compiler trapped")),
            },
        };
        let state = store.into_data();
        Ok(Outcome {
            exit_code,
            stdout: state.stdout,
            stderr: state.stderr,
            console: state.console,
        })
    }
}
/// PATH without a verbatim prefix (`\\?\`, as canonicalized paths have on Windows): the form a process's current
/// PATH without a verbatim prefix (`\?\`, as canonicalized paths have on Windows): the form a process's current
/// directory takes.
fn plain(path: &Path) -> PathBuf {
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
    Ok(u32::from_le_bytes(
        read_bytes(caller, at, 4)?.try_into().unwrap(),
    ))
}

/// The NUL-terminated string at AT (Windows-1252, as the compiler's char strings are).
fn read_string(caller: &mut Caller<'_, State>, at: u32) -> Result<String> {
    let m = memory(caller)?;
    let data = m.data(&*caller);
    let start = at as usize;
    let end = data[start..]
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

impl Heap {
    fn alloc(
        &mut self,
        memory: &Memory,
        store: &mut impl wasmtime::AsContextMut,
        size: u32,
    ) -> Result<u32> {
        let size = (size.max(1) + 15) & !15;
        if let Some(blocks) = self.free.get_mut(&size)
            && let Some(at) = blocks.pop()
        {
            self.sizes.insert(at, size);
            return Ok(at);
        }
        if self.top + size > self.end {
            let pages = (self.top + size - self.end).div_ceil(1 << 16);
            memory
                .grow(&mut *store, u64::from(pages))
                .map_err(|_| format_err!("out of memory"))?;
            self.end += pages << 16;
        }
        let at = self.top;
        self.top += size;
        self.sizes.insert(at, size);
        Ok(at)
    }

    fn free(&mut self, at: u32) -> bool {
        match self.sizes.remove(&at) {
            Some(size) => {
                self.free.entry(size).or_default().push(at);
                true
            }
            None => false,
        }
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

/// The host path of the compiler's PATH, relative ones from the run's directory.
fn host_path(state: &State, path: &str) -> PathBuf {
    let native = if cfg!(windows) {
        path.to_owned()
    } else {
        path.replace('\\', "/")
    };
    state.directory.join(native)
}

fn is_program(state: &State, path: &str) -> bool {
    let a = host_path(state, path);
    let b = host_path(state, &state.program);
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
        let directory = state.directory.to_string_lossy().replace('/', "\\");
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
        let mut text = latin1(&c.data().command_line.clone());
        text.push(0);
        let at = heap_alloc(c, text.len() as u32, false)?;
        write_bytes(c, at, &text)?;
        c.data_mut().command_line_at = Some(at);
        Ok(at)
    });
    import!(linker, "GetEnvironmentStrings", |c| -> u32 {
        let mut block = vec![];
        for (name, value) in c.data().environment.clone() {
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
    import!(linker, "IsDBCSLeadByte", |c, _byte: u32| -> u32 {
        let _ = c;
        Ok(0)
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
        if (size + 15) & !15 <= old {
            return Ok(at);
        }
        let new = heap_alloc(c, size, flags & 0x40 != 0)?;
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
        let path = host_path(c.data(), &name);
        if path.is_dir() {
            c.data_mut().last_error = ERROR_ACCESS_DENIED;
            return Ok(INVALID_HANDLE_VALUE);
        }
        let mut options = fs::OpenOptions::new();
        options.read(access & 0x8000_0000 != 0 || access & 0x4000_0000 == 0);
        options.write(access & 0x4000_0000 != 0);
        let exists = path.exists();
        match creation {
            1 => {
                options.create_new(true);
            }
            2 => {
                options.create(true).truncate(true).write(true);
            }
            4 => {
                options.create(true).write(access & 0x4000_0000 != 0);
            }
            5 => {
                options.truncate(true).write(true);
            }
            _ => {}
        }
        if !exists && matches!(creation, 3 | 5) {
            let parent_exists = path.parent().is_none_or(Path::exists);
            c.data_mut().last_error = if parent_exists {
                ERROR_FILE_NOT_FOUND
            } else {
                ERROR_PATH_NOT_FOUND
            };
            return Ok(INVALID_HANDLE_VALUE);
        }
        match options.open(&path) {
            Ok(file) => {
                let handle = c.data().next_handle;
                c.data_mut().next_handle += 4;
                c.data_mut().handles.insert(handle, Handle::File { file });
                // (CREATE_ALWAYS and OPEN_ALWAYS on an existing file succeed and say so)
                c.data_mut().last_error = if exists && matches!(creation, 2 | 4) {
                    ERROR_ALREADY_EXISTS
                } else {
                    0
                };
                Ok(handle)
            }
            Err(error) => {
                c.data_mut().last_error = if error.kind() == std::io::ErrorKind::NotFound
                    && path.parent().is_some_and(|p| !p.exists())
                {
                    ERROR_PATH_NOT_FOUND
                } else {
                    io_error(&error)
                };
                Ok(INVALID_HANDLE_VALUE)
            }
        }
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
        let mut data = vec![0; count as usize];
        let result = match c.data_mut().handles.get_mut(&handle) {
            Some(Handle::File { file }) => {
                let mut total = 0;
                loop {
                    match file.read(&mut data[total..]) {
                        Ok(0) => break Ok(total),
                        Ok(n) => {
                            total += n;
                            if total == data.len() {
                                break Ok(total);
                            }
                        }
                        Err(e) => break Err(io_error(&e)),
                    }
                }
            }
            _ => Err(ERROR_INVALID_HANDLE),
        };
        match result {
            Ok(n) => {
                write_bytes(c, buffer, &data[..n])?;
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
            _ => match c.data_mut().handles.get_mut(&handle) {
                Some(Handle::File { file }) => file.write_all(&data).map_err(|e| io_error(&e)),
                _ => Err(ERROR_INVALID_HANDLE),
            },
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
        let result = match c.data_mut().handles.get_mut(&handle) {
            Some(Handle::File { file }) => {
                let from = match method {
                    0 => SeekFrom::Start(distance.max(0) as u64),
                    1 => SeekFrom::Current(distance),
                    _ => SeekFrom::End(distance),
                };
                file.seek(from).map_err(|e| io_error(&e))
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
        let size = match c.data_mut().handles.get_mut(&handle) {
            Some(Handle::File { file }) => file.metadata().map(|m| m.len()).ok(),
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
        let result = match c.data_mut().handles.get_mut(&handle) {
            Some(Handle::File { file }) => file
                .stream_position()
                .and_then(|p| file.set_len(p))
                .map_err(|e| io_error(&e)),
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
        let time = match c.data_mut().handles.get_mut(&handle) {
            Some(Handle::File { file }) => file
                .metadata()
                .ok()
                .and_then(|m| m.modified().ok())
                .map(filetime),
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
        let path = host_path(c.data(), &name);
        match fs::metadata(&path) {
            Ok(m) => Ok(entry_of(String::new(), &m).attributes),
            Err(e) => {
                c.data_mut().last_error = if path.parent().is_some_and(|p| !p.exists()) {
                    ERROR_PATH_NOT_FOUND
                } else {
                    io_error(&e)
                };
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
        let path = host_path(c.data(), &name);
        match fs::remove_file(path) {
            Ok(()) => Ok(1),
            Err(e) => fail(c, io_error(&e)),
        }
    });
    import!(linker, "CreateDirectoryA", |c,
                                         path: u32,
                                         _security: u32|
     -> u32 {
        let name = read_string(c, path)?;
        let path = host_path(c.data(), &name);
        match fs::create_dir(path) {
            Ok(()) => Ok(1),
            Err(e) if e.kind() == std::io::ErrorKind::AlreadyExists => {
                fail(c, ERROR_ALREADY_EXISTS)
            }
            Err(e) => fail(c, io_error(&e)),
        }
    });
    import!(linker, "RemoveDirectoryA", |c, path: u32| -> u32 {
        let name = read_string(c, path)?;
        let path = host_path(c.data(), &name);
        match fs::remove_dir(path) {
            Ok(()) => Ok(1),
            Err(e) => fail(c, io_error(&e)),
        }
    });
    import!(linker, "GetCurrentDirectoryA", |c,
                                             size: u32,
                                             buffer: u32|
     -> u32 {
        let directory = c.data().directory.to_string_lossy().into_owned();
        let directory = if cfg!(windows) {
            directory
        } else {
            directory.replace('/', "\\")
        };
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
        if !is_program(c.data(), &name) && !host_path(c.data(), &name).exists() {
            return fail(c, ERROR_FILE_NOT_FOUND);
        }
        give_string(c, &name, buffer, size)
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
            });
        } else if !wildcard.contains(['*', '?']) {
            if let Ok(m) = fs::metadata(host_path(c.data(), &pattern)) {
                // (the name as the directory has it)
                let path = host_path(c.data(), &pattern);
                let name = path
                    .parent()
                    .and_then(|p| fs::read_dir(p).ok())
                    .and_then(|mut d| {
                        d.find_map(|e| {
                            let e = e.ok()?;
                            let n = e.file_name().to_string_lossy().into_owned();
                            n.eq_ignore_ascii_case(&wildcard).then_some(n)
                        })
                    })
                    .unwrap_or(wildcard.clone());
                entries.push(entry_of(name, &m));
            }
        } else if let Ok(listing) = fs::read_dir(host_path(
            c.data(),
            if directory.is_empty() {
                "."
            } else {
                &directory
            },
        )) {
            for e in listing.flatten() {
                let name = e.file_name().to_string_lossy().into_owned();
                if matches(wildcard.as_bytes(), name.as_bytes())
                    && let Ok(m) = e.metadata()
                {
                    entries.push(entry_of(name, &m));
                }
            }
            if matches(wildcard.as_bytes(), b".") {
                entries.insert(
                    0,
                    Entry {
                        name: ".".into(),
                        attributes: FILE_ATTRIBUTE_DIRECTORY,
                        size: 0,
                        written: FILETIME_UNIX,
                    },
                );
                entries.insert(
                    1,
                    Entry {
                        name: "..".into(),
                        attributes: FILE_ATTRIBUTE_DIRECTORY,
                        size: 0,
                        written: FILETIME_UNIX,
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
        let handle = c.data().next_handle;
        c.data_mut().next_handle += 4;
        c.data_mut()
            .handles
            .insert(handle, Handle::Find { entries, next: 1 });
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
    Ok(())
}

/// WIN32_FIND_DATAA: attributes, three FILETIMEs, the size (high, low), two reserved words, the name and the 8.3
/// name.
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

    #[test]
    fn dates_round_trip() {
        for days in [-1, 0, 11_016, 20_000, 30_000] {
            let (y, m, _, d) = civil(days);
            assert_eq!(days_from_civil(y, m, d), days);
        }
        assert_eq!(civil(0), (1970, 1, 4, 1));
    }
}
