//! The register counters' increments in gc2's sources, and the original's site of each: the table the WebAssembly
//! oracle's counter probe looks its sites up in (`crates/mwcc-oracle-hooks/src/counter_sites.rs`).
//!
//! The x86 oracle probes the original's increments (crate::counter_probe). gc2's are its `ALLOC_GPR()`,
//! `ALLOC_FPR()`, `ALLOC_VR()` and `ALLOC_REG(c)` (`include/compiler/RegisterInfo.h`), which report their file and
//! line under ORACLE_PROBES. Each of the original's increments is matched with one of them by the line numbers of
//! gc2's objects, which the original's compiler builds from the same sources into the same code (`ninja all_source`
//! for each version):
//!
//! * A function's increments and its writes are aligned in order, each increment at most a few lines from its write
//!   (the compiler gives an increment its statement's first line, and moves some before it).
//! * The original inlines some functions, at some or all of their calls; their increments then have the call's line.
//!   Their writes, at such a call, are slots too, which the alignment may leave out (the compiler drops the code a
//!   call's constant arguments make dead). The calls are made through `ORACLE_INLINED` (wrapper macros after each such
//!   function's definition), whose line the probe reports with the write's.
//!
//! GC 2.6's increments are GC 2.7's, at the same offsets in the same functions. GC 1.3.2's are aligned on their own,
//! with gc2's sources as 1.3.2 compiles them (the branches of its `#if VERSION ...`) and its objects' lines.
use crate::{
    coff::Object,
    counter_probe::{self, Role},
    disassembly,
    executable::{self, hex},
    sequence,
    source_tree::SourceTree,
};
use anyhow::{Context, Result, bail, ensure};
use regex::Regex;
use std::{
    collections::{BTreeMap, BTreeSet},
    fmt::Write as _,
    fs,
    path::Path,
    sync::LazyLock,
};

/// The generated table, in the hooks' sources.
pub const OUTPUT: &str = "crates/mwcc-oracle-hooks/src/counter_sites.rs";
/// The version whose objects give the lines, the one whose sites are its, moved, and the one aligned on its own.
const LINES: &str = "GC_2_7";
const MOVED: &str = "GC_2_6";
const OWN: &str = "GC_1_3_2";

/// The write at LINE of UNIT, in a call that the original inlines ending at INLINED (the line `__LINE__` gives the call
/// in a macro, its closing parenthesis's), or 0.
#[derive(Clone, Debug, PartialEq, Eq, PartialOrd, Ord)]
pub struct Write {
    pub unit: String,
    pub line: u32,
    pub inlined: u32,
}

/// A write's increments: in GC 2.7 and GC 2.6 (0 without one) and their bindings (counter_probe::Site's
/// object_binding, operand_binding and origin_binding), and in GC 1.3.2 with its bindings.
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct Sites {
    pub gc2_7: (u32, u32, [u8; 3]),
    pub gc1_3_2: (u32, [u8; 3]),
}

#[derive(Debug, Default)]
pub struct Table {
    /// Each write an original has an increment for.
    pub sites: BTreeMap<Write, Sites>,
    /// The writes none has one for (UNIT, LINE): not in a function of the originals' that their probes cover.
    pub none: BTreeSet<(String, u32)>,
    /// The functions the original inlines at some call (UNIT, NAME).
    pub inlined: BTreeSet<(String, String)>,
}

static WRITE: LazyLock<Regex> =
    LazyLock::new(|| Regex::new(r"\bALLOC_(?:GPR|FPR|VR)\(\)|\bALLOC_REG\(([^()]*)\)").unwrap());
static RAW_WRITE: LazyLock<Regex> = LazyLock::new(|| {
    Regex::new(r"used_virtual_registers\s*\[[^\]]*\]\s*(?:\+\+|\+=)|\+\+\s*used_virtual_registers")
        .unwrap()
});
static CALL: LazyLock<Regex> = LazyLock::new(|| Regex::new(r"\b([A-Za-z_]\w*)\s*\(").unwrap());
static DEFINITION: LazyLock<Regex> =
    LazyLock::new(|| Regex::new(r"^[A-Za-z_][\w\s\*]*?\b([A-Za-z_]\w*)\s*\(").unwrap());

fn class(text: &str) -> Option<u8> {
    match text {
        "ALLOC_GPR()" => Some(4),
        "ALLOC_FPR()" => Some(3),
        "ALLOC_VR()" => Some(2),
        _ => None,
    }
}

/// TEXT without its comments, each replaced by spaces (lines and columns kept).
fn strip_comments(text: &str) -> String {
    let mut out = String::with_capacity(text.len());
    let bytes = text.as_bytes();
    let mut i = 0;
    let (mut string, mut character) = (false, false);
    while i < bytes.len() {
        let c = bytes[i];
        if string || character {
            out.push(c as char);
            if c == b'\\' && i + 1 < bytes.len() {
                out.push(bytes[i + 1] as char);
                i += 2;
                continue;
            }
            if (string && c == b'"') || (character && c == b'\'') {
                (string, character) = (false, false);
            }
            i += 1;
            continue;
        }
        if c == b'/' && bytes.get(i + 1) == Some(&b'/') {
            while i < bytes.len() && bytes[i] != b'\n' {
                out.push(' ');
                i += 1;
            }
            continue;
        }
        if c == b'/' && bytes.get(i + 1) == Some(&b'*') {
            out.push_str("  ");
            i += 2;
            while i < bytes.len() && !(bytes[i] == b'*' && bytes.get(i + 1) == Some(&b'/')) {
                out.push(if bytes[i] == b'\n' { '\n' } else { ' ' });
                i += 1;
            }
            out.push_str("  ");
            i += 2;
            continue;
        }
        string = c == b'"';
        character = c == b'\'';
        out.push(c as char);
        i += 1;
    }
    out
}

/// The last line of the call whose opening parenthesis is at COLUMN of LINE.
/// The opening parenthesis of the `ORACLE_INLINED(` the call at COLUMN of LINE is the argument of, if it is: the
/// macro's closing parenthesis is the call's line in it.
fn enclosing_inlined(lines: &[String], line: u32, column: usize) -> Option<(u32, usize)> {
    const MACRO: &str = "ORACLE_INLINED(";
    let (mut line, mut before) = (line, &lines[line as usize - 1][..column]);
    loop {
        let text = before.trim_end();
        if !text.is_empty() {
            return text.ends_with(MACRO).then(|| (line, text.len() - 1));
        }
        line = line.checked_sub(1).filter(|l| *l > 0)?;
        before = &lines[line as usize - 1];
    }
}

fn call_end(lines: &[String], line: u32, column: usize) -> u32 {
    let mut depth = 0;
    for (index, code) in lines.iter().enumerate().skip(line as usize - 1) {
        let from = if index == line as usize - 1 {
            column
        } else {
            0
        };
        for c in code[from..].chars() {
            match c {
                '(' => depth += 1,
                ')' => {
                    depth -= 1;
                    if depth == 0 {
                        return index as u32 + 1;
                    }
                }
                _ => {}
            }
        }
    }
    line
}

#[derive(Clone, Debug)]
enum Event {
    /// A write at its line, of a class if a constant.
    Write(u32, Option<u8>),
    /// A call of a function of the same unit: its first and last lines.
    Call(u32, u32, String),
}

/// A unit of gc2's: its functions' definitions (first and last line) and their writes and calls, in order.
struct Unit {
    lines: Vec<String>,
    functions: BTreeMap<String, (u32, u32)>,
    events: BTreeMap<String, Vec<Event>>,
}

/// gc2's version indices (`include/version.h`): `VERSION_GC_2_7` and the others, by name.
fn version_indices(root: &Path) -> Result<BTreeMap<String, i64>> {
    let path = root.join("gc2/include/version.h");
    let text = fs::read_to_string(&path).with_context(|| format!("read {}", path.display()))?;
    let define = Regex::new(r"^#define (VERSION_\w+) (\d+)")?;
    Ok(text
        .lines()
        .filter_map(|l| define.captures(l))
        .map(|c| (c[1].to_owned(), c[2].parse().unwrap()))
        .collect())
}

/// The value of a `#if` condition on VERSION (`VERSION >= VERSION_GC_2_0 && VERSION < VERSION_GC_2_5`), or None
/// for another condition.
fn version_condition(text: &str, version: i64, indices: &BTreeMap<String, i64>) -> Option<bool> {
    static TOKEN: LazyLock<Regex> =
        LazyLock::new(|| Regex::new(r"\s*(&&|\|\||==|!=|<=|>=|<|>|!|\(|\)|\w+)").unwrap());
    let mut tokens = vec![];
    let mut rest = text.trim();
    let mut versioned = false;
    while !rest.is_empty() {
        let c = TOKEN
            .captures(rest)
            .filter(|c| c.get(0).unwrap().start() == 0)?;
        let token = c[1].to_owned();
        rest = &rest[c.get(0).unwrap().end()..];
        tokens.push(if token == "VERSION" {
            versioned = true;
            version.to_string()
        } else if let Some(index) = indices.get(&token) {
            index.to_string()
        } else {
            token
        });
    }
    if !versioned {
        return None;
    }
    // (|| of && of comparisons of numbers, ! and parentheses)
    fn or(t: &[String], i: &mut usize) -> Option<i64> {
        let mut value = and(t, i)?;
        while t.get(*i).map(String::as_str) == Some("||") {
            *i += 1;
            let right = and(t, i)?;
            value = (value != 0 || right != 0) as i64;
        }
        Some(value)
    }
    fn and(t: &[String], i: &mut usize) -> Option<i64> {
        let mut value = compare(t, i)?;
        while t.get(*i).map(String::as_str) == Some("&&") {
            *i += 1;
            let right = compare(t, i)?;
            value = (value != 0 && right != 0) as i64;
        }
        Some(value)
    }
    fn compare(t: &[String], i: &mut usize) -> Option<i64> {
        let left = unary(t, i)?;
        let op = t.get(*i).map(String::as_str);
        let f: fn(i64, i64) -> bool = match op {
            Some("==") => |a, b| a == b,
            Some("!=") => |a, b| a != b,
            Some("<") => |a, b| a < b,
            Some("<=") => |a, b| a <= b,
            Some(">") => |a, b| a > b,
            Some(">=") => |a, b| a >= b,
            _ => return Some(left),
        };
        *i += 1;
        let right = unary(t, i)?;
        Some(f(left, right) as i64)
    }
    fn unary(t: &[String], i: &mut usize) -> Option<i64> {
        let token = t.get(*i)?.as_str();
        *i += 1;
        match token {
            "!" => Some((unary(t, i)? == 0) as i64),
            "(" => {
                let value = or(t, i)?;
                (t.get(*i).map(String::as_str) == Some(")")).then(|| *i += 1)?;
                Some(value)
            }
            number => number.parse().ok(),
        }
    }
    let mut i = 0;
    let value = or(&tokens, &mut i)?;
    (i == tokens.len()).then_some(value != 0)
}

/// LINES with the lines VERSION does not compile blank: the branches of conditions on VERSION it does not take.
/// (Other conditions are taken whole, all their branches.)
fn preprocess(lines: &mut [String], version: i64, indices: &BTreeMap<String, i64>) {
    // (per open #if: whether it is on VERSION, whether a branch was taken, whether this branch is)
    let mut stack: Vec<(bool, bool, bool)> = vec![];
    for line in lines.iter_mut() {
        let code = line.trim_start();
        let directive = code.strip_prefix('#').map(str::trim_start);
        let mut blank = !stack.iter().all(|f| f.2);
        if let Some(directive) = directive {
            let (word, rest) = directive
                .split_once(|c: char| c.is_whitespace() || c == '(')
                .map_or((directive, ""), |(w, _)| (w, &directive[w.len()..]));
            match word {
                "if" => {
                    stack.push(match version_condition(rest, version, indices) {
                        Some(value) => (true, value, value),
                        None => (false, true, true),
                    });
                }
                "ifdef" | "ifndef" => stack.push((false, true, true)),
                "elif" => {
                    if let Some(frame) = stack.last_mut()
                        && frame.0
                    {
                        let value =
                            !frame.1 && version_condition(rest, version, indices).unwrap_or(true);
                        frame.1 |= value;
                        frame.2 = value;
                    }
                }
                "else" => {
                    if let Some(frame) = stack.last_mut()
                        && frame.0
                    {
                        frame.2 = !frame.1;
                        frame.1 = true;
                    }
                }
                "endif" => {
                    stack.pop();
                }
                _ => {}
            }
            // (the conditions themselves; other directives, like the wrappers' #define, stay)
            blank |= ["if", "ifdef", "ifndef", "elif", "else", "endif"].contains(&word);
        }
        if blank {
            line.clear();
        }
    }
}

impl Unit {
    /// NAME as VERSION (a `version.h` index) compiles it, or with all its branches (None).
    fn read(root: &Path, name: &str, version: Option<i64>) -> Result<Self> {
        let path = root.join("gc2").join(name);
        let text = fs::read(&path).with_context(|| format!("read {}", path.display()))?;
        let text = strip_comments(&String::from_utf8_lossy(&text).replace('\r', ""));
        let mut lines: Vec<String> = text.split('\n').map(str::to_owned).collect();
        if let Some(version) = version {
            preprocess(&mut lines, version, &version_indices(root)?);
        }
        let mut functions = BTreeMap::new();
        let (mut depth, mut current): (i32, Option<(String, u32)>) = (0, None);
        for (index, code) in lines.iter().enumerate() {
            let number = index as u32 + 1;
            if depth == 0
                && !code.trim_end().ends_with(';')
                && !code.starts_with('#')
                && let Some(c) = DEFINITION.captures(code)
                && !["if", "while", "for", "switch", "return", "sizeof"].contains(&&c[1])
            {
                current = Some((c[1].to_owned(), number));
            }
            let (opened, closed) = (code.matches('{').count(), code.matches('}').count());
            depth += opened as i32 - closed as i32;
            if depth == 0
                && opened + closed > 0
                && let Some((function, first)) = current.take()
            {
                functions.insert(function, (first, number));
            }
        }
        let mut events = BTreeMap::new();
        for (function, &(first, last)) in &functions {
            let mut list = vec![];
            for number in first..=last {
                let code = &lines[number as usize - 1];
                if let Some(m) = RAW_WRITE.find(code) {
                    bail!(
                        "{name}:{number}: a counter write outside ALLOC_* ({})",
                        m.as_str()
                    );
                }
                let mut items: Vec<(usize, Event)> = WRITE
                    .find_iter(code)
                    .map(|m| (m.start(), Event::Write(number, class(m.as_str()))))
                    .collect();
                for c in CALL.captures_iter(code) {
                    let callee = &c[1];
                    if callee != function && functions.contains_key(callee) {
                        let at = c.get(0).unwrap();
                        let (line, column) = enclosing_inlined(&lines, number, at.start())
                            .unwrap_or((number, at.end() - 1));
                        let end = call_end(&lines, line, column);
                        items.push((at.start(), Event::Call(number, end, callee.into())));
                    }
                }
                items.sort_by_key(|(at, _)| *at);
                list.extend(items.into_iter().map(|(_, e)| e));
            }
            events.insert(function.clone(), list);
        }
        Ok(Self {
            lines,
            functions,
            events,
        })
    }

    /// The writes of FUNCTION, inlined: (line, class, the inlined calls' lines), and those of the functions it calls
    /// that the original lacks (RETAIL: its functions).
    fn writes(
        &self,
        function: &str,
        retail: &BTreeSet<String>,
        depth: usize,
    ) -> Vec<(u32, Option<u8>)> {
        let mut out = vec![];
        if depth > 4 {
            return out;
        }
        for event in self.events.get(function).into_iter().flatten() {
            match event {
                Event::Write(line, class) => out.push((*line, *class)),
                Event::Call(_, _, callee) if !retail.contains(callee) => {
                    // (a function inlined in an inlined function: its writes would need both calls' lines)
                    let nested = self.writes(callee, retail, depth + 1);
                    if !nested.is_empty() {
                        out.push((0, None));
                    }
                }
                Event::Call(..) => {}
            }
        }
        out
    }
}

/// One of the original's increments: its site, line (None without line numbers) and class (None for a register's).
#[derive(Clone, Copy, Debug)]
struct Increment {
    site: u32,
    line: Option<u32>,
    class: Option<u8>,
    bindings: [u8; 3],
}

/// A place an increment can come from: its lines (a call's), its write, and the costs of leaving it out and of
/// matching it.
struct Slot {
    line: u32,
    end: u32,
    write: Write,
    class: Option<u8>,
    skip: f64,
    cost: f64,
}

const SKIP_WRITE: f64 = 1000.0;

/// The order-keeping assignment of each increment to a slot, at the least cost: each slot's index.
fn align(increments: &[Increment], slots: &[Slot]) -> Option<Vec<usize>> {
    let (n, m) = (increments.len(), slots.len());
    let flat = increments.iter().all(|i| i.line == increments[0].line);
    let mut best = vec![vec![f64::INFINITY; m + 1]; n + 1];
    let mut back = vec![vec![(0, 0, false); m + 1]; n + 1];
    best[0][0] = 0.0;
    for i in 0..=n {
        for j in 0..=m {
            let cost = best[i][j];
            if cost.is_infinite() {
                continue;
            }
            if j < m && cost + slots[j].skip < best[i][j + 1] {
                best[i][j + 1] = cost + slots[j].skip;
                back[i][j + 1] = (i, j, false);
            }
            if i < n && j < m {
                let (increment, slot) = (&increments[i], &slots[j]);
                let distance = increment.line.map_or(0, |l| {
                    if l < slot.line {
                        l as i64 - slot.line as i64
                    } else {
                        (l as i64 - slot.end as i64).max(0)
                    }
                });
                let near = flat || (-6..=3).contains(&distance);
                let same_class = match (increment.class, slot.class) {
                    (Some(a), Some(b)) => a == b,
                    _ => true,
                };
                let total = cost + slot.cost + distance.unsigned_abs() as f64 * 0.01;
                if near && same_class && total < best[i + 1][j + 1] {
                    best[i + 1][j + 1] = total;
                    back[i + 1][j + 1] = (i, j, true);
                }
            }
        }
    }
    if best[n][m].is_infinite() {
        return None;
    }
    let mut out = vec![0; n];
    let (mut i, mut j) = (n, m);
    while (i, j) != (0, 0) {
        let (pi, pj, matched) = back[i][j];
        if matched {
            out[pi] = pj;
        }
        (i, j) = (pi, pj);
    }
    Some(out)
}

/// gc2's functions of VERSION: (address, end, name without its underscore), by address.
fn functions(root: &Path, tree: &SourceTree) -> Result<Vec<(u32, u32, String)>> {
    let symbols = executable::read_symbols(&fs::read_to_string(
        root.join(&tree.config_dir).join("symbols.txt"),
    )?)?;
    let mut out: Vec<(u32, u32, String)> = symbols
        .iter()
        .filter(|s| s.options.get("type").and_then(|t| t.as_str()) == Some("function"))
        .filter_map(|s| {
            let size = hex(s.options.get("size")?.as_str()?).ok()?;
            Some((
                s.address,
                s.address + size,
                s.name.trim_start_matches('_').to_owned(),
            ))
        })
        .collect();
    out.sort();
    Ok(out)
}

fn containing(functions: &[(u32, u32, String)], address: u32) -> Option<&(u32, u32, String)> {
    let index = functions
        .partition_point(|f| f.0 <= address)
        .checked_sub(1)?;
    functions.get(index).filter(|f| address < f.1)
}

/// The line numbers of an object of gc2's, compiled by the original's compiler: per function, its first line and
/// its (offset, line) entries.
struct Lines {
    functions: BTreeMap<String, (u32, Vec<(u32, u32)>)>,
    /// Each function's code.
    code: BTreeMap<String, Vec<u8>>,
}

impl Lines {
    fn read(path: &Path) -> Result<Self> {
        let data = fs::read(path)
            .with_context(|| format!("read {} (ninja all_source)", path.display()))?;
        let object = Object::parse(&data)?;
        let u16_at = |at: usize| u16::from_le_bytes([data[at], data[at + 1]]);
        let u32_at = |at: usize| u32::from_le_bytes(data[at..at + 4].try_into().unwrap());
        let headers = 20 + u16_at(16) as usize;
        let (mut functions, mut code) = (BTreeMap::new(), BTreeMap::new());
        let symbols: Vec<_> = object.symbols.values().collect();
        for (index, symbol) in symbols.iter().enumerate() {
            if symbol.section <= 0 || symbol.kind & 0x20 == 0 {
                continue;
            }
            // (the function's .bf: its first line)
            let Some(first) = symbols[index + 1..]
                .iter()
                .take_while(|s| s.kind & 0x20 == 0)
                .find(|s| s.name == ".bf")
                .and_then(|s| s.auxiliary.first())
                .map(|a| u16::from_le_bytes([a[4], a[5]]) as u32)
            else {
                continue;
            };
            let header = headers + (symbol.section as usize - 1) * 40;
            let (pointer, count) = (u32_at(header + 28) as usize, u16_at(header + 34) as usize);
            let entries = (0..count)
                .map(|k| (u32_at(pointer + 6 * k), u16_at(pointer + 6 * k + 4) as u32))
                .filter(|&(_, line)| line != 0)
                .map(|(offset, line)| (offset.wrapping_sub(symbol.value), line))
                .collect();
            let name = symbol.name.trim_start_matches('_').to_owned();
            if let Some(section) = object.sections.get(symbol.section as usize - 1) {
                code.insert(name.clone(), section.data.to_vec());
            }
            functions.insert(name, (first, entries));
        }
        Ok(Self { functions, code })
    }

    /// The offset in FUNCTION of the instruction at OFFSET in ORIGINAL, the original's code of it. The same when
    /// both are as long; otherwise by the two's instruction sequences (their mnemonics: a function that differs
    /// from the original's in its frame or registers), at the nearest instruction both have before it.
    fn offset(&self, function: &str, original: &[u8], offset: u32) -> Result<u32> {
        let Some(ours) = self.code.get(function) else {
            return Ok(offset);
        };
        if ours.len() == original.len() {
            return Ok(offset);
        }
        let decoder = disassembly::decoder()?;
        let decode = |code: &[u8]| -> Result<Vec<(u32, String)>> {
            Ok(decoder
                .disasm_all(code, 0)?
                .iter()
                .map(|i| (i.address() as u32, i.mnemonic().unwrap_or("").to_owned()))
                .collect())
        };
        let (a, b) = (decode(original)?, decode(ours)?);
        let keys = |list: &[(u32, String)]| list.iter().map(|i| i.1.clone()).collect::<Vec<_>>();
        let mut mapped = 0;
        for (ai, bj, n) in sequence::matching_blocks_with_autojunk(&keys(&a), &keys(&b), false) {
            for k in 0..n {
                if a[ai + k].0 > offset {
                    return Ok(mapped);
                }
                mapped = b[bj + k].0;
            }
        }
        Ok(mapped)
    }

    /// The line of OFFSET in FUNCTION, or None without line numbers.
    fn line(&self, function: &str, offset: u32) -> Option<u32> {
        let (first, entries) = self.functions.get(function)?;
        let relative = entries
            .iter()
            .rfind(|(at, _)| *at <= offset)
            .map_or(1, |(_, line)| *line);
        Some(first + relative - 1)
    }
}

/// One version's increments, each matched with its write: (site, bindings), and where each site is (its function
/// and offset).
#[derive(Default)]
struct VersionSites {
    sites: BTreeMap<Write, (u32, [u8; 3])>,
    places: BTreeMap<u32, (String, u32)>,
    inlined: BTreeSet<(String, String)>,
}

/// VERSION's increments, aligned with the writes of gc2's sources as VERSION compiles them, by the line numbers of
/// the objects the original's compiler builds from them (`build/<VERSION>/gc2/compiled`).
fn version_sites(root: &Path, version: &str) -> Result<VersionSites> {
    let tree = SourceTree::gc2(version);
    let index = *version_indices(root)?
        .get(&format!("VERSION_{version}"))
        .with_context(|| format!("gc2's version.h has no VERSION_{version}"))?;
    let gc2 = functions(root, &tree)?;
    let retail: BTreeSet<String> = gc2.iter().map(|f| f.2.clone()).collect();
    let splits = executable::read_splits(&fs::read_to_string(
        root.join(&tree.config_dir).join("splits.txt"),
    )?)?;
    let unit_of = |address: u32| {
        splits.units.iter().find_map(|u| {
            u.ranges
                .iter()
                .any(|r| r.section == ".text" && r.start <= address && address < r.end)
                .then(|| u.name.clone())
        })
    };
    let original = executable::Original::read(root, version)?;
    let pe = original.pe()?;
    let mut out = VersionSites::default();
    let mut by_function: BTreeMap<(String, String), Vec<Increment>> = BTreeMap::new();
    let mut lines: BTreeMap<String, Lines> = BTreeMap::new();
    for site in counter_probe::plan(root, version)?
        .iter()
        .filter(|s| s.role == Role::Increment)
    {
        let f = containing(&gc2, site.address).with_context(|| {
            format!(
                "{version} increment {:#x} outside gc2's functions",
                site.address
            )
        })?;
        let unit = unit_of(site.address).with_context(|| {
            format!("{version} increment {:#x} outside the splits", site.address)
        })?;
        if !lines.contains_key(&unit) {
            let path = root
                .join(&tree.build_dir)
                .join("compiled")
                .join(format!("{unit}.obj"));
            lines.insert(unit.clone(), Lines::read(&path)?);
        }
        let offset = site.address - f.0;
        out.places.insert(site.address, (f.2.clone(), offset));
        let code = pe.read(f.0, (f.1 - f.0) as usize)?;
        let ours = lines[&unit].offset(&f.2, code, offset)?;
        by_function
            .entry((unit.clone(), f.2.clone()))
            .or_default()
            .push(Increment {
                site: site.address,
                line: lines[&unit].line(&f.2, ours),
                class: u8::try_from(site.selector).ok(),
                bindings: [
                    site.object_binding as u8,
                    site.operand_binding as u8,
                    site.origin_binding as u8,
                ],
            });
    }
    let mut units: BTreeMap<String, Unit> = BTreeMap::new();
    for ((unit_name, function), mut increments) in by_function {
        if !units.contains_key(&unit_name) {
            units.insert(
                unit_name.clone(),
                Unit::read(root, &unit_name, Some(index))?,
            );
        }
        let unit = &units[&unit_name];
        increments.sort_by_key(|i| i.site);
        let events = unit
            .events
            .get(&function)
            .with_context(|| format!("{unit_name} has no function {function} in {version}"))?;
        let mut slots = vec![];
        for event in events {
            match event {
                Event::Write(line, class) => slots.push(Slot {
                    line: *line,
                    end: *line,
                    write: Write {
                        unit: unit_name.clone(),
                        line: *line,
                        inlined: 0,
                    },
                    class: *class,
                    skip: SKIP_WRITE,
                    cost: 0.0,
                }),
                Event::Call(call, end, callee) => {
                    for (k, (line, class)) in
                        unit.writes(callee, &retail, 0).into_iter().enumerate()
                    {
                        ensure!(
                            line != 0,
                            "{unit_name}:{call}: {callee} inlines a function with counter writes"
                        );
                        slots.push(Slot {
                            line: *call,
                            end: *end,
                            write: Write {
                                unit: unit_name.clone(),
                                line,
                                inlined: *end,
                            },
                            class,
                            skip: 0.0,
                            cost: 1.0 + 0.001 * k as f64,
                        });
                    }
                }
            }
        }
        let chosen = align(&increments, &slots).with_context(|| {
            format!(
                "{version} {unit_name}: {function}'s {} increments (lines {:?}) do not align with its writes \
                 (line, inlined at, class: {:?})",
                increments.len(),
                increments.iter().map(|i| i.line).collect::<Vec<_>>(),
                slots
                    .iter()
                    .map(|s| (s.write.line, s.write.inlined, s.class))
                    .collect::<Vec<_>>()
            )
        })?;
        for (j, slot) in slots.iter().enumerate() {
            ensure!(
                slot.skip != SKIP_WRITE || chosen.contains(&j),
                "{version} {unit_name}:{}: {function}'s write has no increment",
                slot.line
            );
        }
        for (increment, &j) in increments.iter().zip(&chosen) {
            let write = slots[j].write.clone();
            if write.inlined != 0 {
                let callee = events
                    .iter()
                    .find_map(|e| match e {
                        Event::Call(_, end, callee)
                            if *end == write.inlined
                                && unit.functions[callee].0 <= write.line
                                && write.line <= unit.functions[callee].1 =>
                        {
                            Some(callee.clone())
                        }
                        _ => None,
                    })
                    .context("inlined write without its call")?;
                out.inlined.insert((unit_name.clone(), callee));
            }
            let previous = out
                .sites
                .insert(write.clone(), (increment.site, increment.bindings));
            ensure!(
                previous.is_none(),
                "{version}: two increments for {write:?}"
            );
        }
    }
    Ok(out)
}

pub fn generate(root: &Path) -> Result<Table> {
    let lines = version_sites(root, LINES)?;
    // (GC 2.6's sites: at GC 2.7's offsets in the same functions)
    let moved_functions = functions(root, &SourceTree::gc2(MOVED))?;
    let moved: BTreeMap<(String, u32), (u32, Vec<u8>)> = counter_probe::plan(root, MOVED)?
        .into_iter()
        .filter(|s| s.role == Role::Increment)
        .map(|s| {
            let f = containing(&moved_functions, s.address).with_context(|| {
                format!("{MOVED} increment {:#x} outside gc2's functions", s.address)
            })?;
            Ok(((f.2.clone(), s.address - f.0), (s.address, s.bytes)))
        })
        .collect::<Result<_>>()?;
    let lines_bytes: BTreeMap<u32, Vec<u8>> = counter_probe::plan(root, LINES)?
        .into_iter()
        .filter(|s| s.role == Role::Increment)
        .map(|s| (s.address, s.bytes))
        .collect();
    ensure!(
        moved.len() == lines.places.len(),
        "{MOVED} has {} increments, {LINES} {}",
        moved.len(),
        lines.places.len()
    );
    let own = version_sites(root, OWN)?;
    let mut table = Table::default();
    for (write, &(site, bindings)) in &lines.sites {
        let (function, offset) = &lines.places[&site];
        let (address, bytes) = moved
            .get(&(function.clone(), *offset))
            .with_context(|| format!("{MOVED} has no increment at {function}+{offset:#x}"))?;
        ensure!(
            *bytes == lines_bytes[&site],
            "{MOVED}'s increment at {function}+{offset:#x} differs"
        );
        table.sites.entry(write.clone()).or_default().gc2_7 = (site, *address, bindings);
    }
    for (write, &(site, bindings)) in &own.sites {
        table.sites.entry(write.clone()).or_default().gc1_3_2 = (site, bindings);
    }
    table.inlined = lines.inlined.union(&own.inlined).cloned().collect();
    // (the other writes: in functions no version's probes cover, or in code none compiles)
    let mut units: BTreeMap<String, Unit> = BTreeMap::new();
    for path in glob(&root.join("gc2/src"))? {
        let name = path
            .strip_prefix(root.join("gc2"))?
            .to_string_lossy()
            .replace('\\', "/");
        let text = fs::read_to_string(&path).unwrap_or_default();
        if !WRITE.is_match(&strip_comments(&text)) {
            continue;
        }
        let unit = Unit::read(root, &name, None)?;
        for (index, code) in unit.lines.iter().enumerate() {
            let line = index as u32 + 1;
            if WRITE.is_match(code) && !table.sites.keys().any(|w| w.unit == name && w.line == line)
            {
                table.none.insert((name.clone(), line));
            }
        }
        units.insert(name, unit);
    }
    check_wrappers(&units, &table)?;
    Ok(table)
}

fn glob(directory: &Path) -> Result<Vec<std::path::PathBuf>> {
    let mut out = vec![];
    for entry in fs::read_dir(directory)? {
        let path = entry?.path();
        if path.is_dir() {
            out.extend(glob(&path)?);
        } else if path.extension().is_some_and(|e| e == "c") {
            out.push(path);
        }
    }
    out.sort();
    Ok(out)
}

/// Each function the original inlines has its wrapper macro (`#define F(...) ORACLE_INLINED(F(__VA_ARGS__))`)
/// after its definition, and no call before it.
fn check_wrappers(units: &BTreeMap<String, Unit>, table: &Table) -> Result<()> {
    let mut problems = vec![];
    for (unit_name, function) in &table.inlined {
        let unit = &units[unit_name];
        let wrapper = format!("#define {function}(...) ORACLE_INLINED({function}(__VA_ARGS__))");
        let (_, last) = unit.functions[function];
        let Some(at) = unit.lines.iter().position(|l| l.trim() == wrapper) else {
            problems.push(format!(
                "{unit_name}:{}: `{wrapper}` (the original inlines {function})",
                last + 1
            ));
            continue;
        };
        let at = at as u32 + 1;
        if at <= last {
            problems.push(format!(
                "{unit_name}:{at}: {function}'s wrapper precedes its definition"
            ));
        }
        // (a call is in a function, a declaration is not; a call before the wrapper is made through ORACLE_INLINED)
        let call = Regex::new(&format!(r"\b{function}\s*\("))?;
        for (other, &(first, end)) in &unit.functions {
            for line in first.max(1)..=end.min(at) {
                let code = &unit.lines[line as usize - 1];
                if other != function
                    && call
                        .find_iter(code)
                        .any(|m| enclosing_inlined(&unit.lines, line, m.start()).is_none())
                {
                    problems.push(format!(
                        "{unit_name}:{line}: a call of {function} before its wrapper"
                    ));
                }
            }
        }
    }
    ensure!(
        problems.is_empty(),
        "wrappers missing:\n{}",
        problems.join("\n")
    );
    Ok(())
}

/// TABLE as the hooks' Rust source.
pub fn render(table: &Table) -> String {
    let units: Vec<&String> = table
        .sites
        .keys()
        .map(|w| &w.unit)
        .chain(table.none.iter().map(|(u, _)| u))
        .collect::<BTreeSet<_>>()
        .into_iter()
        .collect();
    let index = |unit: &String| units.iter().position(|u| *u == unit).unwrap();
    let mut out = String::new();
    out.push_str(
        "//! Generated by `mwcc-tools provenance counter-sites --write` (crates/mwcc-oracle-core/src/counter_sources.rs)\n\
         //! from gc2's sources and the original's increments: not to be edited.\n\n",
    );
    let _ = writeln!(out, "/// gc2's units with register counter writes.");
    let _ = writeln!(out, "pub const UNITS: [&str; {}] = [", units.len());
    for unit in &units {
        let _ = writeln!(out, "    {unit:?},");
    }
    out.push_str("];\n");
    let _ = writeln!(
        out,
        "/// An entry of SITES.\npub type Site = (u8, u16, u16, u32, u32, [u8; 3], u32, [u8; 3]);"
    );
    let _ = writeln!(
        out,
        "/// The writes an original has an increment for: (unit, line, the line of the call the original inlines it\n\
         /// at or 0, its site in GC 2.7, in GC 2.6, their object, operand and origin bindings, its site in GC 1.3.2 and its\n\
         /// bindings; a site is 0 where that version has no increment for it), in order."
    );
    // (one entry a line: wider than rustfmt keeps a tuple on one)
    let _ = writeln!(out, "#[rustfmt::skip]");
    let _ = writeln!(out, "pub static SITES: [Site; {}] = [", table.sites.len());
    let mut sites: Vec<_> = table
        .sites
        .iter()
        .map(|(w, s)| (index(&w.unit), w.line, w.inlined, *s))
        .collect();
    sites.sort_by_key(|s| (s.0, s.1, s.2));
    for (unit, line, inlined, s) in sites {
        let ((a, b, [object, operand, origin]), (c, [o2, p2, r2])) = (s.gc2_7, s.gc1_3_2);
        let _ = writeln!(
            out,
            "    ({unit}, {line}, {inlined}, {a:#x}, {b:#x}, [{object}, {operand}, {origin}], {c:#x}, [{o2}, {p2}, {r2}]),"
        );
    }
    out.push_str("];\n");
    let _ = writeln!(
        out,
        "/// The writes none has an increment for: (unit, line), in order."
    );
    let mut none: Vec<_> = table.none.iter().map(|(u, l)| (index(u), *l)).collect();
    none.sort();
    if none.is_empty() {
        let _ = writeln!(out, "pub const NONE: [(u8, u16); 0] = [];");
        return out;
    }
    let _ = writeln!(out, "pub const NONE: [(u8, u16); {}] = [", none.len());
    for (unit, line) in none {
        let _ = writeln!(out, "    ({unit}, {line}),");
    }
    out.push_str("];\n");
    out
}
