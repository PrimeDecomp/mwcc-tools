//! The strict level: mwdec's exact rules (its `compare.rs` and `placeholder.rs`, CC0). A function matches when:
//! - it has the target's size and instruction words once relocated fields are masked (a branch to a label inside the
//!   function compares as its displacement: a relocated one, as dtk emits for some asm, is made a plain branch
//!   first);
//! - it has the same relocations per instruction (the instruction and the kind: `EMB_SDA21`'s byte offset is +0 in
//!   dtk's objects and +2 in MWCC's, so offsets compare by word);
//! - each relocation's target is equivalent: the same name (and addend) for an ordinary symbol; for a name the
//!   compiler or the splitter made (`@123`, `@stringBase0`, `lbl_...`, `...data.0`, `fn_...`, `init$12`, a bare
//!   section) the same bytes (a string pool's: the NUL-terminated string at symbol + addend) in the same section,
//!   with equivalent relocations inside them (jump tables, pointer tables); a bss target only the same section; an
//!   unnamed function the same masked code, except a target placeholder (`fn_<address>`) against one of our named
//!   functions, which needs a proof by bytes ([`PlaceholderProver`]);
//! - the targets map one to one within the function (two of the target's literals are not one of ours, nor the
//!   reverse).
//!
//! Literals the target object only references (dtk split them into another unit, as the generated `.sdata2` and
//! `.rodata` ones) are read through an [`ExternIndex`] of the module's other target objects.
pub mod elf;

use elf::{Binding, Function, Object, Reloc, RelocKind, Section, SymbolDef};
use serde::Serialize;
use std::{
    cell::Cell,
    collections::HashMap,
    sync::{Arc, Mutex},
};

/// The global symbols a set of objects defines (a module's target objects), for the values of literals a target
/// object only references. An index can be layered on a shared base ([`ExternIndex::layered`]): a REL module's
/// reuses the main module's.
#[derive(Default)]
pub struct ExternIndex {
    /// Every object indexed: the base's first, then this layer's.
    pub objects: Vec<Arc<Object>>,
    /// Name: (object, symbol, weak); `None`: defined globally more than once (ambiguous).
    by_name: HashMap<String, Option<(u32, u32, bool)>>,
    /// Name: (object, function); `None`: ambiguous.
    functions: HashMap<String, Option<(u32, u32)>>,
    base: Option<Arc<ExternIndex>>,
}

impl ExternIndex {
    /// The non-local symbols of OBJECTS (a name defined in several is dropped as ambiguous).
    pub fn new(objects: Vec<Arc<Object>>) -> Self {
        Self::build(objects, None)
    }

    /// BASE's objects followed by EXTRA, looked up as [`ExternIndex::new`] of them all, without indexing BASE again.
    pub fn layered(base: Arc<ExternIndex>, extra: Vec<Arc<Object>>) -> Self {
        Self::build(extra, Some(base))
    }

    fn build(extra: Vec<Arc<Object>>, base: Option<Arc<ExternIndex>>) -> Self {
        let start = base.as_ref().map_or(0, |b| b.objects.len());
        let mut objects: Vec<Arc<Object>> =
            base.as_ref().map(|b| b.objects.clone()).unwrap_or_default();
        objects.extend(extra);
        // (weak duplicates, inline or template copies, are interchangeable: the first is kept; a name defined
        // globally in several objects is ambiguous)
        let mut by_name: HashMap<String, Option<(u32, u32, bool)>> = HashMap::new();
        let mut functions: HashMap<String, Option<(u32, u32)>> = HashMap::new();
        for (oi, o) in objects.iter().enumerate().skip(start) {
            for (si, s) in o.symbols.iter().enumerate() {
                if s.binding == Binding::Local {
                    continue;
                }
                let weak = s.binding == Binding::Weak;
                let here = Some((oi as u32, si as u32, weak));
                let prev = match by_name.get(&s.name) {
                    Some(v) => Some(*v),
                    None => base.as_ref().and_then(|b| b.raw_symbol(&s.name)),
                };
                let next = match prev {
                    None => here,
                    Some(Some((_, _, true))) if weak => continue,
                    Some(Some((_, _, true))) => here,
                    Some(_) if weak => continue,
                    Some(_) => None,
                };
                by_name.insert(s.name.clone(), next);
            }
            for (fi, f) in o.functions.iter().enumerate() {
                let here = Some((oi as u32, fi as u32));
                let prev = match functions.get(&f.name) {
                    Some(v) => Some(*v),
                    None => base.as_ref().and_then(|b| b.raw_function(&f.name)),
                };
                let next = match (f.binding, prev) {
                    (Binding::Local, _) => continue,
                    (Binding::Weak, Some(_)) => continue,
                    (Binding::Weak, None) => here,
                    (Binding::Global, Some(_)) => None,
                    (Binding::Global, None) => here,
                };
                functions.insert(f.name.clone(), next);
            }
        }
        by_name.shrink_to_fit();
        functions.shrink_to_fit();
        ExternIndex {
            objects,
            by_name,
            functions,
            base,
        }
    }

    fn raw_symbol(&self, name: &str) -> Option<Option<(u32, u32, bool)>> {
        match self.by_name.get(name) {
            Some(v) => Some(*v),
            None => self.base.as_ref()?.raw_symbol(name),
        }
    }

    fn raw_function(&self, name: &str) -> Option<Option<(u32, u32)>> {
        match self.functions.get(name) {
            Some(v) => Some(*v),
            None => self.base.as_ref()?.raw_function(name),
        }
    }

    /// A non-local function one of the objects defines.
    pub fn function(&self, name: &str) -> Option<&Function> {
        let (oi, fi) = self.raw_function(name)??;
        Some(&self.objects[oi as usize].functions[fi as usize])
    }

    /// The unambiguous names of this layer and its base.
    pub fn len(&self) -> usize {
        self.by_name.values().filter(|v| v.is_some()).count()
            + self.base.as_ref().map_or(0, |b| b.len())
    }

    pub fn is_empty(&self) -> bool {
        self.len() == 0
    }

    fn locate(&self, name: &str) -> Option<Loc<'_>> {
        let (oi, si, _) = self.raw_symbol(name)??;
        let o = &self.objects[oi as usize];
        let d = &o.symbols[si as usize];
        let sec = o.sections.iter().find(|s| s.name == d.section)?;
        Some((sec, d.address, d.size, Some(d)))
    }
}

/// The objects at PATHS, read in parallel (those missing or not ELF are left out).
pub fn load_objects(paths: &[std::path::PathBuf]) -> Vec<Arc<Object>> {
    let threads = std::thread::available_parallelism()
        .map_or(4, |n| n.get())
        .min(16);
    let chunk = paths.len().div_ceil(threads).max(1);
    std::thread::scope(|scope| {
        let parts: Vec<_> = paths
            .chunks(chunk)
            .map(|part| {
                scope.spawn(move || {
                    part.iter()
                        .filter_map(|p| Some(Arc::new(elf::load(&std::fs::read(p).ok()?).ok()?)))
                        .collect::<Vec<_>>()
                })
            })
            .collect();
        parts.into_iter().flat_map(|h| h.join().unwrap()).collect()
    })
}

/// Makes our definitions of functions a target placeholder (`fn_<address>`, a function dtk named by its address)
/// may be: a template instance, an inline function emitted out of line, a generated special member, compiled in the
/// unit's context.
pub trait PlaceholderProver: Send + Sync {
    /// An object that defines SYMBOL, if some source makes the compiler emit it.
    fn definition(&self, symbol: &str) -> Option<Arc<Object>>;
    /// The proofs made: (placeholder, our symbol): identical.
    fn memo(&self) -> &Mutex<HashMap<(String, String), bool>>;
}

/// How deep placeholder proofs nest (a proof's own relocations may need proofs).
pub const MAX_PROOF_DEPTH: u32 = 2;

/// The proofs a prover's memo keeps (it is cleared when full).
const MEMO_CAP: usize = 4096;

/// `fn_8004A1C0` (with dtk's `_XXXXXXXX` suffix or not): a function dtk named by its address.
pub fn is_placeholder(name: &str) -> bool {
    let Some(rest) = name.strip_prefix("fn_") else {
        return false;
    };
    let hex8 = |s: &str| s.len() == 8 && s.bytes().all(|c| c.is_ascii_hexdigit());
    match rest.split_once('_') {
        None => hex8(rest),
        Some((a, b)) => hex8(a) && hex8(b),
    }
}

thread_local! {
    static DEPTH: Cell<u32> = const { Cell::new(0) };
}

/// F, one proof deeper; `None` past [`MAX_PROOF_DEPTH`].
fn nested<R>(f: impl FnOnce() -> R) -> Option<R> {
    let d = DEPTH.with(|d| d.get());
    if d >= MAX_PROOF_DEPTH {
        return None;
    }
    DEPTH.with(|c| c.set(d + 1));
    let r = f();
    DEPTH.with(|c| c.set(d));
    Some(r)
}

fn remembered(p: &dyn PlaceholderProver, key: &(String, String)) -> Option<bool> {
    p.memo().lock().ok()?.get(key).copied()
}

fn remember(p: &dyn PlaceholderProver, key: (String, String), v: bool) {
    if let Ok(mut m) = p.memo().lock() {
        if m.len() >= MEMO_CAP {
            m.clear();
        }
        m.insert(key, v);
    }
}

/// An object's names, looked up once for many comparisons.
pub struct Index<'a> {
    pub object: &'a Object,
    by_name: HashMap<&'a str, &'a SymbolDef>,
    sections: HashMap<&'a str, &'a Section>,
    functions_by_section: HashMap<&'a str, Vec<&'a SymbolDef>>,
    functions: HashMap<&'a str, &'a Function>,
    externs: Option<&'a ExternIndex>,
    /// (ours only) Compiles the functions a target placeholder may be.
    prover: Option<&'a dyn PlaceholderProver>,
}

/// (section, address in it, declared size, symbol)
type Loc<'a> = (&'a Section, u32, u32, Option<&'a SymbolDef>);

impl<'a> Index<'a> {
    pub fn new(object: &'a Object) -> Self {
        let mut by_name = HashMap::new();
        let mut functions_by_section: HashMap<&str, Vec<&SymbolDef>> = HashMap::new();
        for s in &object.symbols {
            by_name.entry(s.name.as_str()).or_insert(s);
            if s.is_func {
                functions_by_section
                    .entry(s.section.as_str())
                    .or_default()
                    .push(s);
            }
        }
        for v in functions_by_section.values_mut() {
            v.sort_by_key(|s| s.address);
        }
        let sections = object
            .sections
            .iter()
            .map(|s| (s.name.as_str(), s))
            .collect();
        let functions = object
            .functions
            .iter()
            .map(|f| (f.name.as_str(), f))
            .collect();
        Index {
            object,
            by_name,
            sections,
            functions_by_section,
            functions,
            externs: None,
            prover: None,
        }
    }

    /// Symbols this object does not define, read through EXTERNS (the target's side).
    pub fn with_externs(mut self, externs: Option<&'a ExternIndex>) -> Self {
        self.externs = externs;
        self
    }

    /// Target placeholders proven against our named functions through PROVER (our side).
    pub fn with_prover(mut self, prover: Option<&'a dyn PlaceholderProver>) -> Self {
        self.prover = prover;
        self
    }

    pub fn function(&self, name: &str) -> Option<&'a Function> {
        self.functions.get(name).copied()
    }

    /// A defined symbol, a bare section name, or (through the extern index) a symbol another object defines.
    fn locate(&self, name: &str) -> Option<Loc<'a>> {
        if let Some(d) = self.by_name.get(name) {
            let sec = self.sections.get(d.section.as_str())?;
            return Some((sec, d.address, d.size, Some(d)));
        }
        if let Some(s) = self.sections.get(name) {
            return Some((*s, 0, s.size, None));
        }
        self.externs.and_then(|e| e.locate(name))
    }

    /// When NAME + ADDEND is in this object's code: the function that contains it, and the offset in it.
    fn code_location(&self, name: &str, addend: i64) -> Option<(&'a str, i64)> {
        let d = self.by_name.get(name).copied();
        let (sec, addr) = match d {
            Some(d) => (*self.sections.get(d.section.as_str())?, d.address),
            None => (*self.sections.get(name)?, 0),
        };
        if !sec.executable {
            return None;
        }
        let at = addr as i64 + addend;
        let functions = self.functions_by_section.get(sec.name.as_str())?;
        let i = functions.partition_point(|f| (f.address as i64) <= at);
        let f = functions.get(i.checked_sub(1)?)?;
        if at < f.address as i64 + f.size.max(1) as i64 {
            Some((f.name.as_str(), at - f.address as i64))
        } else {
            None
        }
    }

    /// The function that contains NAME + ADDEND (here, or another object's through the extern index).
    fn function_view(&self, name: &str, addend: i64) -> Option<(&'a Function, i64)> {
        if let Some((f, off)) = self.code_location(name, addend) {
            return Some((self.function(f)?, off));
        }
        if self.by_name.contains_key(name) {
            return None;
        }
        Some((self.externs?.function(name)?, addend))
    }

    /// A relocation target's identity, for the one-to-one mapping.
    fn canonical(&self, name: &str, addend: i64) -> (String, i64) {
        match self.code_location(name, addend) {
            Some((f, off)) => (strip_dtk_suffix(f).to_string(), off),
            None => (strip_dtk_suffix(name).to_string(), addend),
        }
    }
}

/// A name the compiler or the splitter made: its equality says nothing of the value.
pub fn is_generated_name(n: &str) -> bool {
    n.starts_with('@')
        || n.starts_with("lbl_")
        || n.starts_with("fn_")
        || n.starts_with("...")
        || n.starts_with('.')
        || n.starts_with("jumptable_")
        || n.starts_with("gap_")
        || n.starts_with("switch_")
        || has_numbered_suffix(n)
}

/// `name$123` (a function's static, a compiler helper), with dtk's `_80412345` suffix or not.
fn has_numbered_suffix(n: &str) -> bool {
    let n = strip_dtk_suffix(n);
    match n.rfind('$') {
        Some(i) => i + 1 < n.len() && n[i + 1..].bytes().all(|b| b.is_ascii_digit()),
        None => false,
    }
}

/// The name without the `_XXXXXXXX` (address) dtk appends to tell duplicate local names apart.
fn strip_dtk_suffix(n: &str) -> &str {
    if n.len() > 9 {
        let (a, b) = n.split_at(n.len() - 9);
        let generated_base = matches!(a, "lbl" | "fn" | "func" | "jumptable" | "switch" | "gap");
        if !generated_base
            && b.starts_with('_')
            && b[1..]
                .bytes()
                .all(|c| c.is_ascii_hexdigit() && !c.is_ascii_lowercase())
        {
            return a;
        }
    }
    n
}

fn is_string_pool(n: &str) -> bool {
    n.starts_with("@stringBase")
}

fn is_wstring_pool(n: &str) -> bool {
    n.starts_with("@wstringBase")
}

/// The bits of an instruction a relocation of KIND covers.
pub fn reloc_mask(kind: RelocKind) -> u32 {
    match kind {
        RelocKind::Addr16Lo | RelocKind::Addr16Hi | RelocKind::Addr16Ha => 0x0000_ffff,
        // (rA, r0/r2/r13 as the linker chooses, and a 16-bit displacement)
        RelocKind::EmbSda21 => 0x001f_ffff,
        RelocKind::Rel24 => 0x03ff_fffc,
        RelocKind::Rel14 => 0x0000_fffc,
        RelocKind::Addr32 | RelocKind::Other(_) => 0xffff_ffff,
    }
}

/// F's instruction words, their relocated fields zeroed.
pub fn masked_words(f: &Function) -> Vec<u32> {
    masked(&f.words().collect::<Vec<_>>(), &f.relocs)
}

fn masked(words: &[u32], relocs: &[Reloc]) -> Vec<u32> {
    let mut w = words.to_vec();
    for r in relocs {
        let i = (r.offset / 4) as usize;
        if i < w.len() {
            w[i] &= !reloc_mask(r.kind);
        }
    }
    w
}

/// F's words and relocations, its relocated branches to labels inside itself made plain branches.
fn internalize(idx: &Index, f: &Function) -> (Vec<u32>, Vec<Reloc>) {
    let mut words: Vec<u32> = f.words().collect();
    let mut relocs = Vec::with_capacity(f.relocs.len());
    for r in &f.relocs {
        if matches!(r.kind, RelocKind::Rel24 | RelocKind::Rel14)
            && let Some((name, dest)) = idx.code_location(&r.target, r.addend)
        {
            let i = (r.offset / 4) as usize;
            if name == f.name && i < words.len() {
                let disp = (dest - (i as i64) * 4) as u32;
                let m = reloc_mask(r.kind);
                words[i] = (words[i] & !m) | (disp & m);
                continue;
            }
        }
        relocs.push(r.clone());
    }
    (words, relocs)
}

/// Why a function does not match, the most fundamental first.
#[derive(Clone, Copy, Debug, PartialEq, Eq, PartialOrd, Ord, Hash, Serialize)]
#[serde(rename_all = "kebab-case")]
pub enum DiffClass {
    Exact,
    /// The sizes differ.
    Size,
    /// The masked instruction words differ.
    Code,
    /// The relocations' positions or kinds differ.
    RelocLayout,
    /// A relocation targets a differently named (not generated) symbol, or another addend.
    TargetName,
    /// A generated target's bytes could not be read on a side (the comparator's limit, or another kind of target).
    Unresolved,
    /// Generated targets read on both sides, with different bytes or sections, or not mapped one to one.
    Literal,
}

impl DiffClass {
    pub fn label(self) -> &'static str {
        match self {
            DiffClass::Exact => "exact",
            DiffClass::Size => "size",
            DiffClass::Code => "code",
            DiffClass::RelocLayout => "reloc-layout",
            DiffClass::TargetName => "target-name",
            DiffClass::Unresolved => "unresolved",
            DiffClass::Literal => "literal",
        }
    }
}

/// A function compared at the strict level.
#[derive(Clone, Debug, Serialize)]
pub struct Strict {
    /// It matches (no class).
    pub exact: bool,
    /// The most fundamental difference (`exact` when it matches).
    pub class: DiffClass,
    /// Every difference seen, the most fundamental first.
    pub classes: Vec<DiffClass>,
    /// 0 to 100: the masked words' similarity, weighted by the relocations that agree (100 only when it matches).
    pub score: f64,
    /// The first differences, described (at most 12).
    pub notes: Vec<String>,
}

enum TEq {
    Same,
    Diff(DiffClass, String),
}

fn hex(b: &[u8]) -> String {
    let mut s: String = b.iter().take(16).map(|x| format!("{x:02x}")).collect();
    if b.len() > 16 {
        s.push_str("..");
    }
    s
}

fn fmt_target(n: &str, a: i64) -> String {
    match a {
        0 => n.to_string(),
        a if a > 0 => format!("{n}+0x{a:x}"),
        a => format!("{n}-0x{:x}", -a),
    }
}

fn read(sec: &Section, start: i64, len: usize) -> Option<&[u8]> {
    if start < 0 || sec.bytes.is_empty() {
        return None;
    }
    let s = start as usize;
    let e = s.checked_add(len)?;
    sec.bytes.get(s..e)
}

/// The NUL-terminated string at START (of WIDTH-byte units), without its terminator.
fn c_str(sec: &Section, start: i64, width: usize) -> Option<&[u8]> {
    if start < 0 {
        return None;
    }
    let b = sec.bytes.get(start as usize..)?;
    let n = b
        .chunks_exact(width)
        .position(|c| c.iter().all(|&x| x == 0))?;
    Some(&b[..n * width])
}

fn relocs_in(sec: &Section, start: i64, len: usize) -> Vec<&Reloc> {
    let lo = sec.relocs.partition_point(|r| (r.offset as i64) < start);
    sec.relocs[lo..]
        .iter()
        .take_while(|r| (r.offset as i64) < start + len as i64)
        .collect()
}

fn targets_equiv(t: &Index, tn: &str, ta: i64, o: &Index, on: &str, oa: i64, depth: u32) -> TEq {
    let desc = || {
        format!(
            "target {} vs ours {}",
            fmt_target(tn, ta),
            fmt_target(on, oa)
        )
    };
    let generated = is_generated_name(tn) || is_generated_name(on);
    // (code compares as a function and an offset; an unnamed function, `fn_...`, by its masked code)
    match (t.function_view(tn, ta), o.function_view(on, oa)) {
        (Some((a, to)), Some((b, oo))) => {
            if to != oo {
                return TEq::Diff(DiffClass::TargetName, format!("code offset {}", desc()));
            }
            if strip_dtk_suffix(&a.name) == strip_dtk_suffix(&b.name) {
                return TEq::Same;
            }
            // (a target placeholder against one of our named functions: proven by compiling ours)
            if is_placeholder(strip_dtk_suffix(&a.name)) && !is_generated_name(&b.name) {
                return if prove_placeholder(t, a, o, Some(b), &b.name) {
                    TEq::Same
                } else {
                    TEq::Diff(
                        DiffClass::TargetName,
                        format!("placeholder not proven: {}", desc()),
                    )
                };
            }
            if is_generated_name(&a.name) || is_generated_name(&b.name) {
                let same = a.code.len() == b.code.len()
                    && masked_words(a) == masked_words(b)
                    && a.relocs.len() == b.relocs.len()
                    && a.relocs
                        .iter()
                        .zip(&b.relocs)
                        .all(|(x, y)| x.offset / 4 == y.offset / 4 && x.kind == y.kind);
                return if same {
                    TEq::Same
                } else {
                    TEq::Diff(
                        DiffClass::Literal,
                        format!("unnamed function code differs: {}", desc()),
                    )
                };
            }
            return TEq::Diff(DiffClass::TargetName, format!("code {}", desc()));
        }
        (Some((a, 0)), None)
            if oa == 0
                && is_placeholder(strip_dtk_suffix(&a.name))
                && !is_generated_name(on)
                && o.prover.is_some() =>
        {
            return if prove_placeholder(t, a, o, None, on) {
                TEq::Same
            } else {
                TEq::Diff(
                    DiffClass::TargetName,
                    format!("placeholder not proven: {}", desc()),
                )
            };
        }
        (Some(_), None) | (None, Some(_))
            if generated && strip_dtk_suffix(tn) != strip_dtk_suffix(on) =>
        {
            return TEq::Diff(
                DiffClass::Unresolved,
                format!(
                    "unnamed function vs function not defined on the other side: {}",
                    desc()
                ),
            );
        }
        _ => {}
    }
    if !generated && strip_dtk_suffix(tn) == strip_dtk_suffix(on) {
        return if ta == oa {
            TEq::Same
        } else {
            TEq::Diff(DiffClass::TargetName, format!("addend {}", desc()))
        };
    }
    let tl = t.locate(tn);
    let ol = o.locate(on);
    let (Some((tsec, taddr, tsize, tdef)), Some((osec, oaddr, osize, odef))) = (tl, ol) else {
        if tn == on && ta == oa {
            // (the same generated name, undefined on a side: nothing more to check)
            return TEq::Same;
        }
        return if generated && (tl.is_some() || ol.is_some()) {
            TEq::Diff(
                DiffClass::Unresolved,
                format!("undefined on one side: {}", desc()),
            )
        } else {
            TEq::Diff(DiffClass::TargetName, desc())
        };
    };
    // (both defined: compared by value only when a side is generated, or both are local with the same name but for
    // dtk's suffix)
    let local = |d: Option<&SymbolDef>| d.is_none_or(|d| d.binding == Binding::Local);
    let same_local = local(tdef) && local(odef) && strip_dtk_suffix(tn) == strip_dtk_suffix(on);
    if !generated && !same_local {
        return TEq::Diff(DiffClass::TargetName, desc());
    }
    if tsec.name != osec.name {
        return TEq::Diff(
            DiffClass::Literal,
            format!("section {} vs {}: {}", tsec.name, osec.name, desc()),
        );
    }
    let tstart = taddr as i64 + ta;
    let ostart = oaddr as i64 + oa;
    for (pool, width) in [
        (is_string_pool as fn(&str) -> bool, 1usize),
        (is_wstring_pool, 2),
    ] {
        if pool(tn) || pool(on) {
            return match (c_str(tsec, tstart, width), c_str(osec, ostart, width)) {
                (Some(a), Some(b)) if a == b => TEq::Same,
                (Some(a), Some(b)) if width == 1 => TEq::Diff(
                    DiffClass::Literal,
                    format!(
                        "string {:?} vs {:?}: {}",
                        String::from_utf8_lossy(a),
                        String::from_utf8_lossy(b),
                        desc()
                    ),
                ),
                (Some(a), Some(b)) => TEq::Diff(
                    DiffClass::Literal,
                    format!("wide string {} vs {}: {}", hex(a), hex(b), desc()),
                ),
                _ => TEq::Diff(DiffClass::Unresolved, format!("no string: {}", desc())),
            };
        }
    }
    if tsec.bytes.is_empty() || osec.bytes.is_empty() {
        // (bss-like: no values; the same section is enough, as dtk's sizes of unnamed bss are guesses, and the
        // one-to-one mapping catches swapped variables)
        return if tsec.bytes.is_empty() && osec.bytes.is_empty() {
            TEq::Same
        } else {
            TEq::Diff(DiffClass::Literal, format!("bss vs data: {}", desc()))
        };
    }
    // (the referenced object's extent: ours, as MWCC sizes literals exactly, else the target's)
    let len = if (osize as i64) > oa {
        (osize as i64 - oa) as usize
    } else if (tsize as i64) > ta {
        (tsize as i64 - ta) as usize
    } else {
        4
    };
    let (Some(tb), Some(ob)) = (read(tsec, tstart, len), read(osec, ostart, len)) else {
        return TEq::Diff(
            DiffClass::Unresolved,
            format!("cannot read {len} bytes: {}", desc()),
        );
    };
    if tb != ob {
        return TEq::Diff(
            DiffClass::Literal,
            format!("bytes {} vs {}: {}", hex(tb), hex(ob), desc()),
        );
    }
    // (relocations inside the data, jump tables and pointer tables: resolved in the objects that hold it)
    let trs = relocs_in(tsec, tstart, len);
    let ors = relocs_in(osec, ostart, len);
    if trs.len() != ors.len() {
        return TEq::Diff(
            DiffClass::Literal,
            format!("data relocs {} vs {}: {}", trs.len(), ors.len(), desc()),
        );
    }
    for (a, b) in trs.iter().zip(&ors) {
        if a.offset as i64 - tstart != b.offset as i64 - ostart || a.kind != b.kind {
            return TEq::Diff(DiffClass::Literal, format!("data reloc layout: {}", desc()));
        }
        if depth < 2 {
            if let TEq::Diff(c, m) =
                targets_equiv(t, &a.target, a.addend, o, &b.target, b.addend, depth + 1)
            {
                return TEq::Diff(c, format!("in {}: {m}", desc()));
            }
        } else if a.target != b.target || a.addend != b.addend {
            return TEq::Diff(
                DiffClass::Unresolved,
                format!("nested reloc too deep: {}", desc()),
            );
        }
    }
    TEq::Same
}

/// Whether the target placeholder TF is our function SYMBOL: ours is LOCAL (in our object) or the prover's, and the
/// two match at the strict level.
fn prove_placeholder(
    t: &Index,
    tf: &Function,
    o: &Index,
    local: Option<&Function>,
    symbol: &str,
) -> bool {
    let key = (strip_dtk_suffix(&tf.name).to_string(), symbol.to_string());
    if let Some(p) = o.prover
        && let Some(v) = remembered(p, &key)
    {
        return v;
    }
    let verdict = match local {
        Some(of) => nested(|| compare(t, tf, o, of).exact).unwrap_or(false),
        None => {
            let Some(p) = o.prover else {
                return false;
            };
            let Some(object) = p.definition(symbol) else {
                remember(p, key, false);
                return false;
            };
            let Some(of) = object.function(symbol) else {
                return false;
            };
            let mut oi = Index::new(&object).with_prover(Some(p));
            oi.externs = o.externs;
            nested(|| compare(t, tf, &oi, of).exact).unwrap_or(false)
        }
    };
    if let Some(p) = o.prover {
        remember(p, key, verdict);
    }
    verdict
}

/// The longest common subsequence's length (two rows; by position for huge inputs).
fn lcs(a: &[u32], b: &[u32]) -> usize {
    if a.is_empty() || b.is_empty() {
        return 0;
    }
    if a.len() * b.len() > 16_000_000 {
        return a.iter().zip(b).filter(|(x, y)| x == y).count();
    }
    let mut prev = vec![0u32; b.len() + 1];
    let mut cur = vec![0u32; b.len() + 1];
    for &x in a {
        for (j, &y) in b.iter().enumerate() {
            cur[j + 1] = if x == y {
                prev[j] + 1
            } else {
                cur[j].max(prev[j + 1])
            };
        }
        std::mem::swap(&mut prev, &mut cur);
    }
    prev[b.len()] as usize
}

const MAX_NOTES: usize = 12;

fn note(notes: &mut Vec<String>, s: String) {
    if notes.len() < MAX_NOTES {
        notes.push(s);
    }
}

/// Our function OF against the target's TF, at the strict level.
pub fn compare(t: &Index, tf: &Function, o: &Index, of: &Function) -> Strict {
    let mut notes = Vec::new();
    let mut classes = Vec::new();
    let (traw, trel) = internalize(t, tf);
    let (oraw, orel) = internalize(o, of);
    let tw = masked(&traw, &trel);
    let ow = masked(&oraw, &orel);
    if tf.code.len() != of.code.len() {
        classes.push(DiffClass::Size);
        note(
            &mut notes,
            format!(
                "size: target {:#x} vs ours {:#x}",
                tf.code.len(),
                of.code.len()
            ),
        );
    }
    let mut code_diffs = 0usize;
    for (i, (a, b)) in tw.iter().zip(&ow).enumerate() {
        if a != b {
            code_diffs += 1;
            if code_diffs == 1 {
                classes.push(DiffClass::Code);
            }
            if code_diffs <= 4 {
                let off = (i * 4) as u32;
                note(
                    &mut notes,
                    format!(
                        "code @{off:#x}: target `{}` vs ours `{}`",
                        elf::disassemble_word(traw[i], off),
                        elf::disassemble_word(oraw[i], off)
                    ),
                );
            }
        }
    }
    if code_diffs > 4 {
        note(
            &mut notes,
            format!("... {code_diffs} differing instructions in total"),
        );
    }
    // (the relocations, instruction by instruction)
    let mut reloc_ok = 0usize;
    let nrel = trel.len().max(orel.len());
    let mut layout_bad = trel.len() != orel.len();
    let mut fwd: HashMap<(String, i64), (String, i64)> = HashMap::new();
    let mut rev: HashMap<(String, i64), (String, i64)> = HashMap::new();
    for (a, b) in trel.iter().zip(&orel) {
        if a.offset / 4 != b.offset / 4 || a.kind != b.kind {
            note(
                &mut notes,
                format!(
                    "reloc layout: target @{:#x} {:?} {} vs ours @{:#x} {:?} {}",
                    a.offset, a.kind, a.target, b.offset, b.kind, b.target
                ),
            );
            layout_bad = true;
            continue;
        }
        let ins = a.offset & !3;
        // (targets map one to one within the function)
        let tk = t.canonical(&a.target, a.addend);
        let ok = o.canonical(&b.target, b.addend);
        let f = fwd.entry(tk.clone()).or_insert_with(|| ok.clone()).clone();
        let r = rev.entry(ok.clone()).or_insert_with(|| tk.clone()).clone();
        if f != ok || r != tk {
            classes.push(DiffClass::Literal);
            note(
                &mut notes,
                format!(
                    "reloc @{ins:#x}: mapping not one-to-one: target {} vs ours {} (elsewhere {} <-> {})",
                    fmt_target(&tk.0, tk.1),
                    fmt_target(&ok.0, ok.1),
                    fmt_target(&r.0, r.1),
                    fmt_target(&f.0, f.1)
                ),
            );
            continue;
        }
        match targets_equiv(t, &a.target, a.addend, o, &b.target, b.addend, 0) {
            TEq::Same => reloc_ok += 1,
            TEq::Diff(c, m) => {
                classes.push(c);
                note(&mut notes, format!("reloc @{ins:#x} {:?}: {m}", a.kind));
            }
        }
    }
    if layout_bad {
        classes.push(DiffClass::RelocLayout);
        if trel.len() != orel.len() {
            note(
                &mut notes,
                format!("reloc count: target {} vs ours {}", trel.len(), orel.len()),
            );
        }
    }
    classes.sort();
    classes.dedup();
    let exact = classes.is_empty();
    let score = if exact {
        100.0
    } else {
        let l = lcs(&tw, &ow) as f64;
        let base = 2.0 * l / (tw.len() + ow.len()).max(1) as f64;
        let rel = if nrel == 0 {
            1.0
        } else {
            reloc_ok as f64 / nrel as f64
        };
        (100.0 * base * (0.9 + 0.1 * rel)).min(99.9)
    };
    Strict {
        exact,
        class: classes.first().copied().unwrap_or(DiffClass::Exact),
        classes,
        score,
        notes,
    }
}
