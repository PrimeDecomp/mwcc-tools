//! A unit's object scored against its target by objdiff-core, as `objdiff-cli diff -c functionRelocDiffs=data_value
//! -u UNIT` scores it: each target function's match, its rows, the kit's instruction statistics (decomp_common's
//! `instruction_stats`, decomp_experiment's `score_symbols`), residual-scan's classification of the differing rows,
//! and mwdec's profile of what differs ([`crate::profile`]).
use crate::{profile, strict, words};
use anyhow::{Result, anyhow};
use objdiff_core::{
    diff::{
        ConfigPropertyId, DiffObjConfig, DiffSide, InstructionDiffKind, InstructionDiffRow,
        MappingConfig, diff_objs, display::InstructionPart,
    },
    obj::{self, InstructionArg, InstructionArgValue, Object, SymbolKind},
};
use serde::Serialize;
use std::{
    collections::{BTreeMap, HashMap},
    fmt::Write,
    str::FromStr,
};

/// The kit's comparison policy: relocation targets compared by the data they refer to.
pub const RELOC_POLICY: (&str, &str) = ("functionRelocDiffs", "data_value");

#[derive(Clone, Debug, Default)]
pub struct ScoreOptions {
    /// objdiff's options for the unit (`id`, `value`, as `-c id=value` gives them): objdiff.json's, applied before
    /// [`RELOC_POLICY`].
    pub config: Vec<(String, String)>,
    /// objdiff.json's `symbol_mappings` of the unit (target name, ours).
    pub mappings: BTreeMap<String, String>,
    /// Keep every function's rows (they are large; the statistics, residual and profile are always computed).
    pub rows: bool,
    /// Only these target functions (by name or demangled name); `fuzzy` is then theirs.
    pub symbols: Option<Vec<String>>,
    /// Each paired function compared at the strict level too ([`crate::strict`]; with the target's
    /// [`Target::with_externs`] for the literals other objects hold), which the profile then uses.
    pub strict: bool,
}

#[derive(Clone, Debug, Serialize)]
pub struct UnitScore {
    /// The size-weighted match of the target's functions (an unpaired one counts 0); `None` without code. It is
    /// report.json's `fuzzy_match_percent` when scored as objdiff's report scores ([`report_config`], through
    /// [`score_with`], for a unit that is not complete); under the kit's policy it can be lower.
    pub fuzzy: Option<f64>,
    /// The target's functions, in its order.
    pub functions: Vec<FunctionScore>,
    /// Our functions the target does not have.
    pub extra: Vec<String>,
}

#[derive(Clone, Debug, Serialize)]
pub struct FunctionScore {
    pub name: String,
    pub demangled: Option<String>,
    /// The target's size, in bytes.
    pub size: u64,
    /// objdiff's match (`None`: unpaired).
    pub match_percent: Option<f32>,
    /// Our function it is paired with.
    pub paired: Option<String>,
    pub stats: Stats,
    pub residual: Residual,
    /// mwdec's profile (`None`: unpaired).
    pub profile: Option<profile::Profile>,
    /// The strict level (with [`ScoreOptions::strict`], for a paired function).
    #[serde(skip_serializing_if = "Option::is_none")]
    pub strict: Option<strict::Strict>,
    #[serde(skip_serializing_if = "Vec::is_empty")]
    pub rows: Vec<Row>,
}

/// The extern indexes of the strict level: the symbols the module's other target objects define (for the target's
/// side), and those of the module as linked, our objects where they are built (for ours).
#[derive(Clone)]
pub struct Externs {
    pub target: std::sync::Arc<strict::ExternIndex>,
    pub ours: std::sync::Arc<strict::ExternIndex>,
}

/// decomp_common's `instruction_stats` and decomp_experiment's `exact_instructions`.
#[derive(Clone, Copy, Debug, Default, Serialize, PartialEq, Eq)]
pub struct Stats {
    /// The target's instructions, and ours.
    pub original: usize,
    pub emitted: usize,
    /// Rows where either side differs.
    pub differing: usize,
    /// 100%, the same number of instructions and no row differs.
    pub exact_instructions: bool,
}

/// residual-scan's classification of the differing rows.
#[derive(Clone, Copy, Debug, Default, Serialize, PartialEq, Eq)]
pub struct Residual {
    /// Same opcode, operands differing only in registers.
    pub register: u32,
    /// Same opcode, r1-relative offsets differing.
    pub stack: u32,
    /// Opcode, length, branch or relocation differences.
    pub other: u32,
    /// `None` for a function that matches (residual-scan does not list those).
    pub verdict: Option<Verdict>,
}

#[derive(Clone, Copy, Debug, Serialize, PartialEq, Eq)]
#[serde(rename_all = "kebab-case")]
pub enum Verdict {
    RegisterOnly,
    StackOnly,
    #[serde(rename = "register+stack")]
    RegisterStack,
    Structural,
    Missing,
    NoRowDiff,
}

impl Verdict {
    pub fn label(self) -> &'static str {
        match self {
            Self::RegisterOnly => "register-only",
            Self::StackOnly => "stack-only",
            Self::RegisterStack => "register+stack",
            Self::Structural => "structural",
            Self::Missing => "missing",
            Self::NoRowDiff => "no-row-diff",
        }
    }
}

/// One row of a function's diff: the target's side and ours.
#[derive(Clone, Debug, Serialize)]
pub struct Row {
    pub target: Cell,
    pub ours: Cell,
}

#[derive(Clone, Debug, Serialize)]
pub struct Cell {
    pub diff_kind: DiffKind,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub instruction: Option<Instruction>,
}

#[derive(Clone, Copy, Debug, Serialize, PartialEq, Eq)]
#[serde(rename_all = "snake_case")]
pub enum DiffKind {
    None,
    OpMismatch,
    ArgMismatch,
    Replace,
    Delete,
    Insert,
}

impl DiffKind {
    /// objdiff-cli's JSON name of it (absent there for `None`).
    pub fn cli_name(self) -> &'static str {
        match self {
            Self::None => "DIFF_NONE",
            Self::OpMismatch => "DIFF_OP_MISMATCH",
            Self::ArgMismatch => "DIFF_ARG_MISMATCH",
            Self::Replace => "DIFF_REPLACE",
            Self::Delete => "DIFF_DELETE",
            Self::Insert => "DIFF_INSERT",
        }
    }
}

impl From<InstructionDiffKind> for DiffKind {
    fn from(kind: InstructionDiffKind) -> Self {
        match kind {
            InstructionDiffKind::None => Self::None,
            InstructionDiffKind::OpMismatch => Self::OpMismatch,
            InstructionDiffKind::ArgMismatch => Self::ArgMismatch,
            InstructionDiffKind::Replace => Self::Replace,
            InstructionDiffKind::Delete => Self::Delete,
            InstructionDiffKind::Insert => Self::Insert,
        }
    }
}

#[derive(Clone, Debug, Serialize)]
pub struct Instruction {
    pub address: u64,
    pub mnemonic: String,
    pub args: Vec<Arg>,
    /// As objdiff shows it.
    pub formatted: String,
}

#[derive(Clone, Debug, Serialize, PartialEq, Eq)]
#[serde(rename_all = "snake_case")]
pub enum Arg {
    Signed(i64),
    Unsigned(u64),
    Opaque(String),
    /// The instruction's relocation (objdiff-cli gives only that there is one).
    Reloc,
    BranchDest(u64),
}

/// The objdiff configuration of OPTIONS: its options, then the kit's policy.
pub fn diff_config(options: &ScoreOptions) -> Result<DiffObjConfig> {
    let mut config = DiffObjConfig::default();
    let policy = (RELOC_POLICY.0.to_owned(), RELOC_POLICY.1.to_owned());
    for (id, value) in options.config.iter().chain(std::iter::once(&policy)) {
        let property =
            ConfigPropertyId::from_str(id).map_err(|_| anyhow!("unknown objdiff option {id}"))?;
        config
            .set_property_value_str(property, value)
            .map_err(|_| anyhow!("objdiff option {id}: invalid value {value}"))?;
    }
    Ok(config)
}

/// The configuration `objdiff-cli report generate` scores with (before a project's options): relocations compared by
/// nothing, data and text sections combined, no pool relocations computed. [`score_with`] it gives report.json's
/// matches.
pub fn report_config() -> DiffObjConfig {
    DiffObjConfig {
        function_reloc_diffs: objdiff_core::diff::FunctionRelocDiffs::None,
        combine_data_sections: true,
        combine_text_sections: true,
        ppc_calculate_pool_relocations: false,
        ..DiffObjConfig::default()
    }
}

fn is_function(symbol: &obj::Symbol) -> bool {
    symbol.kind == SymbolKind::Function && symbol.size != 0
}

/// OURS scored against TARGET (two objects' bytes).
pub fn score(target: &[u8], ours: &[u8], options: &ScoreOptions) -> Result<UnitScore> {
    Target::new(target, diff_config(options)?)?.score(ours, options)
}

/// [`score`] with objdiff's CONFIG as it is (OPTIONS' `config` is not applied).
pub fn score_with(
    target: &[u8],
    ours: &[u8],
    config: &DiffObjConfig,
    options: &ScoreOptions,
) -> Result<UnitScore> {
    Target::new(target, config.clone())?.score(ours, options)
}

/// A target object, read once to score many candidates against (from any thread: objdiff's object is not `Sync`, so
/// its diffs take turns, while each candidate's own parse does not).
pub struct Target {
    config: DiffObjConfig,
    object: std::sync::Mutex<Object>,
    words: HashMap<String, words::Code>,
    /// The target as the strict level reads it, and its extern indexes.
    strict: strict::elf::Object,
    externs: Option<Externs>,
    /// The scores given, by our object's hash and the options (only those without rows: they are large).
    scores: std::sync::Mutex<HashMap<(Vec<u8>, String), UnitScore>>,
    hits: std::sync::atomic::AtomicUsize,
}

/// The scores a target keeps.
const KEPT_SCORES: usize = 4096;

impl Target {
    /// The target object BYTES, scored with objdiff's CONFIG ([`diff_config`]).
    pub fn new(bytes: &[u8], config: DiffObjConfig) -> Result<Self> {
        Ok(Self {
            object: std::sync::Mutex::new(obj::read::parse(bytes, &config, DiffSide::Target)?),
            words: words::functions(bytes)?,
            strict: strict::elf::load(bytes)?,
            externs: None,
            config,
            scores: Default::default(),
            hits: Default::default(),
        })
    }

    /// With the strict level's extern indexes (without them, a literal another object holds is unresolved).
    pub fn with_externs(mut self, externs: Externs) -> Self {
        self.externs = Some(externs);
        self
    }

    /// The scores answered from those kept (an equal object scored with the same options).
    pub fn score_hits(&self) -> usize {
        self.hits.load(std::sync::atomic::Ordering::Relaxed)
    }

    /// OURS scored against it (OPTIONS' `config` is the target's: it is not applied again). An object scored before
    /// with the same options (and no rows) gives the score kept.
    pub fn score(&self, ours: &[u8], options: &ScoreOptions) -> Result<UnitScore> {
        if options.rows {
            return self.score_uncached(ours, options);
        }
        use sha1::{Digest, Sha1};
        let key = (Sha1::digest(ours).to_vec(), format!("{options:?}"));
        if let Some(score) = self.scores.lock().unwrap().get(&key) {
            self.hits.fetch_add(1, std::sync::atomic::Ordering::Relaxed);
            return Ok(score.clone());
        }
        let score = self.score_uncached(ours, options)?;
        let mut scores = self.scores.lock().unwrap();
        if scores.len() >= KEPT_SCORES {
            scores.clear();
        }
        scores.insert(key, score.clone());
        Ok(score)
    }

    /// objdiff-cli's diff document of OURS against it (`objdiff-cli diff --format json`, left the target): what the
    /// tools that read objdiff's rows (regdiff) take.
    pub fn diff_document(&self, ours: &[u8], options: &ScoreOptions) -> Result<serde_json::Value> {
        let config = &self.config;
        let right = obj::read::parse(ours, config, DiffSide::Base)?;
        let left = self.object.lock().unwrap();
        let mapping = MappingConfig {
            mappings: options.mappings.clone(),
            ..MappingConfig::default()
        };
        let result = diff_objs(Some(&*left), Some(&right), None, config, &mapping)?;
        let (left_diff, right_diff) = (result.left.unwrap(), result.right.unwrap());
        let document = objdiff_core::bindings::diff::DiffResult::new(
            Some((&*left, &left_diff)),
            Some((&right, &right_diff)),
            config,
        )?;
        // (through its text: an f32 is written as objdiff-cli writes it, not widened)
        Ok(serde_json::from_str(&serde_json::to_string(&document)?)?)
    }

    fn score_uncached(&self, ours: &[u8], options: &ScoreOptions) -> Result<UnitScore> {
        let config = &self.config;
        let right = obj::read::parse(ours, config, DiffSide::Base)?;
        let our_words = words::functions(ours)?;
        let left = self.object.lock().unwrap();
        let left = &*left;
        let mapping = MappingConfig {
            mappings: options.mappings.clone(),
            ..MappingConfig::default()
        };
        let result = diff_objs(Some(left), Some(&right), None, config, &mapping)?;
        let (left_diff, right_diff) = (result.left.unwrap(), result.right.unwrap());
        let our_strict = options
            .strict
            .then(|| strict::elf::load(ours))
            .transpose()?;
        let strict_indexes = our_strict.as_ref().map(|o| {
            (
                strict::Index::new(&self.strict)
                    .with_externs(self.externs.as_ref().map(|e| &*e.target)),
                strict::Index::new(o).with_externs(self.externs.as_ref().map(|e| &*e.ours)),
            )
        });
        let wanted = |symbol: &obj::Symbol| {
            options.symbols.as_ref().is_none_or(|names| {
                names
                    .iter()
                    .any(|n| *n == symbol.name || symbol.demangled_name.as_ref() == Some(n))
            })
        };
        let mut functions = vec![];
        let (mut total, mut matched) = (0f64, 0f64);
        let mut paired_ours = vec![false; right.symbols.len()];
        for (index, symbol) in left.symbols.iter().enumerate() {
            if !is_function(symbol) {
                continue;
            }
            let diff = &left_diff.symbols[index];
            let other = diff.target_symbol;
            if let Some(other) = other {
                paired_ours[other] = true;
            }
            if !wanted(symbol) {
                continue;
            }
            let empty = vec![];
            let our_rows = other.map_or(&empty, |o| &right_diff.symbols[o].instruction_rows);
            let present = |obj: &Object, symbol: usize, row: Option<&InstructionDiffRow>| {
                row.and_then(|r| r.ins_ref)
                    .is_some_and(|r| obj.resolve_instruction_ref(symbol, r).is_some())
            };
            let differs = |row: Option<&InstructionDiffRow>| {
                row.is_some_and(|r| r.kind != InstructionDiffKind::None)
            };
            // (the statistics from the rows; the instructions shown only where they are needed)
            let (mut stats, mut residual_rows, mut rows) = (Stats::default(), vec![], vec![]);
            for (a, b) in pad(&diff.instruction_rows, our_rows) {
                let b = b.filter(|_| other.is_some());
                stats.original += usize::from(present(left, index, a));
                stats.emitted += usize::from(other.is_some_and(|o| present(&right, o, b)));
                let differing = differs(a) || differs(b);
                stats.differing += usize::from(differing);
                if differing || options.rows {
                    let cells = (
                        cell(left, index, a, config)?,
                        other.map_or_else(empty_cell, |o| cell(&right, o, b, config))?,
                    );
                    if options.rows {
                        rows.push(Row {
                            target: cells.0.clone(),
                            ours: cells.1.clone(),
                        });
                    }
                    if differing {
                        residual_rows.push(cells);
                    }
                }
            }
            stats.exact_instructions = diff.match_percent == Some(100.0)
                && stats.original > 0
                && stats.original == stats.emitted
                && stats.differing == 0;
            let residual = residual(&residual_rows, other.is_some(), diff.match_percent);
            let strict = other
                .zip(strict_indexes.as_ref())
                .and_then(|(o, (t, ours))| {
                    let tf = t.object.function(&symbol.name)?;
                    let of = ours.object.function(&right.symbols[o].name)?;
                    Some(strict::compare(t, tf, ours, of))
                });
            let profile = other.and_then(|o| {
                let ours = our_words.get(&right.symbols[o].name)?;
                let theirs = self.words.get(&symbol.name)?;
                Some(match &strict {
                    Some(strict) => profile::strict_profile(theirs, ours, strict),
                    None => profile::profile(
                        theirs,
                        ours,
                        diff.match_percent.is_some_and(|p| p >= 100.0),
                    ),
                })
            });
            let percent = diff.match_percent.filter(|_| other.is_some());
            total += symbol.size as f64;
            matched += symbol.size as f64 * f64::from(percent.unwrap_or(0.0)) / 100.0;
            functions.push(FunctionScore {
                name: symbol.name.clone(),
                demangled: symbol.demangled_name.clone(),
                size: symbol.size,
                match_percent: percent,
                paired: other.map(|o| right.symbols[o].name.clone()),
                stats,
                residual,
                profile,
                strict,
                rows,
            });
        }
        let extra = right
            .symbols
            .iter()
            .enumerate()
            .filter(|(i, s)| is_function(s) && !paired_ours[*i])
            .map(|(_, s)| s.name.clone())
            .collect();
        Ok(UnitScore {
            fuzzy: (total > 0.0).then(|| matched / total * 100.0),
            functions,
            extra,
        })
    }
}

/// The rows of both sides, the shorter one padded (as `zip_longest`).
fn pad<'a>(
    left: &'a [InstructionDiffRow],
    right: &'a [InstructionDiffRow],
) -> impl Iterator<
    Item = (
        Option<&'a InstructionDiffRow>,
        Option<&'a InstructionDiffRow>,
    ),
> {
    (0..left.len().max(right.len())).map(move |i| (left.get(i), right.get(i)))
}

fn empty_cell() -> Result<Cell> {
    Ok(Cell {
        diff_kind: DiffKind::None,
        instruction: None,
    })
}

/// A row of OBJ's symbol SYMBOL as objdiff-cli gives it: its kind, and its instruction as `display_instruction`
/// shows it.
fn cell(
    obj: &Object,
    symbol: usize,
    row: Option<&InstructionDiffRow>,
    config: &DiffObjConfig,
) -> Result<Cell> {
    let Some(row) = row else { return empty_cell() };
    let instruction = match row
        .ins_ref
        .and_then(|r| obj.resolve_instruction_ref(symbol, r))
    {
        None => None,
        Some(resolved) => {
            let (mut mnemonic, mut args, mut formatted) = (String::new(), vec![], String::new());
            obj.arch
                .display_instruction(resolved, config, &mut |part| {
                    match &part {
                        InstructionPart::Basic(s) => formatted.push_str(s),
                        InstructionPart::Opcode(s, _) => {
                            mnemonic = s.to_string();
                            formatted.push_str(s);
                            formatted.push(' ');
                        }
                        InstructionPart::Separator => formatted.push_str(config.separator()),
                        InstructionPart::Arg(arg) => {
                            let (value, text) = match arg {
                                InstructionArg::Value(InstructionArgValue::Signed(v)) => {
                                    (Arg::Signed(*v), v.to_string())
                                }
                                InstructionArg::Value(InstructionArgValue::Unsigned(v)) => {
                                    (Arg::Unsigned(*v), v.to_string())
                                }
                                InstructionArg::Value(InstructionArgValue::Opaque(v)) => {
                                    (Arg::Opaque(v.to_string()), v.to_string())
                                }
                                InstructionArg::Reloc => (Arg::Reloc, reloc_text(&resolved)),
                                InstructionArg::BranchDest(d) => {
                                    (Arg::BranchDest(*d), format!("{d:#x}"))
                                }
                            };
                            formatted.push_str(&text);
                            args.push(value);
                        }
                    }
                    Ok(())
                })?;
            Some(Instruction {
                address: resolved.ins_ref.address,
                mnemonic,
                args,
                formatted,
            })
        }
    };
    Ok(Cell {
        diff_kind: row.kind.into(),
        instruction,
    })
}

fn reloc_text(resolved: &obj::ResolvedInstructionRef<'_>) -> String {
    let mut out = String::new();
    if let Some(r) = resolved.relocation {
        out.push_str(&r.symbol.name);
        match r.relocation.addend {
            0 => {}
            a if a < 0 => {
                let _ = write!(out, "-{:#x}", -a);
            }
            a => {
                let _ = write!(out, "+{a:#x}");
            }
        }
    }
    out
}

fn is_register(text: &str) -> bool {
    // (residual-scan's `^[rfc]r?\d+$`)
    let rest = text.strip_prefix(['r', 'f', 'c']).unwrap_or("");
    let rest = rest.strip_prefix('r').unwrap_or(rest);
    text.len() > 1 && !rest.is_empty() && rest.bytes().all(|b| b.is_ascii_digit())
}

/// residual-scan's classification of a function's differing rows (PAIRED: it has a counterpart; PERCENT: its match).
fn residual(rows: &[(Cell, Cell)], paired: bool, percent: Option<f32>) -> Residual {
    let mut out = Residual::default();
    if !paired {
        out.verdict = Some(Verdict::Missing);
        return out;
    }
    for (left, right) in rows {
        if left.diff_kind == DiffKind::None && right.diff_kind == DiffKind::None {
            continue;
        }
        let (Some(a), Some(b)) = (&left.instruction, &right.instruction) else {
            out.other += 1;
            continue;
        };
        if a.mnemonic != b.mnemonic || a.args.len() != b.args.len() {
            out.other += 1;
            continue;
        }
        // (as objdiff-cli's JSON compares them: a relocation is only that there is one)
        let differs: Vec<(&Arg, &Arg)> =
            a.args.iter().zip(&b.args).filter(|(x, y)| x != y).collect();
        let opaque_register = |arg: &Arg| matches!(arg, Arg::Opaque(t) if is_register(t));
        if !differs.is_empty()
            && differs
                .iter()
                .all(|(x, y)| opaque_register(x) && opaque_register(y))
        {
            out.register += 1;
        } else if !differs.is_empty()
            && b.args
                .iter()
                .any(|arg| matches!(arg, Arg::Opaque(t) if t == "r1"))
            && differs
                .iter()
                .all(|(x, y)| matches!((x, y), (Arg::Signed(_), Arg::Signed(_))))
        {
            out.stack += 1;
        } else {
            out.other += 1;
        }
    }
    if percent.is_some_and(|p| p >= 100.0) {
        return out;
    }
    out.verdict = Some(match (out.register > 0, out.stack > 0, out.other > 0) {
        (false, false, false) => Verdict::NoRowDiff,
        (true, false, false) => Verdict::RegisterOnly,
        (false, true, false) => Verdict::StackOnly,
        (true, true, false) => Verdict::RegisterStack,
        _ => Verdict::Structural,
    });
    out
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn registers_are_residual_scans() {
        for r in ["r3", "f31", "cr0", "c1", "r10"] {
            assert!(is_register(r), "{r}");
        }
        for r in ["r", "sp", "3", "r3a", "lr", "ctr", "f"] {
            assert!(!is_register(r), "{r}");
        }
    }

    #[test]
    fn the_policy_is_set_last() {
        let config = diff_config(&ScoreOptions {
            config: vec![("functionRelocDiffs".into(), "none".into())],
            ..ScoreOptions::default()
        })
        .unwrap();
        assert_eq!(
            config.get_property_value(ConfigPropertyId::FunctionRelocDiffs),
            objdiff_core::diff::ConfigPropertyValue::Choice("data_value")
        );
        assert!(
            diff_config(&ScoreOptions {
                config: vec![("noSuchOption".into(), "1".into())],
                ..ScoreOptions::default()
            })
            .is_err()
        );
    }
}
