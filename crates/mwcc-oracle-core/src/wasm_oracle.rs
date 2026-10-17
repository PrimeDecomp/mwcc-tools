//! The oracle as WebAssembly: the module of gc2's sources (crate::wasm_build) with the Rust hooks linked in,
//! `build/<version>/wasm/oracle.wasm`, which runs in process in the oracle's place (crate::in_process).
//!
//! The hooks are built for wasm32 as LLVM IR (`mwcc-tools build-hooks --wasm`). They name what they wrap and read by
//! `config/<version>/symbols.txt`'s names, which are taken to gc2's at the same address, as for the source-built
//! oracle (crate::source_oracle). The units that define a hook's function are compiled to IR and edited there:
//! - the function `g` of hook `H` is renamed `H__retail` (and made global), its own calls to itself stay retail, and
//!   the unit's other references to it go to `H`, the Rust wrapper; a global `g` of another name becomes a thunk to
//!   `H`, for the other units' calls;
//! - a static variable the hooks read is given a global alias.
//!
//! In the Rust IR, the names the hooks read become gc2's (an offset into a variable, as `copts+0xf8`, a constant
//! address), and a name the hooks define that gc2 also defines (`initheaps`) is renamed. `wasm/src/oracle.c` has the
//! trace writer, the variadic entry of `IroDump_Print`, and the stand-ins for the probes' islands that the source-built
//! oracle has too (see there).
use crate::{
    executable::{self, Symbol},
    producer,
    source_oracle::{ARENA_GLOBALS, FORCE_RETAIL, Hook, Import},
    wasm_build,
};
use anyhow::{Context, Result, bail, ensure};
use regex::{Captures, Regex};
use serde::Serialize;
use serde_json::Value;
use std::{
    collections::{BTreeMap, BTreeSet},
    fs,
    path::Path,
    process::Command,
    sync::LazyLock,
};

/// The oracle's C side.
pub const ORACLE_C: &str = "wasm/src/oracle.c";
/// The trace writer, which `oracle.c` defines.
const TRACE: &str = "OracleTrace_Printf";
/// The variadic hook, whose entry `oracle.c` defines under gc2's name (`ORACLE_IRO_DUMP`), and the Rust function it
/// passes its arguments to.
const VARIADIC: (&str, &str) = ("IroDump_Print", "IroDump_PrintArguments");

/// Where VERSION's work files are.
fn work(version: &str) -> String {
    format!("{}/oracle", wasm_build::directory(version))
}

/// The Rust IR it links (`mwcc-tools build-hooks --wasm`): the common hooks and the version's frontend.
pub fn rust_ir(version: &str) -> [String; 2] {
    let directory = format!("{}/rust", work(version));
    [
        format!("{directory}/hooks.ll"),
        format!("{directory}/frontend.ll"),
    ]
}

#[derive(Debug, Serialize)]
pub struct Outcome {
    pub output: String,
    pub hooks: Vec<Hook>,
    pub imports: Vec<Import>,
    /// The Rust names renamed because gc2 defines them too.
    pub renamed: Vec<String>,
}

/// An LLVM IR global name.
static NAME: LazyLock<Regex> =
    LazyLock::new(|| Regex::new(r#"@(?:"([^"]+)"|([-A-Za-z$._][-A-Za-z$._0-9]*))"#).unwrap());

fn name_of(c: &Captures<'_>) -> String {
    c.get(1).or(c.get(2)).unwrap().as_str().to_owned()
}

fn ir_name(name: &str) -> String {
    if name
        .chars()
        .all(|c| c.is_ascii_alphanumeric() || "-$._".contains(c))
    {
        format!("@{name}")
    } else {
        format!("@\"{name}\"")
    }
}

/// The names a module defines (but for its internal ones) and those it declares, from its top-level lines.
fn interface(ir: &str) -> (BTreeSet<String>, BTreeSet<String>) {
    let mut defined = BTreeSet::new();
    let mut declared = BTreeSet::new();
    for line in ir.lines() {
        let local = |rest: &str| rest.contains(" internal ") || rest.contains(" private ");
        if line.starts_with("define ") || line.starts_with("declare ") {
            let Some(c) = NAME.captures(line) else {
                continue;
            };
            let header = &line[..c.get(0).unwrap().start()];
            if line.starts_with("declare ") {
                declared.insert(name_of(&c));
            } else if !local(&format!(" {header}")) {
                defined.insert(name_of(&c));
            }
        } else if line.starts_with('@')
            && let Some(c) = NAME.captures(line)
            && c.get(0).unwrap().start() == 0
        {
            let rest = &line[c.get(0).unwrap().end()..];
            if rest.starts_with(" = external ") {
                declared.insert(name_of(&c));
            } else if !local(rest) {
                defined.insert(name_of(&c));
            }
        }
    }
    (defined, declared)
}

/// A function's definition line: its return (attributes and type), name and parameters (types and attributes, then
/// the name), and whether it is internal or variadic.
struct Header {
    ret: String,
    parameters: Vec<String>,
    internal: bool,
    variadic: bool,
}

/// The parameters of a list, split at its top-level commas.
fn split_parameters(list: &str) -> Vec<String> {
    let mut out = vec![];
    let (mut depth, mut start) = (0, 0);
    for (i, c) in list.char_indices() {
        match c {
            '(' | '{' | '[' | '<' => depth += 1,
            ')' | '}' | ']' | '>' => depth -= 1,
            ',' if depth == 0 => {
                out.push(list[start..i].trim().to_owned());
                start = i + 1;
            }
            _ => {}
        }
    }
    if !list[start..].trim().is_empty() {
        out.push(list[start..].trim().to_owned());
    }
    out
}

fn header(line: &str) -> Result<Header> {
    let c = NAME.captures(line).context("a definition without a name")?;
    let whole = c.get(0).unwrap();
    const DROPPED: &[&str] = &[
        "define",
        "internal",
        "private",
        "dso_local",
        "dso_preemptable",
        "hidden",
        "protected",
        "default",
    ];
    let words: Vec<&str> = line[..whole.start()].split_whitespace().collect();
    let internal = words.contains(&"internal") || words.contains(&"private");
    let ret = words
        .iter()
        .filter(|w| !DROPPED.contains(w))
        .copied()
        .collect::<Vec<_>>()
        .join(" ");
    let rest = &line[whole.end()..];
    ensure!(rest.starts_with('('), "{line}: no parameters");
    let mut depth = 0;
    let end = rest
        .char_indices()
        .find(|&(_, ch)| {
            match ch {
                '(' => depth += 1,
                ')' => depth -= 1,
                _ => {}
            }
            depth == 0
        })
        .context("unbalanced parameters")?
        .0;
    let mut parameters = vec![];
    let mut variadic = false;
    for p in split_parameters(&rest[1..end]) {
        if p == "..." {
            variadic = true;
            continue;
        }
        // (the parameter's name, last)
        let ty = match p.rsplit_once(' ') {
            Some((ty, name)) if name.starts_with('%') => ty.to_owned(),
            _ => p,
        };
        parameters.push(ty);
    }
    Ok(Header {
        ret,
        parameters,
        internal,
        variadic,
    })
}

fn declaration(name: &str, h: &Header) -> String {
    let mut parameters = h.parameters.clone();
    if h.variadic {
        parameters.push("...".into());
    }
    format!(
        "declare {} {}({})",
        h.ret,
        ir_name(name),
        parameters.join(", ")
    )
}

/// A definition of NAME that calls TARGET with its arguments.
fn thunk(name: &str, target: &str, h: &Header) -> Result<String> {
    ensure!(!h.variadic, "{name} is variadic: it cannot be a thunk");
    let parameters: Vec<String> = h
        .parameters
        .iter()
        .enumerate()
        .map(|(i, ty)| format!("{ty} %a{i}"))
        .collect();
    let ty = h.ret.rsplit(' ').next().unwrap_or("void");
    let call = format!(
        "call {} {}({})",
        h.ret,
        ir_name(target),
        parameters.join(", ")
    );
    let body = if ty == "void" {
        format!("  tail {call}\n  ret void")
    } else {
        format!("  %r = tail {call}\n  ret {ty} %r")
    };
    Ok(format!(
        "define {} {}({}) {{\n{body}\n}}",
        h.ret,
        ir_name(name),
        parameters.join(", ")
    ))
}

/// A gc2 symbol: its name (as C has it), section, address and scope.
struct Place {
    name: String,
    offset: u32,
    unit: String,
    function: bool,
}

struct Resolver {
    ours: BTreeMap<String, Symbol>,
    theirs: BTreeMap<String, Vec<Symbol>>,
    splits: executable::Splits,
    /// gc2's names (without the leading underscore).
    names: BTreeSet<String>,
    /// gc2's variables, by name (with the underscore).
    variables: BTreeMap<String, Vec<Symbol>>,
}

impl Resolver {
    fn new(root: &Path, version: &str) -> Result<Self> {
        let read = |path: String| -> Result<Vec<Symbol>> {
            let text =
                fs::read_to_string(root.join(&path)).with_context(|| format!("read {path}"))?;
            executable::read_symbols(&text)
        };
        let ours = read(format!("config/{version}/symbols.txt"))?
            .into_iter()
            .map(|s| (s.name.clone(), s))
            .collect();
        let config_dir = format!("gc2/config/{version}");
        let mut theirs: BTreeMap<String, Vec<Symbol>> = BTreeMap::new();
        let mut names = BTreeSet::new();
        let mut variables: BTreeMap<String, Vec<Symbol>> = BTreeMap::new();
        for s in read(format!("{config_dir}/symbols.txt"))? {
            names.insert(s.name.strip_prefix('_').unwrap_or(&s.name).to_owned());
            if s.section != ".text" {
                variables.entry(s.name.clone()).or_default().push(s.clone());
            }
            theirs.entry(s.section.clone()).or_default().push(s);
        }
        for list in theirs.values_mut() {
            list.sort_by_key(|s| s.address);
        }
        let splits = executable::read_splits(&fs::read_to_string(
            root.join(format!("{config_dir}/splits.txt")),
        )?)?;
        Ok(Self {
            ours,
            theirs,
            splits,
            names,
            variables,
        })
    }

    /// Whether NAME is the oracle's, or a variable of gc2's (which the hooks read by gc2's name where the oracle
    /// finds it in the original's code).
    fn known(&self, name: &str) -> bool {
        self.ours.contains_key(&format!("_{name}"))
            || self.variables.contains_key(&format!("_{name}"))
    }

    /// gc2's symbol at the address of the oracle's NAME (C's name, without the underscore), or gc2's variable NAME.
    fn resolve(&self, name: &str) -> Result<Place> {
        let key = format!("_{name}");
        let ours = match (self.ours.get(&key), self.variables.get(&key)) {
            (Some(ours), _) => ours,
            (None, Some(list)) => {
                let [s] = list.as_slice() else {
                    bail!("gc2 has {} variables named {name}", list.len());
                };
                s
            }
            (None, None) => bail!("{name} is neither the oracle's nor gc2's"),
        };
        let address = ours.address;
        let function = ours.section == ".text";
        let theirs = self
            .theirs
            .get(&ours.section)
            .and_then(|list| {
                list.iter().rev().find(|s| {
                    s.address == address
                        || (s.address < address
                            && executable::size(&s.options).is_ok_and(|n| address < s.address + n))
                })
            })
            .with_context(|| format!("{name}: gc2 has no symbol at {address:#x}"))?;
        let offset = address - theirs.address;
        ensure!(
            !function || offset == 0,
            "{name} ({address:#x}) is inside gc2's {}",
            theirs.name
        );
        let unit = self
            .splits
            .units
            .iter()
            .find(|u| {
                u.ranges.iter().any(|r| {
                    r.section == theirs.section
                        && r.start <= theirs.address
                        && theirs.address < r.end
                })
            })
            .with_context(|| format!("{name}: no unit of gc2 has {}", theirs.name))?;
        Ok(Place {
            name: theirs
                .name
                .strip_prefix('_')
                .unwrap_or(&theirs.name)
                .to_owned(),
            offset,
            unit: unit.name.clone(),
            function,
        })
    }
}

/// Run PROGRAM in ROOT; its diagnostics.
fn run_tool(root: &Path, program: &str, arguments: &[String]) -> Result<String> {
    let output = Command::new(program)
        .args(arguments)
        .current_dir(root)
        .output()
        .with_context(|| format!("launch {program}"))?;
    ensure!(
        output.status.success(),
        "{program} {}: {}{}",
        arguments.join(" "),
        String::from_utf8_lossy(&output.stdout),
        String::from_utf8_lossy(&output.stderr)
    );
    Ok(String::from_utf8_lossy(&output.stderr).into_owned())
}

/// The arguments that compile IR to an object of the module.
fn codegen_arguments(input: &str, output: &str) -> Vec<String> {
    [
        "--target=wasm32-unknown-unknown",
        "-O2",
        "-mllvm",
        "-wasm-enable-sjlj",
        "-mllvm",
        "-wasm-use-legacy-eh=false",
        "-mexception-handling",
        "-Wno-override-module",
        "-c",
        input,
        "-o",
        output,
    ]
    .map(String::from)
    .to_vec()
}

/// A gc2 unit's edits.
#[derive(Default)]
struct UnitEdit {
    /// g → (H, the Rust wrapper's name as linked).
    hooks: BTreeMap<String, (String, String)>,
    /// The aliases the unit gives its statics: (alias, gc2's name, offset).
    aliases: Vec<(String, String, u32)>,
    /// The global entries it gives its static functions: (entry, gc2's name).
    entries: Vec<(String, String)>,
    /// The unit's functions that the original has (gc2's names): one it lacks, the original inlined everywhere.
    retail: BTreeSet<String>,
}

/// The functions of IR (their names) and the names each one's body references.
fn function_references(ir: &str) -> BTreeMap<String, BTreeSet<String>> {
    let mut out: BTreeMap<String, BTreeSet<String>> = BTreeMap::new();
    let mut current: Option<String> = None;
    for line in ir.lines() {
        if line.starts_with("define ") {
            current = NAME.captures(line).map(|c| name_of(&c));
            if let Some(f) = &current {
                out.entry(f.clone()).or_default();
            }
            continue;
        }
        if line == "}" {
            current = None;
        } else if let Some(f) = &current {
            out.get_mut(f)
                .unwrap()
                .extend(NAME.captures_iter(line).map(|c| name_of(&c)));
        }
    }
    out
}

/// For each hook's function g, the functions of the unit that the original inlined everywhere (EDIT's `retail`
/// lacks them), that g calls and that call g back, through each other: in the original, those calls are g's calls
/// to itself, which stay retail. Each gets a copy for g (`<name>__in_<hook>`).
fn inlined_callers(
    functions: &BTreeMap<String, BTreeSet<String>>,
    edit: &UnitEdit,
) -> BTreeMap<String, BTreeSet<String>> {
    let inlined = |f: &String| !edit.retail.contains(f) && !edit.hooks.contains_key(f);
    let mut out = BTreeMap::new();
    for g in edit.hooks.keys() {
        // (the inlined functions g reaches through inlined functions)
        let mut reached = BTreeSet::new();
        let mut pending: Vec<&String> = functions
            .get(g)
            .into_iter()
            .flatten()
            .filter(|f| functions.contains_key(*f) && inlined(f))
            .collect();
        while let Some(f) = pending.pop() {
            if reached.insert(f.clone()) {
                pending.extend(
                    functions[f]
                        .iter()
                        .filter(|n| functions.contains_key(*n) && inlined(n)),
                );
            }
        }
        // (those of them that reach g back)
        let mut back: BTreeSet<String> = BTreeSet::new();
        loop {
            let more: Vec<String> = reached
                .iter()
                .filter(|f| !back.contains(*f))
                .filter(|f| functions[*f].iter().any(|n| n == g || back.contains(n)))
                .cloned()
                .collect();
            if more.is_empty() {
                break;
            }
            back.extend(more);
        }
        if !back.is_empty() {
            out.insert(g.clone(), back);
        }
    }
    out
}

/// The copy of inlined function F for hook H's function.
fn inlined_copy(f: &str, hook: &str) -> String {
    format!("{f}__in_{hook}")
}

/// UNIT's IR, edited.
fn edit_unit(ir: &str, edit: &UnitEdit) -> Result<(String, Vec<String>)> {
    let copies = inlined_callers(&function_references(ir), edit);
    let mut headers: BTreeMap<String, Header> = BTreeMap::new();
    let mut statics = BTreeSet::new();
    for line in ir.lines() {
        if line.starts_with("define ")
            && let Some(c) = NAME.captures(line)
        {
            let name = name_of(&c);
            if edit.hooks.contains_key(&name) || edit.entries.iter().any(|(_, g)| *g == name) {
                headers.insert(name, header(line)?);
            }
        } else if line.starts_with('@')
            && let Some(c) = NAME.captures(line)
            && line[c.get(0).unwrap().end()..].starts_with(" = internal ")
        {
            statics.insert(name_of(&c));
        }
    }
    for g in edit.hooks.keys() {
        ensure!(headers.contains_key(g), "the unit does not define {g}");
    }
    let mut out = String::with_capacity(ir.len() + 4096);
    let mut current: Option<String> = None;
    for line in ir.lines() {
        if line.starts_with("define ") {
            current = NAME.captures(line).map(|c| name_of(&c));
        }
        let defining = line.starts_with("define ");
        let replaced = NAME.replace_all(line, |c: &Captures<'_>| {
            let name = name_of(c);
            match edit.hooks.get(&name) {
                Some((hook, _)) if current.as_deref() == Some(name.as_str()) => {
                    ir_name(&format!("{hook}__retail"))
                }
                Some((_, wrapper)) => ir_name(wrapper),
                None => match current.as_ref().and_then(|g| Some((g, copies.get(g)?))) {
                    // (g's call of a function the original inlined in it)
                    Some((g, inlined)) if !defining && inlined.contains(&name) => {
                        ir_name(&inlined_copy(&name, &edit.hooks[g].0))
                    }
                    _ => c.get(0).unwrap().as_str().to_owned(),
                },
            }
        });
        let mut replaced = replaced.into_owned();
        if defining && current.as_ref().is_some_and(|g| edit.hooks.contains_key(g)) {
            // (the retail function, called from the wrapper: global)
            replaced = replaced
                .replacen("define internal ", "define ", 1)
                .replacen("define private ", "define ", 1);
        }
        out.push_str(&replaced);
        out.push('\n');
        if line == "}" {
            current = None;
        }
    }
    // (the copies of the functions the original inlined in a hook's function: their calls of it stay retail)
    for (g, inlined) in &copies {
        let hook = &edit.hooks[g].0;
        let mut current: Option<String> = None;
        for line in ir.lines() {
            if line.starts_with("define ") {
                current = NAME
                    .captures(line)
                    .map(|c| name_of(&c))
                    .filter(|f| inlined.contains(f));
            }
            if current.is_none() {
                continue;
            }
            let replaced = NAME.replace_all(line, |c: &Captures<'_>| {
                let name = name_of(c);
                if name == *g {
                    ir_name(&format!("{hook}__retail"))
                } else if inlined.contains(&name) {
                    ir_name(&inlined_copy(&name, hook))
                } else if let Some((_, wrapper)) = edit.hooks.get(&name) {
                    ir_name(wrapper)
                } else {
                    c.get(0).unwrap().as_str().to_owned()
                }
            });
            let replaced = if line.starts_with("define ") && !line.starts_with("define internal ") {
                replaced.replacen("define ", "define internal ", 1)
            } else {
                replaced.into_owned()
            };
            out.push_str(&replaced);
            out.push('\n');
            if line == "}" {
                current = None;
            }
        }
    }
    let (defined, declared) = interface(&out);
    let mut thunks = vec![];
    for (g, (_, wrapper)) in &edit.hooks {
        let h = &headers[g];
        if !defined.contains(wrapper) && !declared.contains(wrapper) {
            out.push_str(&declaration(wrapper, h));
            out.push('\n');
        }
        if !h.internal && g != wrapper {
            if h.variadic {
                // (oracle.c's entry has gc2's name)
                thunks.push(g.clone());
            } else {
                out.push_str(&thunk(g, wrapper, h)?);
                out.push('\n');
            }
        }
    }
    for (entry, g) in &edit.entries {
        let h = headers
            .get(g)
            .with_context(|| format!("the unit does not define {g}"))?;
        out.push_str(&thunk(entry, g, h)?);
        out.push('\n');
    }
    for (alias, name, offset) in &edit.aliases {
        ensure!(
            statics.contains(name),
            "{name} is not a static variable of the unit"
        );
        let target = if *offset == 0 {
            format!("ptr {}", ir_name(name))
        } else {
            format!("getelementptr (i8, ptr {}, i32 {offset})", ir_name(name))
        };
        out.push_str(&format!("{} = alias i8, {target}\n", ir_name(alias)));
    }
    Ok((out, thunks))
}

/// A Rust IR, with its names renamed: RENAMES maps a name to its replacement (`@x`, or a constant expression).
fn edit_rust(ir: &str, renames: &BTreeMap<String, String>) -> String {
    let mut out = String::with_capacity(ir.len());
    let mut externals: BTreeSet<String> = BTreeSet::new();
    let mut declared: BTreeSet<String> = BTreeSet::new();
    for line in ir.lines() {
        // (a variable's declaration: replaced by its target's, below)
        if line.starts_with('@')
            && let Some(c) = NAME.captures(line)
            && c.get(0).unwrap().start() == 0
            && line[c.get(0).unwrap().end()..].starts_with(" = external ")
            && let Some(target) = renames.get(&name_of(&c))
        {
            if let Some(t) = NAME.captures(target) {
                externals.insert(name_of(&t));
            }
            continue;
        }
        let replaced = NAME.replace_all(line, |c: &Captures<'_>| {
            renames
                .get(&name_of(c))
                .cloned()
                .unwrap_or_else(|| c.get(0).unwrap().as_str().to_owned())
        });
        if line.starts_with("declare ")
            && let Some(c) = NAME.captures(&replaced)
            && !declared.insert(name_of(&c))
        {
            continue;
        }
        out.push_str(&replaced);
        out.push('\n');
    }
    let (defined, declared) = interface(&out);
    for name in externals {
        if !defined.contains(&name) && !declared.contains(&name) {
            out.push_str(&format!("{} = external global i8\n", ir_name(&name)));
        }
    }
    out
}

/// The names a WebAssembly module imports.
fn imports(module: &[u8]) -> Result<BTreeSet<String>> {
    fn leb(data: &[u8], at: &mut usize) -> Result<u32> {
        let (mut value, mut shift) = (0u32, 0);
        loop {
            let byte = *data.get(*at).context("truncated module")?;
            *at += 1;
            value |= u32::from(byte & 0x7f) << shift;
            if byte & 0x80 == 0 {
                return Ok(value);
            }
            shift += 7;
            ensure!(shift < 35, "a bad LEB128 number");
        }
    }
    fn text<'a>(data: &'a [u8], at: &mut usize) -> Result<&'a str> {
        let n = leb(data, at)? as usize;
        let s = data.get(*at..*at + n).context("truncated module")?;
        *at += n;
        Ok(std::str::from_utf8(s)?)
    }
    ensure!(module.starts_with(b"\0asm"), "not a WebAssembly module");
    let mut at = 8;
    let mut out = BTreeSet::new();
    while at < module.len() {
        let id = module[at];
        at += 1;
        let size = leb(module, &mut at)? as usize;
        let end = at + size;
        if id == 2 {
            let count = leb(module, &mut at)?;
            for _ in 0..count {
                let space = text(module, &mut at)?.to_owned();
                let name = text(module, &mut at)?;
                out.insert(format!("{space}.{name}"));
                match module.get(at).context("truncated module")? {
                    0 => {
                        at += 1;
                        leb(module, &mut at)?;
                    }
                    kind => bail!("import {name}: kind {kind} (only functions are expected)"),
                }
            }
        }
        at = end;
    }
    Ok(out)
}

/// Link VERSION's oracle module: its stock objects built (`wasm_build`) and the Rust IR (`rust_ir`).
pub fn run(root: &Path, version: &str, clang: &str, wasm_ld: &str) -> Result<Outcome> {
    let output = wasm_build::oracle_output(version);
    let work = work(version);
    fs::create_dir_all(root.join(&work))?;
    let resolver = Resolver::new(root, version)?;
    let rust: Vec<(String, String)> = rust_ir(version)
        .into_iter()
        .map(|path| {
            let text =
                fs::read_to_string(root.join(&path)).with_context(|| format!("read {path}"))?;
            Ok((path, text))
        })
        .collect::<Result<_>>()?;
    let mut exports = BTreeSet::new();
    let mut wanted = BTreeSet::new();
    for (_, ir) in &rust {
        let (defined, declared) = interface(ir);
        exports.extend(defined);
        wanted.extend(declared);
    }
    // (the hooks: the oracle's, as for the source-built oracle)
    let config: Value = serde_json::from_slice(&fs::read(
        root.join(format!("config/{version}/config.json")),
    )?)?;
    let mut hooks: Vec<String> = config["oracle"]["hooks"]
        .as_array()
        .context("config.json lists no oracle hooks")?
        .iter()
        .filter_map(Value::as_str)
        .map(Into::into)
        .collect();
    for hook in producer::ARGUMENT_HOOKS
        .into_iter()
        .chain(producer::ARENA_HOOKS)
        .chain(producer::SOURCE_HOOKS)
    {
        if !hooks.iter().any(|h| h == hook) {
            hooks.push(hook.into());
        }
    }
    let hooks: Vec<String> = hooks
        .iter()
        .map(|h| h.strip_prefix('_').unwrap_or(h).to_owned())
        .collect();
    let mut places = BTreeMap::new();
    for hook in &hooks {
        let wrapper = if hook == VARIADIC.0 { VARIADIC.1 } else { hook };
        ensure!(
            exports.contains(wrapper),
            "the Rust hooks do not define {wrapper}"
        );
        let place = resolver.resolve(hook)?;
        ensure!(place.function, "{hook} is not a function");
        places.insert(hook.clone(), place);
    }
    // (a name the hooks define and gc2 too, but for a hook's own function: renamed)
    let mut renames: BTreeMap<String, String> = BTreeMap::new();
    let mut renamed = vec![];
    for name in &exports {
        let own = places.get(name).is_some_and(|p| p.name == *name);
        if resolver.names.contains(name) && !own {
            renames.insert(name.clone(), ir_name(&format!("{name}__oracle")));
            renamed.push(name.clone());
        }
    }
    let exported = renames.clone();
    let linked = |name: &str| {
        exported
            .get(name)
            .map_or(name.to_owned(), |r| r.trim_start_matches('@').to_owned())
    };
    let units = wasm_build::units(root, version)?;
    let mut edits: BTreeMap<String, UnitEdit> = BTreeMap::new();
    let mut outcome = Outcome {
        output: output.clone(),
        hooks: vec![],
        imports: vec![],
        renamed,
    };
    for hook in &hooks {
        let place = &places[hook];
        let wrapper = if hook == VARIADIC.0 {
            place.name.clone()
        } else {
            linked(hook)
        };
        let edit = edits.entry(place.unit.clone()).or_default();
        ensure!(
            edit.hooks
                .insert(place.name.clone(), (hook.clone(), wrapper))
                .is_none(),
            "two hooks wrap {}",
            place.name
        );
        outcome.hooks.push(Hook {
            hook: hook.clone(),
            function: place.name.clone(),
            unit: place.unit.clone(),
            matching: true,
        });
    }
    for name in &wanted {
        if let Some(hook) = name.strip_suffix("__retail") {
            ensure!(
                hooks.iter().any(|h| h == hook),
                "{name}: {hook} is not a hook"
            );
            continue;
        }
        if [ARENA_GLOBALS, FORCE_RETAIL, TRACE].contains(&name.as_str())
            || exports.contains(name)
            || name.starts_with("llvm.")
            || !resolver.known(name)
        {
            continue;
        }
        let place = resolver.resolve(name)?;
        let target = if place.offset == 0 {
            ir_name(&place.name)
        } else {
            format!(
                "getelementptr (i8, ptr {}, i32 {})",
                ir_name(&place.name),
                place.offset
            )
        };
        outcome.imports.push(Import {
            name: name.clone(),
            resolved: if place.offset == 0 {
                place.name.clone()
            } else {
                format!("{}+{:#x}", place.name, place.offset)
            },
            unit: Some(place.unit.clone()),
        });
        if place.name != *name || place.offset != 0 {
            renames.insert(name.clone(), target);
        }
        // (a static, aliased: known once the unit is compiled; `common`'s are global)
        if units.iter().any(|u| u.name == place.unit) {
            edits.entry(place.unit.clone()).or_default();
        }
    }
    let mut thunks: Vec<String> = vec![];
    let mut replaced: BTreeMap<String, String> = BTreeMap::new();
    let compiled: Vec<Result<(String, String)>> = std::thread::scope(|scope| {
        let tasks: Vec<_> = edits
            .keys()
            .map(|name| {
                let unit = units.iter().find(|u| u.name == *name);
                let work = &work;
                scope.spawn(move || -> Result<(String, String)> {
                    let unit =
                        unit.with_context(|| format!("{name} is not a unit of the module"))?;
                    let path = format!("{work}/{name}.ll");
                    if let Some(parent) = root.join(&path).parent() {
                        fs::create_dir_all(parent)?;
                    }
                    let mut arguments = unit.arguments.clone();
                    arguments.extend(
                        [
                            wasm_build::PROBES,
                            "-S",
                            "-emit-llvm",
                            "-Xclang",
                            "-disable-llvm-passes",
                            &unit.input,
                            "-o",
                            &path,
                        ]
                        .map(String::from),
                    );
                    run_tool(root, clang, &arguments)?;
                    Ok((name.clone(), fs::read_to_string(root.join(&path))?))
                })
            })
            .collect();
        tasks
            .into_iter()
            .map(|t| t.join().unwrap_or_else(|_| bail!("a compile panicked")))
            .collect()
    });
    let mut irs = BTreeMap::new();
    for c in compiled {
        let (name, ir) = c?;
        irs.insert(name, ir);
    }
    // (the names the hooks read: gc2's global, or a static given an alias)
    for import in &outcome.imports {
        let unit = import.unit.as_ref().unwrap();
        let place = resolver.resolve(&import.name)?;
        let Some(ir) = irs.get(unit) else {
            continue;
        };
        let static_ = ir.lines().any(|line| {
            line.starts_with('@')
                && NAME
                    .captures(line)
                    .is_some_and(|c| name_of(&c) == place.name && c.get(0).unwrap().start() == 0)
                && line.contains(" = internal ")
        });
        let internal_function = ir.lines().any(|line| {
            line.starts_with("define internal ")
                && NAME
                    .captures(line)
                    .is_some_and(|c| name_of(&c) == place.name)
        });
        if internal_function {
            // (called through a global entry)
            let entry = format!("{}__oracle", import.name);
            edits
                .get_mut(unit)
                .unwrap()
                .entries
                .push((entry.clone(), place.name.clone()));
            renames.insert(import.name.clone(), ir_name(&entry));
        } else if static_ {
            let alias = format!("{}__oracle", import.name);
            edits.get_mut(unit).unwrap().aliases.push((
                alias.clone(),
                place.name.clone(),
                place.offset,
            ));
            renames.insert(import.name.clone(), ir_name(&alias));
        } else {
            ensure!(
                interface(ir).0.contains(&place.name),
                "{}: {unit} does not define {}",
                import.name,
                place.name
            );
        }
    }
    for edit in edits.values_mut() {
        edit.retail = resolver.names.clone();
    }
    let mut jobs = vec![];
    for (name, edit) in &edits {
        if edit.hooks.is_empty() && edit.aliases.is_empty() && edit.entries.is_empty() {
            continue;
        }
        let (ir, variadic) = edit_unit(&irs[name], edit).with_context(|| format!("edit {name}"))?;
        thunks.extend(variadic);
        let path = format!("{work}/{name}.oracle.ll");
        fs::write(root.join(&path), ir)?;
        let object = format!("{work}/{name}.o");
        jobs.push(codegen_arguments(&path, &object));
        replaced.insert(name.clone(), object);
    }
    let mut extra = vec![];
    for (path, ir) in &rust {
        let name = Path::new(path).file_stem().unwrap().to_string_lossy();
        let edited = format!("{work}/rust-{name}.ll");
        fs::write(root.join(&edited), edit_rust(ir, &renames))?;
        let object = format!("{work}/rust-{name}.o");
        jobs.push(codegen_arguments(&edited, &object));
        extra.push(object);
    }
    // (oracle.c, with the arguments of MSL's sources)
    ensure!(
        thunks.len() <= 1 && thunks.iter().all(|g| places[VARIADIC.0].name == *g),
        "variadic hooks other than {}: {thunks:?}",
        VARIADIC.0
    );
    let like = units
        .iter()
        .find(|u| u.name == "wasm/src/msl_extras.c")
        .context("wasm/src/msl_extras.c is not a unit")?;
    let object = format!("{work}/oracle.c.o");
    let mut arguments = like.arguments.clone();
    arguments.extend([
        format!("-DORACLE_IRO_DUMP={}", places[VARIADIC.0].name),
        format!("-DORACLE_IRO_DUMP_ARGUMENTS={}", linked(VARIADIC.1)),
        "-c".into(),
        ORACLE_C.into(),
        "-o".into(),
        object.clone(),
    ]);
    jobs.push(arguments);
    extra.push(object);
    std::thread::scope(|scope| -> Result<()> {
        let tasks: Vec<_> = jobs
            .iter()
            .map(|arguments| scope.spawn(move || run_tool(root, clang, arguments).map(drop)))
            .collect();
        for t in tasks {
            t.join().unwrap_or_else(|_| bail!("a compile panicked"))?;
        }
        Ok(())
    })?;
    // (the link: the oracle's objects, the edited units in their place, and the hooks')
    let mut objects: Vec<String> = wasm_build::objects(root, version, true)?
        .into_iter()
        .map(|(unit, object)| replaced.get(&unit).cloned().unwrap_or(object))
        .collect();
    objects.extend(extra);
    let list = format!("{work}/objects.rsp");
    fs::write(root.join(&list), objects.join("\n"))?;
    let mut arguments: Vec<String> = wasm_build::LINK_FLAGS
        .iter()
        .map(|s| s.to_string())
        .collect();
    arguments.extend([format!("@{list}"), "-o".into(), output.clone()]);
    // (a hook declared otherwise than its function would trap when called)
    let diagnostics = run_tool(root, wasm_ld, &arguments)?;
    if diagnostics.contains("signature mismatch") {
        let _ = fs::remove_file(root.join(&output));
        bail!(
            "the hooks' and gc2's functions differ:
{diagnostics}"
        );
    }
    // (the oracle imports only what the compiler does: nothing of the hooks is left undefined)
    let stock = root.join(wasm_build::output(version));
    if stock.is_file() {
        let extra: Vec<String> = imports(&fs::read(root.join(&output))?)?
            .difference(&imports(&fs::read(&stock)?)?)
            .cloned()
            .collect();
        if !extra.is_empty() {
            let _ = fs::remove_file(root.join(&output));
            bail!("the oracle module leaves undefined: {}", extra.join(", "));
        }
    }
    fs::write(
        root.join(format!("{work}/hooks.json")),
        serde_json::to_vec_pretty(&outcome)?,
    )?;
    Ok(outcome)
}

/// VERSION's edges (after `wasm_build`'s): the Rust IR, and the oracle module.
pub fn edges(root: &Path, version: &str) -> Result<Vec<String>> {
    let [hooks, frontend] = rust_ir(version);
    let sources = |names: &[&str]| {
        names
            .iter()
            .map(|name| format!("crates/mwcc-oracle-hooks/src/{name}"))
            .collect::<Vec<_>>()
            .join(" ")
    };
    let objects: Vec<String> = wasm_build::objects(root, version, true)?
        .into_iter()
        .map(|(_, object)| object)
        .collect();
    let gc2 = format!("gc2/config/{version}");
    Ok(vec![
        "rule wasm_rust_hooks".into(),
        "  command = $tools build-hooks $out --wasm".into(),
        "  description = RUST HOOKS (wasm32) $out".into(),
        format!(
            "build {hooks}: wasm_rust_hooks {} | $tools_dep rust-toolchain.toml",
            sources(&producer::SOURCES)
        ),
        "rule wasm_rust_frontend".into(),
        format!("  command = $tools build-hooks $out --wasm --frontend-version {version}"),
        "  description = RUST FRONTEND (wasm32) $out".into(),
        format!(
            "build {frontend}: wasm_rust_frontend {} | $tools_dep rust-toolchain.toml",
            sources(&producer::FRONTEND_SOURCES)
        ),
        "rule wasm_oracle".into(),
        format!(
            "  command = $tools source-only {version} --oracle --wasm --clang $clang --wasm-ld $wasm_ld"
        ),
        "  description = WASM-LD (oracle) $out".into(),
        format!(
            "build {}: wasm_oracle {hooks} {frontend} | {} {ORACLE_C} config/{version}/symbols.txt config/{version}/config.json {gc2}/symbols.txt {gc2}/splits.txt $tools_dep",
            wasm_build::oracle_output(version),
            objects.join(" ")
        ),
        String::new(),
    ])
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn a_definition_becomes_a_declaration_and_a_thunk() {
        let h = header(
            "define dso_local noundef i32 @f(ptr noundef %0, %struct.S* byval(%struct.S) %s) #0 {",
        )
        .unwrap();
        assert!(!h.internal && !h.variadic);
        assert_eq!(
            declaration("g", &h),
            "declare noundef i32 @g(ptr noundef, %struct.S* byval(%struct.S))"
        );
        let t = thunk("f", "g", &h).unwrap();
        assert!(t.contains(
            "%r = tail call noundef i32 @g(ptr noundef %a0, %struct.S* byval(%struct.S) %a1)"
        ));
        assert!(t.contains("ret i32 %r"));
    }

    #[test]
    fn a_hooked_unit_calls_the_wrapper_but_from_itself() {
        let ir = "define internal void @g(i32 noundef %0) #0 {\n  call void @g(i32 1)\n  ret void\n}\ndefine void @k() {\n  call void @g(i32 2)\n  ret void\n}\n";
        let mut edit = UnitEdit::default();
        edit.hooks
            .insert("g".into(), ("hook".into(), "hook".into()));
        let (out, _) = edit_unit(ir, &edit).unwrap();
        assert!(out.contains(
            "define void @hook__retail(i32 noundef %0) #0 {\n  call void @hook__retail(i32 1)"
        ));
        assert!(out.contains("call void @hook(i32 2)"));
        assert!(out.contains("declare void @hook(i32 noundef)"));
        assert!(!out.contains("define void @g("));
    }

    #[test]
    fn the_rust_names_become_gc2s() {
        let ir = "@copts = external global [0 x i8]\n@ipa = external global i8\ndefine void @f() {\n  load i8, ptr @ipa\n  load i8, ptr @copts\n  ret void\n}\n";
        let renames = BTreeMap::from([(
            "ipa".to_owned(),
            "getelementptr (i8, ptr @copts, i32 248)".to_owned(),
        )]);
        let out = edit_rust(ir, &renames);
        assert!(out.contains("load i8, ptr getelementptr (i8, ptr @copts, i32 248)"));
        assert!(!out.contains("@ipa"));
        assert_eq!(out.matches("@copts = external").count(), 1);
    }

    #[test]
    fn a_function_inlined_in_a_hooks_function_gets_a_copy_that_calls_it_retail() {
        // (s, which the original inlined everywhere, calls g back; t, which it has, does too)
        let ir = "define internal void @g(i32 %0) {\n  call void @s(i32 1)\n  call void @t()\n  ret void\n}\n\
                  define internal void @s(i32 %0) {\n  call void @g(i32 2)\n  ret void\n}\n\
                  define void @t() {\n  call void @g(i32 3)\n  ret void\n}\n";
        let mut edit = UnitEdit::default();
        edit.hooks
            .insert("g".into(), ("hook".into(), "hook".into()));
        edit.retail = BTreeSet::from(["g".to_owned(), "t".to_owned()]);
        let (out, _) = edit_unit(ir, &edit).unwrap();
        assert!(out.contains("call void @s__in_hook(i32 1)"));
        assert!(out.contains(
            "define internal void @s__in_hook(i32 %0) {\n  call void @hook__retail(i32 2)"
        ));
        // (the others' calls go to the wrapper)
        assert!(out.contains("define internal void @s(i32 %0) {\n  call void @hook(i32 2)"));
        assert!(out.contains("define void @t() {\n  call void @hook(i32 3)"));
        assert!(out.contains("call void @t()"));
    }
}
