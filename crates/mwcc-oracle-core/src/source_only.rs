//! The compiler made of a source tree's sources alone (crate::source_tree): `build/<version>/<tree>/source-only/
//! mwcceppc.exe`, every function as its source compiles, nothing of the original's (gc2's `tools/equivalent.py
//! --source-only`, ported).
//!
//! What is linked, with the linker the original was linked with: the linker's runtime objects, every source's
//! object (the tree's `config/sources.json`, in its order), import libraries made from the tree's `src/imports/*.def`
//! and resources compiled from its `src/driver/mwcceppc.rc`. Before the link the objects are patched:
//! - a COMDAT section the compiler marks "no duplicates" is made "any" (the license thunks of `LicenseImports.c` are
//!   the import library's too);
//! - a function or variable two objects define outside a COMDAT is kept in the object of the function's row (the
//!   tree's `functions.json`) and made undefined in the others;
//! - a name nothing defines is an error, unless only functions the original does not have refer to it (its linker
//!   dropped them): those references are dropped and the functions trap (`ud2`).
//!
//! What it lacks of the original's is what a build from the sources alone lacks: `mwcc-tools corpus --exe` compares
//! it with the original.
use crate::{
    coff::{self, Object},
    link,
    pe::{self, Resource, ResourceName},
    source_tree::SourceTree,
};
use anyhow::{Context, Result, bail, ensure};
use regex::Regex;
use serde_json::Value;
use std::{
    collections::{BTreeMap, BTreeSet, HashMap},
    fs,
    path::Path,
    time::Duration,
};

/// The linker's runtime objects the compiler links (the original's, with thread-local storage's), in the tree's
/// `lib/`.
pub const RUNTIME_OBJECTS: [&str; 3] = [
    "lib/runtime/cexc.obj",
    "lib/runtime/ccinit.obj",
    "lib/runtime/ctls.obj",
];
/// The DLLs the compiler imports from: one `src/imports/<name>.def` each.
pub const IMPORT_LIBRARIES: [&str; 3] = ["kernel32", "lmgr326b", "version"];
pub const RESOURCES: &str = "src/driver/mwcceppc.rc";
/// The FlexLM license library the compiler loads: the tree's stand-in, put beside it.
pub const LICENSING: &str = "tools/lmgr_stub/lmgr326b.dll";

const EXTERNAL: u8 = 2;
const STATIC: u8 = 3;
const FUNCTION_TYPE: u16 = 0x20;
const LINK_COMDAT: u32 = 0x1000;
const UD2: [u8; 2] = [0x0f, 0x0b];

pub struct Outcome {
    pub output: String,
    pub objects: usize,
    pub comdats: usize,
    pub duplicates: usize,
    pub trapped: Vec<String>,
}

/// Where VERSION's source-only compiler is linked.
pub fn output(tree: &SourceTree) -> String {
    format!("{}/source-only/mwcceppc.exe", tree.build_dir)
}

/// The tree VERSION builds its source-only compiler from: a vendored one whose own version VERSION is (1.3.2 builds
/// 2.7's sources, so its source-only compiler would be 2.7's).
pub fn tree(root: &Path, version: &str) -> Result<Option<SourceTree>> {
    let tree = SourceTree::of(root, version, false)?;
    let config: Value = serde_json::from_slice(&fs::read(
        root.join(format!("config/{version}/config.json")),
    )?)?;
    Ok((tree.vendored() && config["source_version"].is_null()).then_some(tree))
}

/// Link VERSION's source-only compiler (its sources compiled beforehand: ninja all_source).
pub fn run(root: &Path, version: &str) -> Result<Outcome> {
    let tree = tree(root, version)?.with_context(|| {
        format!("{version} has no source tree of its own to build a source-only compiler from")
    })?;
    let prefix = &tree.prefix;
    let read = |path: &str| fs::read(root.join(path)).with_context(|| format!("read {path}"));
    let sources = tree.sources(root)?;
    let sources = sources
        .as_object()
        .context("the tree's sources.json is not an object")?;
    let mut objects = vec![];
    for source in sources.keys() {
        let path = format!("{}/compiled/{source}.obj", tree.build_dir);
        objects.push((source.clone(), read(&path)?));
    }
    let runtime = RUNTIME_OBJECTS
        .iter()
        .map(|p| Ok((format!("{prefix}{p}"), read(&format!("{prefix}{p}"))?)))
        .collect::<Result<Vec<_>>>()?;
    let rows: Vec<Value> =
        serde_json::from_slice(&read(&format!("{}/functions.json", tree.config_dir))?)?;
    let bindings: BTreeMap<String, Value> =
        serde_json::from_slice(&read(&format!("{}/bindings.json", tree.config_dir))?)?;
    let mut libraries = vec![];
    let mut thunks = BTreeSet::new();
    for name in IMPORT_LIBRARIES {
        let text = String::from_utf8(read(&format!("{prefix}src/imports/{name}.def"))?)?;
        let (dll, exports) = coff::definition(&text)?;
        thunks.extend(exports.iter().map(|e| format!("_{}", e.symbol)));
        libraries.push((name, coff::import_library_of(&dll, &exports, true)?));
    }
    let rows = rows
        .iter()
        .map(|r| {
            Ok(Row {
                name: r["name"].as_str().context("a row without a name")?.into(),
                source: r["source"].as_str().map(Into::into),
            })
        })
        .collect::<Result<Vec<_>>>()?;
    let patched = patch(
        &mut objects,
        &runtime,
        &rows,
        bindings.keys().map(String::as_str),
        &thunks,
    )?;
    let resources = pe::res_file(&compile_resources(
        &String::from_utf8(read(&format!("{prefix}{RESOURCES}"))?)?,
        &String::from_utf8(read(&format!("{prefix}include/version.h"))?)?,
        tree.version_define(root, version)?
            .context("the tree has no number for the version")?,
        &read(&format!(
            "{prefix}{}",
            RESOURCES.replace("mwcceppc.rc", "mwcceppc.ico")
        ))?,
    )?)?;
    let output = output(&tree);
    let work = format!("{}/source-only/link", tree.build_dir);
    let work_path = root.join(&work);
    if work_path.exists() {
        fs::remove_dir_all(&work_path)?;
    }
    fs::create_dir_all(&work_path)?;
    let mut inputs: Vec<String> = runtime.iter().map(|(p, _)| p.clone()).collect();
    let mut listing = String::new();
    for (source, data) in &objects {
        let name: String = source
            .chars()
            .map(|c| {
                if c.is_alphanumeric() || "._-".contains(c) {
                    c
                } else {
                    '_'
                }
            })
            .collect();
        let path = format!("{work}/{name}.obj");
        fs::write(root.join(&path), data)?;
        listing += &format!("{path}\n");
    }
    for (name, data) in &libraries {
        let path = format!("{work}/{name}.lib");
        fs::write(root.join(&path), data)?;
        listing += &format!("{path}\n");
    }
    fs::write(root.join(format!("{work}/inputs.txt")), listing)?;
    fs::write(root.join(format!("{work}/resources.res")), &resources)?;
    inputs.push(format!("@{work}/inputs.txt"));
    inputs.push(format!("{work}/resources.res"));
    let (code, text) = link::mwld(root, &inputs, &output, Duration::from_secs(600))?;
    if code != Some(0) || !root.join(&output).is_file() {
        let _ = fs::remove_file(root.join(&output));
        bail!("the link fails:\n{text}");
    }
    let output_path = root.join(&output);
    fs::copy(
        root.join(format!("{prefix}{LICENSING}")),
        output_path.with_file_name("lmgr326b.dll"),
    )?;
    Ok(Outcome {
        output,
        objects: objects.len(),
        comdats: patched.comdats,
        duplicates: patched.duplicates,
        trapped: patched.trapped,
    })
}

// ------------------------------------------------------------------------------------------------ the objects

pub struct Row {
    pub name: String,
    pub source: Option<String>,
}

#[derive(Default)]
pub struct Patched {
    pub comdats: usize,
    pub duplicates: usize,
    /// The functions made to trap: "source: name".
    pub trapped: Vec<String>,
}

/// A function or variable's linker name: a mangled name as it is, a C one with its underscore (and a __stdcall
/// function's `@bytes`, from DECORATED).
fn linker_name(name: &str, decorated: &HashMap<String, String>) -> String {
    if name.starts_with('?') {
        return name.into();
    }
    let plain = format!("_{name}");
    decorated.get(&plain).cloned().unwrap_or(plain)
}

fn put_u16(data: &mut [u8], at: usize, value: u16) {
    data[at..at + 2].copy_from_slice(&value.to_le_bytes());
}
fn put_u32(data: &mut [u8], at: usize, value: u32) {
    data[at..at + 4].copy_from_slice(&value.to_le_bytes());
}

/// The numbers (from 1) of OBJECT's COMDAT sections: those whose section symbol's auxiliary record has a selection.
fn comdat_sections(object: &Object) -> BTreeSet<i16> {
    object
        .symbols
        .values()
        .filter(|s| {
            s.storage == STATIC
                && s.section > 0
                && object
                    .sections
                    .get(s.section as usize - 1)
                    .is_some_and(|section| {
                        section.name == s.name && section.flags & LINK_COMDAT != 0
                    })
                && s.auxiliary.first().is_some_and(|a| a[14] != 0)
        })
        .map(|s| s.section)
        .collect()
}

/// Patch OBJECTS ([(source, object)]) in place for the link with RUNTIME (see the module's description). ROWS are
/// the original's functions, BINDINGS the names of its variables (and decorated functions), THUNKS the linker names
/// of the functions the import libraries define.
pub fn patch<'a>(
    objects: &mut [(String, Vec<u8>)],
    runtime: &[(String, Vec<u8>)],
    rows: &[Row],
    bindings: impl Iterator<Item = &'a str>,
    thunks: &BTreeSet<String>,
) -> Result<Patched> {
    let mut patched = Patched::default();
    let mut errors = vec![];
    // (the linker names of __stdcall functions, _name@bytes: from the bindings and the objects)
    let decoration = Regex::new(r"^(?:__imp_)?(_[A-Za-z_]\w*)@(\d+)$")?;
    let mut decorated = HashMap::new();
    let mut names: Vec<String> = bindings.map(Into::into).collect();
    for (_, data) in objects.iter() {
        names.extend(Object::parse(data)?.symbols.into_values().map(|s| s.name));
    }
    for name in &names {
        if let Some(m) = decoration.captures(name) {
            decorated
                .entry(m[1].to_owned())
                .or_insert_with(|| format!("{}@{}", &m[1], &m[2]));
        }
    }
    let source_of: HashMap<String, Option<&str>> = rows
        .iter()
        .map(|r| (linker_name(&r.name, &decorated), r.source.as_deref()))
        .collect();
    // (a function or variable two objects define outside a COMDAT: the row's source's copy)
    let mut definers: BTreeMap<String, Vec<usize>> = BTreeMap::new();
    for (index, (_, data)) in objects.iter().enumerate() {
        let object = Object::parse(data)?;
        let comdat = comdat_sections(&object);
        for s in object.symbols.values() {
            if s.storage == EXTERNAL && s.section > 0 && !comdat.contains(&s.section) {
                definers.entry(s.name.clone()).or_default().push(index);
            }
        }
    }
    let mut undefine: BTreeMap<usize, BTreeSet<String>> = BTreeMap::new();
    for (name, found) in &definers {
        if found.len() < 2 {
            continue;
        }
        let owner = source_of.get(name).copied().flatten();
        match found
            .iter()
            .find(|i| Some(objects[**i].0.as_str()) == owner)
        {
            Some(keep) => {
                for i in found.iter().filter(|i| *i != keep) {
                    undefine.entry(*i).or_default().insert(name.clone());
                }
            }
            None => errors.push(format!(
                "{name} is defined by {}: which is the original's?",
                found
                    .iter()
                    .map(|i| objects[*i].0.as_str())
                    .collect::<Vec<_>>()
                    .join(", ")
            )),
        }
    }
    for (index, (_, data)) in objects.iter_mut().enumerate() {
        let object = Object::parse(data)?;
        let mut writes = vec![];
        for s in object.symbols.values() {
            if s.storage == EXTERNAL
                && s.section > 0
                && undefine.get(&index).is_some_and(|n| n.contains(&s.name))
            {
                writes.push(s.offset);
            }
        }
        let comdats: Vec<_> = object
            .symbols
            .values()
            .filter(|s| {
                comdat_sections(&object).contains(&s.section)
                    && s.storage == STATIC
                    && s.auxiliary.first().is_some_and(|a| a[14] == 1)
            })
            .map(|s| s.offset)
            .collect();
        drop(object);
        for at in writes {
            // (the section number and value: undefined)
            put_u32(data, at + 8, 0);
            put_u16(data, at + 12, 0);
            patched.duplicates += 1;
        }
        for at in comdats {
            // (the selection, in the auxiliary record: IMAGE_COMDAT_SELECT_ANY)
            data[at + 18 + 14] = 2;
            patched.comdats += 1;
        }
    }
    // (what nothing defines)
    let mut defined = BTreeSet::new();
    let mut referenced = BTreeSet::new();
    for (_, data) in objects.iter().chain(runtime) {
        for s in Object::parse(data)?.symbols.into_values() {
            if s.storage == EXTERNAL && (s.section > 0 || s.value > 0) {
                defined.insert(s.name);
            } else if s.storage == EXTERNAL && s.section == 0 {
                referenced.insert(s.name);
            }
        }
    }
    let missing: BTreeSet<_> = referenced
        .into_iter()
        .filter(|n| !defined.contains(n) && !n.starts_with("__imp_") && !thunks.contains(n))
        .collect();
    // (references only functions the original does not have make: dropped, and those functions made to trap)
    for (source, data) in objects.iter_mut() {
        let object = Object::parse(data)?;
        let mut functions: Vec<(i16, u32, &str)> = object
            .symbols
            .values()
            .filter(|s| {
                s.section > 0
                    && s.kind & FUNCTION_TYPE != 0
                    && (s.storage == EXTERNAL || s.storage == STATIC)
            })
            .map(|s| (s.section, s.value, s.name.as_str()))
            .collect();
        functions.sort();
        let mut kept_tables = vec![];
        let mut traps = BTreeSet::new();
        for (number, section) in object.sections.iter().enumerate() {
            let number = number as i16 + 1;
            let mut kept = vec![];
            for r in &section.relocations {
                let symbol = &object.symbols[&r.symbol];
                let undefined =
                    symbol.storage == EXTERNAL && symbol.section == 0 && symbol.value == 0;
                if !(undefined && missing.contains(&symbol.name)) {
                    kept.push(r.clone());
                    continue;
                }
                let owner = functions
                    .iter()
                    .rev()
                    .find(|(s, value, _)| *s == number && *value <= r.offset);
                match owner {
                    Some(&(_, value, name)) if !source_of.contains_key(name) => {
                        traps.insert((number, value, name.to_owned()));
                    }
                    _ => {
                        errors.push(format!(
                            "{source}: {} refers to {}, which nothing defines",
                            owner.map_or(section.name.as_str(), |o| o.2),
                            symbol.name
                        ));
                        kept.push(r.clone());
                    }
                }
            }
            if kept.len() != section.relocations.len() {
                ensure!(
                    section.relocations.len() < 0xffff,
                    "{source}: {} has too many relocations to patch",
                    section.name
                );
                kept_tables.push((number, section.relocation_offset, section.raw_offset, kept));
            }
        }
        drop(object);
        for (number, offset, _, kept) in kept_tables {
            let header = 20 + 40 * (number as usize - 1);
            put_u16(data, header + 32, kept.len() as u16);
            for (i, r) in kept.iter().enumerate() {
                let at = offset as usize + i * 10;
                put_u32(data, at, r.offset);
                put_u32(data, at + 4, r.symbol);
                put_u16(data, at + 8, r.kind);
            }
        }
        for (number, value, name) in traps {
            let raw =
                u32::from_le_bytes(data[20 + 40 * (number as usize - 1) + 20..][..4].try_into()?);
            let at = (raw + value) as usize;
            data[at..at + 2].copy_from_slice(&UD2);
            patched.trapped.push(format!("{source}: {name}"));
        }
    }
    ensure!(
        errors.is_empty(),
        "the source-only compiler cannot be linked:\n{}",
        errors.join("\n")
    );
    Ok(patched)
}

// ------------------------------------------------------------------------------------------------- resources

/// Compile the driver's resource script (the subset it uses: `#if`/`#elif`/`#else` on VERSION, `#define`, one
/// VERSIONINFO and one ICON) as rc does, with VERSION = INDEX (the tree's `include/version.h`): the resources, in
/// the script's order (an icon's images, then its group).
pub fn compile_resources(
    script: &str,
    version_h: &str,
    index: usize,
    icon: &[u8],
) -> Result<Vec<Resource>> {
    let mut constants: HashMap<String, i64> = HashMap::new();
    let define = Regex::new(r"(?m)^#define (\w+) (\d+)\s*$")?;
    for m in define.captures_iter(version_h) {
        constants.insert(m[1].to_owned(), m[2].parse()?);
    }
    constants.insert("VERSION".into(), index as i64);
    // (the preprocessor: conditions on VERSION, and #defines substituted)
    let mut macros: Vec<(String, String)> = vec![];
    let mut text = String::new();
    // ([(this branch is taken, a branch was taken)])
    let mut stack: Vec<(bool, bool)> = vec![];
    let condition = Regex::new(r"^(\w+)\s*(<=|>=|==|!=|<|>)\s*(\w+)$")?;
    let evaluate = |expression: &str, constants: &HashMap<String, i64>| -> Result<bool> {
        let m = condition
            .captures(expression.trim())
            .with_context(|| format!("unsupported resource condition {expression}"))?;
        let value = |word: &str| -> Result<i64> {
            word.parse()
                .ok()
                .or_else(|| constants.get(word).copied())
                .with_context(|| format!("unknown resource constant {word}"))
        };
        let (a, b) = (value(&m[1])?, value(&m[3])?);
        Ok(match &m[2] {
            "<=" => a <= b,
            ">=" => a >= b,
            "==" => a == b,
            "!=" => a != b,
            "<" => a < b,
            _ => a > b,
        })
    };
    for line in script.lines() {
        let line = match line.find("//") {
            Some(at) if !line[..at].contains('"') => &line[..at],
            _ => line,
        };
        let active = stack.iter().all(|(taken, _)| *taken);
        let directive = line.trim_start();
        if let Some(expression) = directive.strip_prefix("#if ") {
            let value = active && evaluate(expression, &constants)?;
            stack.push((value, value));
        } else if let Some(expression) = directive.strip_prefix("#elif ") {
            let outer = stack[..stack.len().saturating_sub(1)]
                .iter()
                .all(|(t, _)| *t);
            let (_, done) = *stack.last().context("#elif without #if")?;
            let value = outer && !done && evaluate(expression, &constants)?;
            *stack.last_mut().unwrap() = (value, done || value);
        } else if directive.starts_with("#else") {
            let outer = stack[..stack.len().saturating_sub(1)]
                .iter()
                .all(|(t, _)| *t);
            let (_, done) = *stack.last().context("#else without #if")?;
            *stack.last_mut().unwrap() = (outer && !done, true);
        } else if directive.starts_with("#endif") {
            stack.pop().context("#endif without #if")?;
        } else if !active || directive.starts_with("#include") {
        } else if let Some(rest) = directive.strip_prefix("#define ") {
            let (name, value) = rest
                .trim()
                .split_once(char::is_whitespace)
                .unwrap_or((rest.trim(), ""));
            macros.push((name.to_owned(), value.trim().to_owned()));
        } else {
            ensure!(
                !directive.starts_with('#'),
                "unsupported resource directive {directive}"
            );
            let mut line = line.to_owned();
            for (name, value) in &macros {
                line = Regex::new(&format!(r"\b{name}\b"))?
                    .replace_all(&line, regex::NoExpand(value))
                    .into_owned();
            }
            text += &line;
            text.push('\n');
        }
    }
    let tokens = tokenize(&text)?;
    let mut parser = Parser { tokens, at: 0 };
    let mut out = vec![];
    let resource = |kind: u32, name: ResourceName, data: Vec<u8>| Resource {
        kind: ResourceName::Number(kind),
        name,
        language: 0x409,
        codepage: 0,
        data,
    };
    let mut icon_id = 1u16;
    while !parser.done() {
        let name = parser.word()?;
        let kind = parser.word()?;
        parser.flags();
        match kind.as_str() {
            "VERSIONINFO" => {
                ensure!(name == "1", "VERSIONINFO {name}: only 1 is supported");
                let data = parser.version_info()?;
                out.push(resource(16, ResourceName::Number(1), data));
            }
            "ICON" => {
                let Token::String(_) = parser.next()? else {
                    bail!("ICON {name} names no file");
                };
                ensure!(
                    icon.len() >= 6 && icon[2..4] == [1, 0],
                    "the icon file is not an icon"
                );
                let count = u16::from_le_bytes(icon[4..6].try_into()?) as usize;
                let mut group = vec![0, 0, 1, 0];
                group.extend((count as u16).to_le_bytes());
                for i in 0..count {
                    let e = icon
                        .get(6 + 16 * i..6 + 16 * (i + 1))
                        .context("icon entry")?;
                    let size = u32::from_le_bytes(e[8..12].try_into()?) as usize;
                    let at = u32::from_le_bytes(e[12..16].try_into()?) as usize;
                    let image = icon.get(at..at + size).context("icon image")?;
                    out.push(resource(
                        3,
                        ResourceName::Number(icon_id.into()),
                        image.to_vec(),
                    ));
                    group.extend(&e[..12]);
                    group.extend(icon_id.to_le_bytes());
                    icon_id += 1;
                }
                out.push(resource(14, ResourceName::Name(name.to_uppercase()), group));
            }
            _ => bail!("unsupported resource {kind}"),
        }
    }
    Ok(out)
}

fn pad4(data: &mut Vec<u8>) {
    while !data.len().is_multiple_of(4) {
        data.push(0);
    }
}

#[derive(Clone, Debug, PartialEq)]
enum Token {
    Word(String),
    Number(u32),
    String(Vec<u16>),
    Comma,
}

fn tokenize(text: &str) -> Result<Vec<Token>> {
    let chars: Vec<char> = text.chars().collect();
    let mut tokens = vec![];
    let mut i = 0;
    while i < chars.len() {
        let c = chars[i];
        if c.is_whitespace() {
            i += 1;
        } else if c == ',' {
            tokens.push(Token::Comma);
            i += 1;
        } else if c == '"' || (c == 'L' && chars.get(i + 1) == Some(&'"')) {
            i += if c == 'L' { 2 } else { 1 };
            let mut value = vec![];
            while chars.get(i) != Some(&'"') {
                let c = *chars.get(i).context("unterminated resource string")?;
                i += 1;
                if c != '\\' {
                    value.push(c as u16);
                    continue;
                }
                let e = *chars.get(i).context("unterminated resource escape")?;
                if e.is_digit(8) {
                    let digits: String = chars[i..]
                        .iter()
                        .take(3)
                        .take_while(|c| c.is_digit(8))
                        .collect();
                    i += digits.len();
                    value.push(u16::from_str_radix(&digits, 8)?);
                    continue;
                }
                i += 1;
                value.push(match e {
                    'n' => 10,
                    't' => 9,
                    'r' => 13,
                    '\\' | '"' => e as u16,
                    _ => bail!("unsupported resource escape \\{e}"),
                });
            }
            i += 1;
            tokens.push(Token::String(value));
        } else if c.is_ascii_digit() {
            let word: String = chars[i..]
                .iter()
                .take_while(|c| c.is_ascii_alphanumeric())
                .collect();
            i += word.len();
            let value = match word.strip_prefix("0x").or_else(|| word.strip_prefix("0X")) {
                Some(hex) => u32::from_str_radix(hex, 16)?,
                None => word.parse()?,
            };
            tokens.push(Token::Number(value));
        } else if c.is_ascii_alphabetic() || c == '_' {
            let word: String = chars[i..]
                .iter()
                .take_while(|c| c.is_ascii_alphanumeric() || **c == '_')
                .collect();
            i += word.len();
            tokens.push(Token::Word(word));
        } else {
            bail!("unexpected character {c:?} in the resource script");
        }
    }
    Ok(tokens)
}

struct Parser {
    tokens: Vec<Token>,
    at: usize,
}

impl Parser {
    fn done(&self) -> bool {
        self.at >= self.tokens.len()
    }
    fn next(&mut self) -> Result<Token> {
        let token = self
            .tokens
            .get(self.at)
            .cloned()
            .context("the resource script ends early")?;
        self.at += 1;
        Ok(token)
    }
    fn peek(&self) -> Option<&Token> {
        self.tokens.get(self.at)
    }
    fn word(&mut self) -> Result<String> {
        match self.next()? {
            Token::Word(w) => Ok(w),
            Token::Number(n) => Ok(n.to_string()),
            t => bail!("expected a word in the resource script, found {t:?}"),
        }
    }
    fn number(&mut self) -> Result<u32> {
        match self.next()? {
            Token::Number(n) => Ok(n),
            t => bail!("expected a number in the resource script, found {t:?}"),
        }
    }
    fn numbers(&mut self) -> Result<Vec<u32>> {
        let mut out = vec![self.number()?];
        while self.peek() == Some(&Token::Comma) {
            self.at += 1;
            out.push(self.number()?);
        }
        Ok(out)
    }
    /// The memory flags after a resource's type (MOVEABLE, PURE, DISCARDABLE...): the image has none of them.
    fn flags(&mut self) {
        while let Some(Token::Word(w)) = self.peek() {
            if ![
                "MOVEABLE",
                "FIXED",
                "PURE",
                "IMPURE",
                "PRELOAD",
                "LOADONCALL",
                "DISCARDABLE",
            ]
            .contains(&w.as_str())
            {
                break;
            }
            self.at += 1;
        }
    }
    fn version_info(&mut self) -> Result<Vec<u8>> {
        let mut fixed: HashMap<String, Vec<u32>> = HashMap::new();
        loop {
            let word = self.word()?;
            if word == "BEGIN" {
                break;
            }
            fixed.insert(word, self.numbers()?);
        }
        let get = |key: &str, i: usize| fixed.get(key).and_then(|v| v.get(i)).copied().unwrap_or(0);
        let version = |key: &str| {
            [
                (get(key, 0) << 16) | get(key, 1),
                (get(key, 2) << 16) | get(key, 3),
            ]
        };
        let mut value = vec![];
        for word in [0xFEEF04BD, 0x00010000]
            .into_iter()
            .chain(version("FILEVERSION"))
            .chain(version("PRODUCTVERSION"))
            .chain(
                [
                    "FILEFLAGSMASK",
                    "FILEFLAGS",
                    "FILEOS",
                    "FILETYPE",
                    "FILESUBTYPE",
                ]
                .map(|k| get(k, 0)),
            )
            .chain([0, 0])
        {
            value.extend(word.to_le_bytes());
        }
        let children = self.version_blocks()?;
        Ok(version_node(
            "VS_VERSION_INFO",
            0,
            value.len() as u16,
            &value,
            &children,
        ))
    }
    /// The blocks and values up to END, as nodes.
    fn version_blocks(&mut self) -> Result<Vec<u8>> {
        let mut out = vec![];
        loop {
            match self.word()?.as_str() {
                "END" => return Ok(out),
                "BLOCK" => {
                    let Token::String(key) = self.next()? else {
                        bail!("a BLOCK without a name");
                    };
                    ensure!(self.word()? == "BEGIN", "a BLOCK without BEGIN");
                    let children = self.version_blocks()?;
                    out.extend(version_node(
                        &String::from_utf16(&key)?,
                        1,
                        0,
                        &[],
                        &children,
                    ));
                }
                "VALUE" => {
                    let Token::String(key) = self.next()? else {
                        bail!("a VALUE without a name");
                    };
                    let key = String::from_utf16(&key)?;
                    let mut text = None;
                    let mut words = vec![];
                    while self.peek() == Some(&Token::Comma) {
                        self.at += 1;
                        match self.next()? {
                            Token::String(s) => text = Some(s),
                            Token::Number(n) => words.push(n as u16),
                            t => bail!("unexpected {t:?} in VALUE {key}"),
                        }
                    }
                    out.extend(match text {
                        Some(mut s) => {
                            // (a string's length counts its terminator, which it is given if it has none)
                            if s.last() != Some(&0) {
                                s.push(0);
                            }
                            let bytes: Vec<u8> = s.iter().flat_map(|c| c.to_le_bytes()).collect();
                            version_node(&key, 1, s.len() as u16, &bytes, &[])
                        }
                        None => {
                            let bytes: Vec<u8> =
                                words.iter().flat_map(|c| c.to_le_bytes()).collect();
                            version_node(&key, 0, bytes.len() as u16, &bytes, &[])
                        }
                    });
                }
                w => bail!("unexpected {w} in VERSIONINFO"),
            }
        }
    }
}

/// A VS_VERSIONINFO node: its length, the value's length (TEXT: in characters), its type, its key, its value and
/// its children, each part aligned to 4 bytes.
fn version_node(key: &str, text: u16, value_length: u16, value: &[u8], children: &[u8]) -> Vec<u8> {
    let mut out = vec![0, 0];
    out.extend(value_length.to_le_bytes());
    out.extend(text.to_le_bytes());
    out.extend(key.encode_utf16().chain([0]).flat_map(u16::to_le_bytes));
    pad4(&mut out);
    out.extend(value);
    if !children.is_empty() {
        pad4(&mut out);
        out.extend(children);
    }
    let length = out.len() as u16;
    out[..2].copy_from_slice(&length.to_le_bytes());
    pad4(&mut out);
    out
}
