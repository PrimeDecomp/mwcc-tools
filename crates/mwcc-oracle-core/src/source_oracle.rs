//! The oracle made of a source tree's sources alone: the source-only compiler (crate::source_only) with the Rust
//! hooks linked in, `build/<version>/<tree>/source-oracle/mwcceppc.exe`. Neither the original nor its split is read.
//!
//! The hooks name what they wrap and read by `config/<version>/symbols.txt`'s names (`fn_00507070`,
//! `data_00582468`, ...). Each name is taken to the tree's symbol at the same address (the tree's `symbols.txt`):
//! - a hook's function is renamed `<hook>__retail` in its object and made global, and the calls to it (from its
//!   object, outside its own body, and from the other objects) call `<hook>`, the Rust wrapper, as in the oracle;
//! - a function or variable the hooks read is given the oracle's name as a second, global name in its object (a
//!   static variable, which MWCC addresses through its section, at its offset in the unit's section, from the tree's
//!   `splits.txt`), or, when it is a common block (which has no section yet), is renamed in the Rust objects, the
//!   offset added to each of their references.
//!
//! Only the hooks are linked: the guarded probes the oracle installs into the original's split objects (dispatch,
//! call arguments, arenas, source nodes, counters, coalescing) are not, so their `pv.*` records are absent. The two
//! names the hooks import from those probes' islands are stand-ins: the arena globals are zero (the arena stream
//! records `invalid_globals`) and the forced-GPR entry, which only an installed probe calls, traps.
use crate::{
    coff::{self, Builder, Object, SymbolEdit},
    executable::{self, Symbol},
    producer, source_only,
    source_tree::SourceTree,
};
use anyhow::{Context, Result, bail, ensure};
use serde::Serialize;
use serde_json::Value;
use std::{
    collections::{BTreeMap, BTreeSet},
    fs,
    path::Path,
};

use crate::coff::{EXTERNAL, FUNCTION_TYPE, STATIC};
/// The imports the hooks take from the probes' islands, and their stand-ins.
pub const ARENA_GLOBALS: &str = crate::source_arena_probe::GLOBALS;
pub const FORCE_RETAIL: &str = crate::call_argument_probe::FORCE_RETAIL;

/// Where VERSION's source-built oracle is linked.
pub fn output(tree: &SourceTree) -> String {
    format!("{}/source-oracle/mwcceppc.exe", tree.build_dir)
}

/// The Rust objects it links (`mwcc-tools build-hooks`): the common hooks, the version's frontend and the trace
/// writer.
pub fn rust_objects(tree: &SourceTree) -> [String; 3] {
    let directory = format!("{}/source-oracle/rust", tree.build_dir);
    [
        format!("{directory}/hooks.obj"),
        format!("{directory}/frontend.obj"),
        format!("{directory}/trace.obj"),
    ]
}

/// A hook point: the oracle's name, the tree's function and the unit it is in.
#[derive(Clone, Debug, Serialize)]
pub struct Hook {
    pub hook: String,
    pub function: String,
    pub unit: String,
    pub matching: bool,
}

/// A name the hooks read: the oracle's name and where it is in the tree's build.
#[derive(Clone, Debug, Serialize)]
pub struct Import {
    pub name: String,
    pub resolved: String,
    pub unit: Option<String>,
}

#[derive(Debug, Serialize)]
pub struct Outcome {
    pub output: String,
    pub hooks: Vec<Hook>,
    pub imports: Vec<Import>,
}

/// Where a name of the oracle's is in the tree's objects.
#[derive(Clone, Debug)]
enum Place {
    /// In object OBJECT's section SECTION (from 1) at VALUE; SYMBOL is the tree's symbol there, if the object names
    /// it.
    Defined {
        object: usize,
        section: i16,
        value: u32,
        symbol: Option<u32>,
    },
    /// OFFSET bytes into the common block NAME.
    Common { name: String, offset: u32 },
}

fn symbols_by_name(path: &Path) -> Result<BTreeMap<String, Symbol>> {
    let text = fs::read_to_string(path).with_context(|| format!("read {}", path.display()))?;
    Ok(executable::read_symbols(&text)?
        .into_iter()
        .map(|s| (s.name.clone(), s))
        .collect())
}

/// Is SYMBOL an object's section symbol (a static named as its section, with the section's auxiliary record)?
fn is_section_symbol(object: &Object, symbol: &coff::ObjectSymbol) -> bool {
    symbol.storage == STATIC
        && symbol.section > 0
        && symbol.value == 0
        && symbol.auxiliary.len() == 1
        && object
            .sections
            .get(symbol.section as usize - 1)
            .is_some_and(|s| s.name == symbol.name)
}

/// A symbol of the tree's objects: in OBJECT, at VALUE in SECTION (from 1; 0 for a common block).
struct Definition {
    object: usize,
    symbol: u32,
    section: i16,
    value: u32,
}

struct Resolver<'a> {
    ours: BTreeMap<String, Symbol>,
    /// The tree's symbols, by section, sorted by address.
    theirs: BTreeMap<String, Vec<Symbol>>,
    splits: executable::Splits,
    objects: &'a [(String, Vec<u8>)],
    /// name → the named definitions and common blocks.
    definitions: BTreeMap<String, Vec<Definition>>,
}

impl<'a> Resolver<'a> {
    fn new(
        root: &Path,
        version: &str,
        config_dir: &str,
        objects: &'a [(String, Vec<u8>)],
    ) -> Result<Self> {
        let ours = symbols_by_name(&root.join(format!("config/{version}/symbols.txt")))?;
        let mut theirs: BTreeMap<String, Vec<Symbol>> = BTreeMap::new();
        for s in symbols_by_name(&root.join(format!("{config_dir}/symbols.txt")))?.into_values() {
            theirs.entry(s.section.clone()).or_default().push(s);
        }
        for list in theirs.values_mut() {
            list.sort_by_key(|s| s.address);
        }
        let splits = executable::read_splits(&fs::read_to_string(
            root.join(format!("{config_dir}/splits.txt")),
        )?)?;
        let mut definitions: BTreeMap<String, Vec<_>> = BTreeMap::new();
        for (index, (_, data)) in objects.iter().enumerate() {
            let object = Object::parse(data)?;
            for (&number, s) in &object.symbols {
                let defined = s.section > 0 && (s.storage == EXTERNAL || s.storage == STATIC);
                let common = s.storage == EXTERNAL && s.section == 0 && s.value > 0;
                if (defined && !is_section_symbol(&object, s)) || common {
                    definitions
                        .entry(s.name.clone())
                        .or_default()
                        .push(Definition {
                            object: index,
                            symbol: number,
                            section: s.section,
                            value: s.value,
                        });
                }
            }
        }
        Ok(Self {
            ours,
            theirs,
            splits,
            objects,
            definitions,
        })
    }

    /// Where the oracle's NAME (of SIZE bytes, at least one) is, and the tree's name for it.
    fn resolve(&self, name: &str) -> Result<(Place, String)> {
        let ours = self
            .ours
            .get(name)
            .with_context(|| format!("{name} is not in the oracle's symbols.txt"))?;
        let address = ours.address;
        let size = executable::size(&ours.options)?.max(1);
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
            .with_context(|| format!("{name}: the tree has no symbol at {address:#x}"))?;
        let offset = address - theirs.address;
        ensure!(
            !function || offset == 0,
            "{name} ({address:#x}) is inside the tree's {}",
            theirs.name
        );
        let label = if offset == 0 {
            theirs.name.clone()
        } else {
            format!("{}+{offset:#x}", theirs.name)
        };
        let found = self.definitions.get(&theirs.name);
        let defined: Vec<_> = found
            .into_iter()
            .flatten()
            .filter(|d| d.section > 0)
            .collect();
        let common: Vec<_> = found
            .into_iter()
            .flatten()
            .filter(|d| d.section == 0)
            .collect();
        if let [d] = defined.as_slice() {
            return Ok((
                Place::Defined {
                    object: d.object,
                    section: d.section,
                    value: d.value + offset,
                    symbol: (offset == 0).then_some(d.symbol),
                },
                label,
            ));
        }
        ensure!(
            defined.is_empty(),
            "{name}: the tree's {} is defined by {} objects",
            theirs.name,
            defined.len()
        );
        if !common.is_empty() {
            return Ok((
                Place::Common {
                    name: theirs.name.clone(),
                    offset,
                },
                label,
            ));
        }
        ensure!(
            !function,
            "{name}: no object defines the tree's {}",
            theirs.name
        );
        // (a static variable: at its offset in its unit's section, which the object does not name)
        let (unit, range) = self
            .splits
            .units
            .iter()
            .find_map(|u| {
                u.ranges
                    .iter()
                    .find(|r| r.section == ours.section && r.start <= address && address < r.end)
                    .map(|r| (u, r))
            })
            .with_context(|| format!("{name}: no unit's {} has {address:#x}", ours.section))?;
        let index = self
            .objects
            .iter()
            .position(|(source, _)| *source == unit.name)
            .with_context(|| format!("{name}: {} is not compiled", unit.name))?;
        let object = Object::parse(&self.objects[index].1)?;
        let sections: Vec<_> = object
            .sections
            .iter()
            .enumerate()
            .filter(|(_, s)| s.name == ours.section)
            .collect();
        let [(number, section)] = sections.as_slice() else {
            bail!(
                "{name}: {} has {} {} sections",
                unit.name,
                sections.len(),
                ours.section
            );
        };
        let number = *number as i16 + 1;
        ensure!(
            section.size == range.end - range.start,
            "{name}: {}'s {} is {:#x} bytes, not the original's {:#x}",
            unit.name,
            ours.section,
            section.size,
            range.end - range.start
        );
        let at = address - range.start;
        // (checked: the object refers to it there)
        let referred = object.sections.iter().any(|s| {
            s.relocations.iter().any(|r| {
                let target = &object.symbols[&r.symbol];
                target.section == number
                    && is_section_symbol(&object, target)
                    && matches!(r.kind, 6 | 20)
                    && s.data
                        .get(r.offset as usize..r.offset as usize + 4)
                        .is_some_and(|f| {
                            let field = u32::from_le_bytes(f.try_into().unwrap());
                            at <= field && field < at + size
                        })
            })
        });
        ensure!(
            referred,
            "{name}: {} never refers to its {} at {at:#x}",
            unit.name,
            ours.section
        );
        Ok((
            Place::Defined {
                object: index,
                section: number,
                value: at,
                symbol: None,
            },
            format!("{} {}+{at:#x}", unit.name, ours.section),
        ))
    }
}

/// The undefined and the defined global names of OBJECTS.
fn interface(objects: &[(String, Vec<u8>)]) -> Result<(BTreeSet<String>, BTreeSet<String>)> {
    let mut defined = BTreeSet::new();
    let mut referenced = BTreeSet::new();
    for (_, data) in objects {
        for s in Object::parse(data)?.symbols.into_values() {
            if s.storage != EXTERNAL {
                continue;
            }
            if s.section > 0 || s.value > 0 {
                defined.insert(s.name);
            } else if s.section == 0 {
                referenced.insert(s.name);
            }
        }
    }
    let undefined = referenced.difference(&defined).cloned().collect();
    Ok((undefined, defined))
}

/// The stand-ins for what the hooks import from the probes' islands.
fn stand_ins() -> Result<Vec<u8>> {
    let mut builder = Builder::default();
    let text = builder.section(".text", 0x6030_0020, vec![0x0f, 0x0b], 2, vec![])?;
    builder.add_symbol(FORCE_RETAIL, 0, text, FUNCTION_TYPE, EXTERNAL, &[])?;
    let data = builder.section(".data", 0xc030_0040, vec![0; 12], 12, vec![])?;
    builder.add_symbol(ARENA_GLOBALS, 0, data, 0, EXTERNAL, &[])?;
    builder.finish()
}

/// Link VERSION's source-built oracle (its sources compiled beforehand, and the Rust objects built).
pub fn run(root: &Path, version: &str) -> Result<Outcome> {
    let mut prepared = source_only::prepare(root, version)?;
    let output = output(&prepared.tree);
    let config_dir = prepared.tree.config_dir.clone();
    let prefix = prepared.tree.prefix.clone();
    let mut rust = rust_objects(&prepared.tree)
        .into_iter()
        .map(|path| {
            let data = fs::read(root.join(&path)).with_context(|| format!("read {path}"))?;
            Ok((path, data))
        })
        .collect::<Result<Vec<_>>>()?;
    let config: Value = serde_json::from_slice(&fs::read(
        root.join(format!("config/{version}/config.json")),
    )?)?;
    let state: Value =
        serde_json::from_slice(&fs::read(root.join(format!("{config_dir}/state.json")))?)?;
    let matching: BTreeSet<&str> = state["matching"]
        .as_array()
        .context("state.json lacks matching")?
        .iter()
        .filter_map(Value::as_str)
        .collect();
    let (imports, exports) = interface(&rust)?;
    // (the hooks: the oracle's, each wrapper's retail entry imported)
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
    for hook in &hooks {
        // (a wrapper may replace its function, never calling the retail one: IroDump_Print)
        ensure!(
            exports.contains(hook),
            "the Rust objects do not wrap {hook}"
        );
    }
    for name in &imports {
        if let Some(hook) = name.strip_suffix("__retail") {
            ensure!(
                hooks.iter().any(|h| h == hook),
                "{name}: {hook} is not a hook"
            );
        }
    }
    // (what the tree's objects, the runtime and the import libraries define as it is)
    let (_, mut provided) = interface(&prepared.objects)?;
    provided.extend(interface(&prepared.runtime)?.1);
    for name in source_only::IMPORT_LIBRARIES {
        let text = fs::read_to_string(root.join(format!("{prefix}src/imports/{name}.def")))?;
        let (_, exports) = coff::definition(&text)?;
        provided.extend(exports.iter().map(|e| format!("_{}", e.symbol)));
    }
    for name in &exports {
        ensure!(
            !provided.contains(name) || hooks.contains(name),
            "the Rust objects and the tree both define {name}"
        );
    }
    let resolver = Resolver::new(root, version, &config_dir, &prepared.objects)?;
    let mut edits: BTreeMap<usize, SymbolEdit> = BTreeMap::new();
    let mut rust_renames: BTreeMap<String, (String, u32)> = BTreeMap::new();
    let mut outcome = Outcome {
        output: output.clone(),
        hooks: vec![],
        imports: vec![],
    };
    let counts: Vec<u32> = prepared
        .objects
        .iter()
        .map(|(_, data)| Ok(Object::parse(data)?.symbol_count))
        .collect::<Result<_>>()?;
    let add = |edits: &mut BTreeMap<usize, SymbolEdit>,
               object: usize,
               symbol: (String, u32, i16, u16, u8)|
     -> u32 {
        let edit = edits.entry(object).or_default();
        edit.added.push(symbol);
        counts[object] + edit.added.len() as u32 - 1
    };
    // (the hooks' functions: renamed, and called through the wrapper)
    let mut redirected: BTreeMap<String, String> = BTreeMap::new();
    for hook in &hooks {
        let (place, label) = resolver.resolve(hook)?;
        let Place::Defined {
            object: index,
            section,
            value,
            symbol: Some(symbol),
        } = place
        else {
            bail!("{hook}: the tree's {label} is not a function its object names");
        };
        let object = Object::parse(&prepared.objects[index].1)?;
        let definition = &object.symbols[&symbol];
        // (its body: up to the next function in its section)
        let end = object
            .symbols
            .values()
            .filter(|s| s.section == section && s.value > value && s.kind & FUNCTION_TYPE != 0)
            .map(|s| s.value)
            .min()
            .unwrap_or(object.sections[section as usize - 1].size);
        let wrapper = add(
            &mut edits,
            index,
            (hook.clone(), 0, 0, FUNCTION_TYPE, EXTERNAL),
        );
        let edit = edits.entry(index).or_default();
        edit.renames.insert(symbol, format!("{hook}__retail"));
        edit.storage.insert(symbol, EXTERNAL);
        for (number, s) in object.sections.iter().enumerate() {
            let number = number as i16 + 1;
            for (at, r) in s.relocations.iter().enumerate() {
                let inside = number == section && value <= r.offset && r.offset < end;
                let field = s.data.get(r.offset as usize..r.offset as usize + 4);
                if r.symbol == symbol
                    && !inside
                    && matches!(r.kind, 6 | 20)
                    && field == Some(&[0; 4][..])
                {
                    edit.retarget.insert((number, at), wrapper);
                }
            }
        }
        if definition.storage == EXTERNAL && definition.name != *hook {
            redirected.insert(definition.name.clone(), hook.clone());
        }
        let unit = prepared.objects[index].0.clone();
        outcome.hooks.push(Hook {
            hook: hook.clone(),
            function: definition.name.clone(),
            matching: matching.contains(unit.as_str()),
            unit,
        });
    }
    // (the other objects' calls to a hook's global function: to the wrapper)
    for (index, (_, data)) in prepared.objects.iter().enumerate() {
        let object = Object::parse(data)?;
        for (&number, s) in &object.symbols {
            if s.storage == EXTERNAL
                && s.section == 0
                && s.value == 0
                && let Some(hook) = redirected.get(&s.name)
            {
                edits
                    .entry(index)
                    .or_default()
                    .renames
                    .insert(number, hook.clone());
            }
        }
    }
    // (the names the hooks read)
    for name in &imports {
        if name.ends_with("__retail")
            || [ARENA_GLOBALS, FORCE_RETAIL].contains(&name.as_str())
            || (provided.contains(name) && !resolver.ours.contains_key(name))
        {
            continue;
        }
        let (place, label) = resolver.resolve(name)?;
        let unit = match &place {
            Place::Defined { object, .. } => Some(prepared.objects[*object].0.clone()),
            Place::Common { .. } => None,
        };
        match place {
            Place::Defined {
                object,
                section,
                value,
                symbol,
            } => {
                let named = symbol.is_some_and(|s| {
                    let o = Object::parse(&prepared.objects[object].1).ok();
                    o.is_some_and(|o| {
                        o.symbols[&s].name == *name && o.symbols[&s].storage == EXTERNAL
                    })
                });
                if !named {
                    let kind = if resolver.ours[name].section == ".text" {
                        FUNCTION_TYPE
                    } else {
                        0
                    };
                    // (a name the tree gives something else: CMangler_GetLinkName)
                    let alias = if provided.contains(name) {
                        let alias = format!("{name}__oracle");
                        rust_renames.insert(name.clone(), (alias.clone(), 0));
                        alias
                    } else {
                        name.clone()
                    };
                    add(&mut edits, object, (alias, value, section, kind, EXTERNAL));
                }
            }
            Place::Common {
                name: block,
                offset,
            } => {
                if block != *name || offset != 0 {
                    rust_renames.insert(name.clone(), (block, offset));
                }
            }
        }
        outcome.imports.push(Import {
            name: name.clone(),
            resolved: label,
            unit,
        });
    }
    for (index, edit) in &edits {
        let data = &mut prepared.objects[*index].1;
        *data = coff::edit_symbols(data, edit)?;
    }
    // (the Rust objects' references to common blocks)
    for (_, data) in &mut rust {
        let object = Object::parse(data)?;
        let mut edit = SymbolEdit::default();
        for (&number, s) in &object.symbols {
            if let Some((block, _)) = rust_renames.get(&s.name)
                && s.section == 0
                && s.storage == EXTERNAL
            {
                edit.renames.insert(number, block.clone());
            }
        }
        for (number, section) in object.sections.iter().enumerate() {
            for r in &section.relocations {
                if let Some((_, offset)) = rust_renames.get(&object.symbols[&r.symbol].name)
                    && *offset != 0
                {
                    ensure!(
                        matches!(r.kind, 6 | 20),
                        "a Rust object refers to {} other than by address",
                        object.symbols[&r.symbol].name
                    );
                    edit.addends
                        .push((number as i16 + 1, r.offset, i32::try_from(*offset)?));
                }
            }
        }
        if !edit.renames.is_empty() {
            *data = coff::edit_symbols(data, &edit)?;
        }
    }
    rust.push(("stand-ins".into(), stand_ins()?));
    let extra: Vec<_> = rust
        .into_iter()
        .map(|(path, data)| {
            let name = Path::new(&path)
                .file_stem()
                .map_or(path.clone(), |s| s.to_string_lossy().into_owned());
            (format!("oracle-{name}"), data)
        })
        .collect();
    source_only::link_compiler(root, &prepared, &extra, &output)?;
    fs::write(
        root.join(&output).with_file_name("hooks.json"),
        serde_json::to_vec_pretty(&outcome)?,
    )?;
    Ok(outcome)
}
