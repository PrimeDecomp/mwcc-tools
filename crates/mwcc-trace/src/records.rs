//! The records the oracle's hooks write of what the compiler decides, read into mwdec's model of a trace ([`Trace`],
//! as mwdec's debugger tracer gives it too): its inlining decisions, colouring rounds, scheduled blocks, PCode stages,
//! copy propagation and peephole. [`Selection`] says what to read, and the trace options it needs.
//!
//! - colouring: `ra.graph` (the class), `ra.node` (each node before simplification), `ra.temporaries` and `ra.value`
//!   (the class's temporaries; each node's object, argument and rematerialization), `ra.order` (the stack select
//!   takes, with each node's spill cost) and `ra.select` (the colour and flags each took), in the function of the
//!   `ra.before` snapshot they follow;
//! - scheduler: `sc.block`, its instructions (`sc.i`), the DAG (`sc.node`) and each pick (`sc.pick`);
//! - inliner: each check (`inl.can`, `inl.decide`, and `inl.cost` for the size estimate it made) in the function
//!   `inl.expand` began;
//! - PCode: snapshots (`st.*`: `st.codegen`, as the global optimizer begins, `st.presched`, as the first scheduling
//!   pass begins, and `st.precolor.*`, as select colours a class), copy propagation (`cp.use`, each decision on a
//!   copy's use, `cp.remove`, each copy all uses took) and the peephole (`ph.hit`, a pattern that changed an
//!   instruction, `ph.dead`, a dead one removed);
//! - the optimizer's dump, into the file `MWCC_ORACLE_IRO_DUMP` names, after the phases `MWCC_ORACLE_IRO_PHASES`
//!   names;
//! - alias analysis: each table gather_alias_info leaves (`al.begin`, its aliases, `al.alias`, the alias of each
//!   access, `al.access`, and `al.end`) and, if asked, every query (`al.query`);
//! - value numbering: each pass (`vn.begin` ... `vn.end`), what became of each load and operation (`vn.decide`, by
//!   the instructions `vn.i` lists) and what killed the memory's values (`vn.kill`, `vn.killall`);
//! - names: `pc.opcode` (an opcode's name, the first time) and `pc.object` (the object an instruction's operand
//!   refers to, after it).
use crate::{
    options::TraceOptions,
    sched::{SchedBlock, SchedNode, SchedPick, classify_edges},
};
use serde::Serialize;
use std::collections::{BTreeMap, HashMap};

#[derive(Clone, Debug, Serialize)]
pub struct InlineDecision {
    /// function whose body was being expanded
    pub caller: String,
    pub callee: String,
    /// expansion pass (cinline_level) of the caller
    pub pass: i16,
    /// depth budget passed to the size estimate
    pub level: Option<i32>,
    /// estimated cost of the callee's (expanded) body
    pub cost: Option<i32>,
    pub max: i32,
    pub inlined: bool,
    pub reason: String,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq, Serialize)]
pub enum VregKind {
    Param,
    Named,
    FeTemp,
    Temp,
}

#[derive(Clone, Debug, Serialize)]
pub struct IgNode {
    pub vreg: u16,
    pub name: Option<String>,
    pub kind: VregKind,
    /// IG adjacency size (physical + virtual neighbours) before simplification
    pub degree: u16,
    /// final register number (0..31) or None if spilled/unresolved
    pub reg: Option<u8>,
    pub coalesced_into: Option<i16>,
    pub spilled: bool,
    pub neighbours: Vec<i16>,
    /// position in colouring order (0 = coloured first); None for coalesced nodes
    pub order: Option<usize>,
    /// pushed in a later simplify pass (degree >= K), i.e. coloured ahead of its vreg rank
    pub blocked: bool,
    /// spill cost computed before simplify (uses 2w, defs w; see `regalloc::spill_cost`)
    pub spill_cost: i32,
    /// rematerialisable (single location-independent definition: li/lis/addi rX,r1,N)
    pub remat: bool,
    /// current IG degree when simplify started (after coalescing); simplify compares this with K
    pub simplify_degree: u16,
}

#[derive(Clone, Debug, Serialize)]
pub struct ColorRound {
    pub function: String,
    /// "GPR" or "FPR"
    pub class: String,
    pub nreal: u32,
    pub first_fe_temp: u32,
    pub first_temp: u32,
    pub nodes: Vec<IgNode>,
}

/// One copy-propagation decision: may the copy `mr vX, vY` be propagated into this use of vX?
/// A copy disappears (its target becomes the source register) only if every use accepts.
#[derive(Clone, Debug, Serialize)]
pub struct CopyPropDecision {
    pub function: String,
    /// the copy (`mr vX, vY` / `fmr`)
    pub copy: String,
    /// the use of vX
    pub use_text: String,
    pub accepted: bool,
    pub reason: String,
}

/// A copy all of whose uses accepted: removed, or kept by the propagation pass (exception
/// register, or a physical source in the non-aggressive pass).
#[derive(Clone, Debug, Serialize)]
pub struct CopyRemoval {
    pub function: String,
    pub copy: String,
    pub removed: bool,
}

/// A peephole rule that changed an instruction (post-RA, physical registers).
#[derive(Clone, Debug, Serialize)]
pub struct PeepholeHit {
    pub function: String,
    pub rule: String,
    pub before: String,
    /// the instruction after the rule (None: removed)
    pub after: Option<String>,
}

/// What an alias stands for: an object, a part of one (`offset`, `size`), or a set of aliases.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Serialize)]
#[serde(rename_all = "snake_case")]
pub enum AliasKind {
    Object,
    Part,
    Set,
}

/// One alias of the alias analysis's table.
#[derive(Clone, Debug, Serialize)]
pub struct Alias {
    pub index: u32,
    pub kind: AliasKind,
    /// the object's name (None: a set)
    pub object: Option<String>,
    pub offset: i32,
    pub size: i32,
    /// a set's members (the aliases' indices, and their parts')
    pub members: Vec<u32>,
}

/// A load, store or call and its alias.
#[derive(Clone, Debug, Serialize)]
pub struct AliasAccess {
    pub instruction: String,
    pub alias: u32,
}

/// The table gather_alias_info left (the scheduler and the global optimizer's passes each gather one).
#[derive(Clone, Debug, Serialize)]
pub struct AliasTable {
    pub function: String,
    pub link: String,
    /// gathered while the registers are virtual (the accesses' aliases follow the address registers' definitions)
    pub coloring: bool,
    pub aliases: Vec<Alias>,
    pub accesses: Vec<AliasAccess>,
}

/// One query of the alias analysis: may the access `a` touch what `b` does?
#[derive(Clone, Debug, Serialize)]
pub struct AliasQuery {
    pub function: String,
    /// `may_alias`, `may_alias_scheduling` (2.0p2's scheduler) or `may_alias_object`
    pub kind: String,
    pub a: String,
    /// the other access, or the object's name (`may_alias_object`)
    pub b: String,
    pub may: bool,
}

/// What the value numbering did with a load or an operation.
#[derive(Clone, Debug, Serialize)]
pub struct VnDecision {
    /// the block it is in, the first of its extended basic block (the values it can reuse are the extended block's),
    /// and its source line
    pub block: i32,
    pub extended_block: i32,
    pub line: i32,
    pub instruction: String,
    /// its flags' names ([`flag_names`])
    pub flags: Vec<String>,
    /// `skipped` (volatile, or it writes more than one register), `reused` (an available value's: `other`),
    /// `forwarded` (the value the store `other` wrote) or `kept` (a new available value: `reason` says why)
    pub outcome: String,
    pub reason: Option<String>,
    /// the instruction it reused, was forwarded from, or (kept) the nearest available value
    pub other: Option<String>,
    pub other_line: Option<i32>,
    /// kept because the memory's value changed: the instruction that changed it last (a store or a call)
    pub killed_by: Option<String>,
    /// its alias's index in the pass's table (`Trace::alias`)
    pub alias: Option<u32>,
}

/// One pass of the value numbering (removecommonsubexpressions: twice a function, before and after the allocation).
#[derive(Clone, Debug, Serialize)]
pub struct VnPass {
    pub function: String,
    pub link: String,
    /// the more aggressive pass (compares and `li` / `lis` of temporaries are candidates too)
    pub aggressive: bool,
    pub removed: bool,
    pub decisions: Vec<VnDecision>,
}

/// The names of the PCode flags FLAGS has (an instruction's that is not a branch; PCode.h), the unnamed ones in
/// hexadecimal.
pub fn flag_names(flags: u32) -> Vec<String> {
    const NAMES: [(u32, &str); 21] = [
        (0x2, "fIsRead"),
        (0x4, "fIsWrite"),
        (0x8, "fIsCall"),
        (0x10, "fIsMove"),
        (0x20, "fIsPtrOp"),
        (0x40, "fIsConst"),
        (0x80, "fIsVolatile"),
        (0x100, "fSideEffects"),
        (0x2000, "fCommutative"),
        (0x4000, "fIsCSE"),
        (0x8000, "fIsArgInit"),
        (0x20000, "fOpFromMemory"),
        (0x40000, "fOpToMemory"),
        (0x400000, "fHasBaseReg"),
        (0x800000, "fOverflow"),
        (0x2000000, "fUpdatesPtr"),
        (0x4000000, "fCanSetCarry"),
        (0x8000000, "fCanSetRecordBit"),
        (0x10000000, "fSetsCarry"),
        (0x20000000, "fRecordBit"),
        (0x1, "fIsBranch"),
    ];
    let mut names = vec![];
    let mut rest = flags;
    for (bit, name) in NAMES {
        if flags & bit != 0 {
            names.push(name.to_string());
            rest &= !bit;
        }
    }
    match flags & 0xC000_0000 {
        0x4000_0000 => names.push("fOpTypeFPR".into()),
        0x8000_0000 => names.push("fOpTypeGPR".into()),
        0xC000_0000 => names.push("fOpTypeVR".into()),
        _ => {}
    }
    rest &= !0xC000_0000;
    if rest != 0 {
        names.push(format!("{rest:#x}"));
    }
    names
}

#[derive(Clone, Debug, Serialize)]
pub struct PcodeDump {
    pub stage: String,
    pub function: String,
    pub lines: Vec<String>,
}

#[derive(Clone, Debug, Default, Serialize)]
pub struct Trace {
    pub inline: Vec<InlineDecision>,
    /// functions in the order their bodies were expanded (= code generation order)
    pub expand_order: Vec<String>,
    pub rounds: Vec<ColorRound>,
    /// list-scheduler DAGs and picks per basic block (pre-RA and post-RA passes)
    pub sched: Vec<SchedBlock>,
    /// PCode copy propagation: every (copy, use) decision
    pub copyprop: Vec<CopyPropDecision>,
    /// PCode copy propagation: copies whose uses all accepted (removed or kept)
    pub copyprop_removals: Vec<CopyRemoval>,
    /// post-RA peephole rule hits (and dead instructions removed)
    pub peephole: Vec<PeepholeHit>,
    /// the alias analysis's tables, in the order they were gathered
    pub alias: Vec<AliasTable>,
    /// the alias analysis's queries (with `Selection::alias_queries`)
    pub alias_queries: Vec<AliasQuery>,
    /// the value numbering's passes
    pub value_numbering: Vec<VnPass>,
    pub pcode: Vec<PcodeDump>,
    /// the front-end optimizer's own dump (IRO passes, expression trees), when requested
    pub iro: String,
    #[serde(skip)]
    pub object: Vec<u8>,
}

/// One rendered PCode instruction plus its register operands (for dependence classification).
#[derive(Clone, Debug, Default)]
pub struct PInstr {
    pub op: i16,
    pub name: String,
    pub text: String,
    /// (class, reg, read, write)
    pub regs: Vec<(u8, i16, bool, bool)>,
}

/// Render one PCode instruction: opcode `op` named `name`, its flags word (0x14) and its `argc` 12-byte operands
/// `raw`, with `object` naming the objects operands refer to.
pub fn render_pcode(
    op: i16,
    argc: i16,
    flags: u32,
    raw: &[u8],
    name: String,
    mut object: impl FnMut(u32) -> String,
) -> PInstr {
    let mut args = vec![];
    let mut regs = vec![];
    for k in 0..(argc as usize).min(raw.len() / 12) {
        let a = &raw[12 * k..12 * k + 12];
        let (kind, cls) = (a[0], a[1]);
        let eff = u16::from_le_bytes([a[2], a[3]]);
        let reg = i16::from_le_bytes([a[4], a[5]]);
        let imm = i32::from_le_bytes([a[2], a[3], a[4], a[5]]);
        args.push(match kind {
            0 => {
                let pre = match cls {
                    4 => "r",
                    3 => "f",
                    1 => "cr",
                    0 => "spr",
                    _ => "v",
                };
                regs.push((cls, reg, eff & 1 != 0, eff & 2 != 0));
                format!("{pre}{reg}{}", if eff & 3 == 2 { "=" } else { "" })
            }
            2 => format!("#{imm}"),
            3 => {
                let obj = u32::from_le_bytes([a[6], a[7], a[8], a[9]]);
                let n = if obj == 0 {
                    "?".to_string()
                } else {
                    object(obj)
                };
                if imm == 0 { n } else { format!("{n}+{imm}") }
            }
            _ => format!("<{kind}:{imm}>"),
        });
    }
    if matches!(name.as_str(), "bl" | "bctrl" | "blrl") && args.len() > 4 {
        let w = args.iter().filter(|a| a.ends_with('=')).count();
        args.truncate(1);
        args.push(format!("({} operands, {} written regs)", argc, w));
    }
    let mut text = format!("{:<8} {}", name, args.join(", "))
        .trim_end()
        .to_string();
    // `MWDEC_PCODE_FLAGS`: the alias class the scheduler sees on memory operations (word 0x14 of
    // the PCode): `{ptr}` = through a pointer of unknown origin (ordered against every store and
    // the prologue's register saves), `{const}` = marked read-only; neither = a known object (a
    // stack/global object, or the pointee of a pointer-to-const parameter: never ordered against
    // the register saves)
    if std::env::var_os("MWDEC_PCODE_FLAGS").is_some()
        && args.iter().any(|a| a.starts_with('#') || a.contains('+'))
        && (name.starts_with('l') || name.starts_with("st") || name.starts_with("psq"))
        && !matches!(name.as_str(), "li" | "lis")
    {
        if flags & 0x20 != 0 {
            text.push_str(" {ptr}");
        }
        if flags & 0x40 != 0 {
            text.push_str(" {const}");
        }
    }
    PInstr {
        op,
        name,
        text,
        regs,
    }
}

/// What [`parse`] reads of a trace, and so what the compile must trace ([`Selection::trace_options`]).
#[derive(Clone, Debug, Default)]
pub struct Selection {
    pub inline: bool,
    pub coloring: bool,
    /// any of "initial", "presched", "precolor", or a stage the compiler's debug listing names (`AFTER VALUE
    /// NUMBERING 2`, or a part of the name, without regard to case: `value numbering`; `listing`: every one)
    pub pcode: Vec<String>,
    /// the compiler's own IRO dump (`Trace::iro`), into the file `iro_dump`: the final form of every function
    pub iro: bool,
    /// with `iro`: the flowgraph after every IRO pass, not only the final one (large)
    pub iro_all_stages: bool,
    /// the list scheduler (DAG and every pick) per basic block (`Trace::sched`)
    pub sched: bool,
    /// with `sched`: only functions whose name contains this
    pub sched_filter: Option<String>,
    /// PCode copy-propagation decisions (`Trace::copyprop`)
    pub copyprop: bool,
    /// post-RA peephole rule hits (`Trace::peephole`)
    pub peephole: bool,
    /// the alias analysis's tables (`Trace::alias`)
    pub alias: bool,
    /// and its queries (`Trace::alias_queries`)
    pub alias_queries: bool,
    /// the value numbering's decisions (`Trace::value_numbering`)
    pub value_numbering: bool,
}

/// The stages of [`Selection::pcode`] that are not the debug listing's.
const STAGES: [&str; 3] = ["initial", "presched", "precolor"];

/// The phase whose IRO dump is the final form of a function (`MWCC_ORACLE_IRO_PHASES`).
pub const IRO_FINAL_STAGE: &str = "After IRO_Optimizer";

impl Selection {
    /// The trace options a compile needs for this selection; IRO_DUMP: the file the optimizer's dump goes to.
    pub fn trace_options(&self, iro_dump: &std::path::Path) -> TraceOptions {
        TraceOptions {
            names: true,
            schedule: self.sched,
            stages: !self.pcode.is_empty(),
            listing: self.pcode.iter().any(|s| !STAGES.contains(&s.as_str())),
            copy_propagation: self.copyprop,
            peephole: self.peephole,
            alias: self.alias,
            alias_queries: self.alias_queries,
            value_numbering: self.value_numbering,
            iro_dump: self.iro.then(|| iro_dump.to_string_lossy().into_owned()),
            iro_phases: self.iro.then(|| {
                if self.iro_all_stages {
                    "all"
                } else {
                    IRO_FINAL_STAGE
                }
                .to_string()
            }),
            ..TraceOptions::default()
        }
    }
}

/// One record: its kind and the rest of its line.
struct Record<'a> {
    kind: &'a str,
    rest: &'a str,
}

impl<'a> Record<'a> {
    /// The value of the field `key` (up to the next space).
    fn get(&self, key: &str) -> Option<&'a str> {
        self.rest
            .split_whitespace()
            .find_map(|t| t.strip_prefix(key).and_then(|t| t.strip_prefix('=')))
    }

    fn int(&self, key: &str) -> i64 {
        self.get(key).and_then(|v| v.parse().ok()).unwrap_or(0)
    }

    fn hex(&self, key: &str) -> u32 {
        self.get(key)
            .and_then(|v| u32::from_str_radix(v, 16).ok())
            .unwrap_or(0)
    }

    /// The value of the field `key` to the end of the line.
    fn tail(&self, key: &str) -> &'a str {
        let pattern = format!("{key}=");
        let start = if self.rest.starts_with(&pattern) {
            Some(0)
        } else {
            self.rest.find(&format!(" {pattern}")).map(|i| i + 1)
        };
        start.map_or("", |i| self.rest[i + pattern.len()..].trim_end())
    }
}

/// The records of `text`: its lines that are records (the driver writes a line or two of its own there).
fn records(text: &str) -> Vec<Record<'_>> {
    text.lines()
        .filter_map(|line| {
            let (kind, rest) = line.split_once(' ').unwrap_or((line, ""));
            let record = kind.split_once('.').is_some_and(|(a, b)| {
                !a.is_empty()
                    && a.bytes().all(|c| c.is_ascii_lowercase())
                    && b.bytes().all(|c| c.is_ascii_lowercase() || c == b'.')
            });
            record.then_some(Record { kind, rest })
        })
        .collect()
}

/// A PCode instruction record (`*.i`), rendered with the opcode and object names around it.
struct Instruction {
    pointer: u32,
    rendered: PInstr,
}

/// Read the instruction record `records[at]` and the `pc.object` records after it.
fn instruction(records: &[Record], at: usize, opcodes: &HashMap<i16, (String, u8)>) -> Instruction {
    let r = &records[at];
    let op = r.int("op") as i16;
    let argc = r.int("n") as i16;
    let raw: Vec<u8> = r
        .tail("ops")
        .split_whitespace()
        .flat_map(|g| {
            (0..g.len() / 2).map(move |i| u8::from_str_radix(&g[2 * i..2 * i + 2], 16).unwrap_or(0))
        })
        .collect();
    let objects: HashMap<u32, &str> = records[at + 1..]
        .iter()
        .take_while(|o| matches!(o.kind, "pc.object" | "pc.opcode"))
        .filter(|o| o.kind == "pc.object")
        .map(|o| (o.hex("p"), o.get("name").unwrap_or("?")))
        .collect();
    let rendered = if !(0..471).contains(&op) || !(0..200).contains(&argc) {
        PInstr {
            op,
            name: "??".into(),
            text: format!("?? op={op} argc={argc}"),
            regs: vec![],
        }
    } else {
        let name = opcodes
            .get(&op)
            .map_or_else(|| format!("op{op}"), |(n, _)| n.clone());
        render_pcode(op, argc, r.hex("flags"), &raw, name, |p| {
            objects.get(&p).map_or("?".into(), |n| n.to_string())
        })
    };
    Instruction {
        pointer: r.hex("p"),
        rendered,
    }
}

/// A colouring round, from its `ra.graph` to its `ra.select`.
#[derive(Default)]
struct Graph {
    class: u8,
    start: u32,
    first_fe_temp: u32,
    first_temp: u32,
    /// vreg -> (pointer, degree at simplification, flags, colour, neighbours)
    nodes: BTreeMap<u16, (u32, u16, u16, i16, Vec<i16>)>,
    /// vreg -> (object, rematerializable, argument, name)
    values: HashMap<u16, (u32, bool, bool, String)>,
    /// the stack: (pointer, spill cost)
    order: Vec<(u32, i32)>,
}

impl Graph {
    fn round(self, function: &str) -> ColorRound {
        let by_pointer: HashMap<u32, u16> = self.nodes.iter().map(|(&v, n)| (n.0, v)).collect();
        let order: Vec<u16> = self
            .order
            .iter()
            .filter_map(|(p, _)| by_pointer.get(p).copied())
            .collect();
        let costs: HashMap<u16, i32> = self
            .order
            .iter()
            .filter_map(|(p, c)| by_pointer.get(p).map(|&v| (v, *c)))
            .collect();
        let position: HashMap<u16, usize> =
            order.iter().enumerate().map(|(k, &v)| (v, k)).collect();
        // (blocked: in colouring order, runs of descending vregs; all but the last run were pushed in later
        // simplify passes)
        let mut run_of: HashMap<u16, usize> = HashMap::new();
        let mut run = 0usize;
        for (k, &v) in order.iter().enumerate() {
            if k > 0 && v > order[k - 1] {
                run += 1;
            }
            run_of.insert(v, run);
        }
        let nreal = self.start;
        let resolve = |mut colour: i16| -> Option<u8> {
            let mut guard = 0;
            while colour >= nreal as i16 && guard < 64 {
                colour = self.nodes.get(&(colour as u16)).map_or(-1, |n| n.3);
                guard += 1;
            }
            (0..32).contains(&colour).then_some(colour as u8)
        };
        let nodes = self
            .nodes
            .iter()
            .map(|(&v, (_, degree, flags, colour, neighbours))| {
                let (object, remat, argument, name) =
                    self.values
                        .get(&v)
                        .cloned()
                        .unwrap_or((0, false, false, String::new()));
                let kind = if object != 0 && argument {
                    VregKind::Param
                } else if u32::from(v) < self.first_fe_temp {
                    VregKind::Named
                } else if u32::from(v) < self.first_temp {
                    VregKind::FeTemp
                } else {
                    VregKind::Temp
                };
                IgNode {
                    vreg: v,
                    name: (object != 0).then_some(name),
                    kind,
                    degree: neighbours.len() as u16,
                    reg: resolve(*colour),
                    coalesced_into: (flags & 4 != 0).then_some(*colour),
                    spilled: flags & 1 != 0,
                    neighbours: neighbours.clone(),
                    order: position.get(&v).copied(),
                    blocked: run_of.get(&v).is_some_and(|&r| r < run),
                    spill_cost: costs.get(&v).copied().unwrap_or(0),
                    remat,
                    simplify_degree: *degree,
                }
            })
            .collect();
        ColorRound {
            function: function.to_string(),
            class: match self.class {
                3 => "FPR".into(),
                4 => "GPR".into(),
                c => format!("class{c}"),
            },
            nreal,
            first_fe_temp: self.first_fe_temp,
            first_temp: self.first_temp,
            nodes,
        }
    }
}

/// A scheduled block, from its `sc.block` to its `sc.after`.
/// A scheduled instruction's latency, height, deadline, predecessors, and successors with their latencies.
type Node = (u16, u16, u16, u16, Vec<(u32, u16)>);

struct Block {
    pre_ra: bool,
    index: u32,
    instructions: Vec<Instruction>,
    /// instruction -> (latency, height, deadline, predecessors, successors and their latencies)
    nodes: HashMap<u32, Node>,
    picks: Vec<(u16, u32)>,
}

impl Block {
    fn finish(self, function: &str, opcodes: &HashMap<i16, (String, u8)>) -> Option<SchedBlock> {
        // (a block the scheduler made no DAG for, as the debugger tracer has it)
        if self.nodes.is_empty() {
            return None;
        }
        let index: HashMap<u32, usize> = self
            .instructions
            .iter()
            .enumerate()
            .map(|(i, x)| (x.pointer, i))
            .collect();
        let mut regs = vec![];
        let mut nodes = vec![];
        for x in self.instructions {
            let (latency, height, deadline, preds, succs) =
                self.nodes.get(&x.pointer).cloned().unwrap_or_default();
            regs.push(x.rendered.regs.clone());
            nodes.push(SchedNode {
                opcode_rank: opcodes.get(&x.rendered.op).map_or(0, |o| o.1),
                text: x.rendered.text,
                latency,
                height,
                deadline,
                preds,
                succs: vec![],
                raw_succs: succs
                    .iter()
                    .filter_map(|(p, l)| index.get(p).map(|&i| (i, *l)))
                    .collect(),
            });
        }
        Some(SchedBlock {
            function: function.to_string(),
            pre_ra: self.pre_ra,
            block: self.index,
            nodes: classify_edges(nodes, &regs),
            picks: self
                .picks
                .iter()
                .filter_map(|&(cycle, p)| index.get(&p).map(|&node| SchedPick { cycle, node }))
                .collect(),
        })
    }
}

/// The trace of the records `text`, for `opts`.
pub fn parse(text: &str, opts: &Selection) -> Trace {
    let records = records(text);
    let opcodes: HashMap<i16, (String, u8)> = records
        .iter()
        .filter(|r| r.kind == "pc.opcode")
        .map(|r| {
            (
                r.int("op") as i16,
                (
                    r.get("name").unwrap_or("?").to_lowercase(),
                    r.int("rank") as u8,
                ),
            )
        })
        .collect();
    let mut t = Trace::default();
    let mut function = String::new();
    let mut allocating = String::new();
    let mut graph: Option<Graph> = None;
    let mut block: Option<Block> = None;
    let mut can: Option<usize> = None;
    let mut cost: Option<(i32, i32)> = None;
    let mut dump: Option<PcodeDump> = None;
    let mut listed = String::new();
    let mut table: Option<AliasTable> = None;
    // (the function the last table was gathered in, and the instructions it listed, by their pointers)
    let mut alias_function = String::new();
    let mut accessed: HashMap<u32, String> = HashMap::new();
    // (the pass being read: its instructions by their pointers, with their flags, its block, what last killed each
    // alias's value, and the call that last killed every value in the extended block)
    let mut numbering: Option<VnPass> = None;
    let mut numbered: HashMap<u32, (String, u32, i32)> = HashMap::new();
    let mut vn_block = 0;
    let mut vn_extended = 0;
    let mut killed: HashMap<i64, u32> = HashMap::new();
    let mut killed_all: Option<u32> = None;
    let expanding = opts.inline
        || opts.coloring
        || opts.sched
        || opts.copyprop
        || opts.peephole
        || opts.alias
        || opts.alias_queries
        || opts.value_numbering
        || !opts.pcode.is_empty();
    let scheduled = |function: &str| {
        opts.sched_filter
            .as_ref()
            .is_none_or(|f| function.contains(f.as_str()))
    };
    for (at, r) in records.iter().enumerate() {
        match r.kind {
            "inl.expand" => {
                function = r.get("function").unwrap_or("?").to_string();
                if expanding {
                    t.expand_order.push(function.clone());
                }
                cost = None;
            }
            "inl.cost" if r.int("nest") == 0 => {
                cost = Some((r.int("cost") as i32, r.int("depth") as i32))
            }
            "inl.can" => can = Some(at),
            "inl.decide" if opts.inline => {
                let Some(c) = can.take().map(|i| &records[i]) else {
                    continue;
                };
                let candidate = r.int("candidate") != 0;
                let pass = r.int("pass");
                let max = r.int("max") as i32;
                // (the size estimate is the check's own only on the bottom-up path)
                let estimated =
                    candidate && c.int("always") == 0 && c.int("auto") != 0 && c.int("body") != 0;
                let (cost, level) = match cost.take() {
                    Some((c, l)) if estimated => (Some(c), Some(l)),
                    _ => (None, None),
                };
                // (the hook names the check's return that decided: `inl.reason`, after the decision)
                let reason = records
                    .get(at + 1)
                    .filter(|n| n.kind == "inl.reason")
                    .and_then(|n| n.get("reason"))
                    .unwrap_or("unknown");
                t.inline.push(InlineDecision {
                    caller: r.get("caller").unwrap_or("?").to_string(),
                    callee: r.get("callee").unwrap_or("?").to_string(),
                    pass: pass as i16,
                    level,
                    cost,
                    max,
                    inlined: c.int("result") != 0,
                    reason: reason.to_string(),
                });
            }
            "ra.before" => allocating = r.get("function").unwrap_or("?").to_string(),
            "ra.graph" if opts.coloring => {
                graph = Some(Graph {
                    class: r.int("cls") as u8,
                    start: r.int("start") as u32,
                    ..Default::default()
                })
            }
            "ra.node" => {
                if let Some(g) = graph.as_mut() {
                    let neighbours = r
                        .get("n")
                        .unwrap_or("")
                        .split(',')
                        .filter_map(|n| n.parse().ok())
                        .collect();
                    let node = (
                        r.hex("p"),
                        r.int("deg") as u16,
                        r.hex("flags") as u16,
                        r.int("color") as i16,
                        neighbours,
                    );
                    g.nodes.insert(r.int("v") as u16, node);
                }
            }
            "ra.temporaries" => {
                if let Some(g) = graph.as_mut() {
                    g.first_fe_temp = r.int("fe") as u32;
                    g.first_temp = r.int("first") as u32;
                }
            }
            "ra.value" => {
                if let Some(g) = graph.as_mut() {
                    let value = (
                        r.hex("object"),
                        r.int("remat") != 0,
                        r.int("argument") != 0,
                        r.tail("name").to_string(),
                    );
                    g.values.insert(r.int("v") as u16, value);
                }
            }
            "ra.order" => {
                if let Some(g) = graph.as_mut() {
                    g.order = r
                        .get("v")
                        .unwrap_or("")
                        .split(',')
                        .filter_map(|e| e.split_once(':'))
                        .map(|(p, c)| {
                            (
                                u32::from_str_radix(p, 16).unwrap_or(0),
                                c.parse().unwrap_or(0),
                            )
                        })
                        .collect();
                }
            }
            "ra.select" => {
                if let Some(mut g) = graph.take() {
                    let by_pointer: HashMap<u32, u16> =
                        g.nodes.iter().map(|(&v, n)| (n.0, v)).collect();
                    for entry in r
                        .get("v")
                        .unwrap_or("")
                        .split(',')
                        .filter(|e| !e.is_empty())
                    {
                        let mut parts = entry.split(':');
                        let (Some(p), Some(colour), Some(flags)) =
                            (parts.next(), parts.next(), parts.next())
                        else {
                            continue;
                        };
                        let Some(v) = u32::from_str_radix(p, 16)
                            .ok()
                            .and_then(|p| by_pointer.get(&p))
                        else {
                            continue;
                        };
                        if let Some(n) = g.nodes.get_mut(v) {
                            n.3 = colour.parse().unwrap_or(-1);
                            n.2 = u16::from_str_radix(flags, 16).unwrap_or(0);
                        }
                    }
                    t.rounds.push(g.round(&allocating));
                }
            }
            "sc.block" if opts.sched => {
                block = scheduled(&function).then(|| Block {
                    pre_ra: r.int("virtual") != 0,
                    index: r.int("index") as u32,
                    instructions: vec![],
                    nodes: HashMap::new(),
                    picks: vec![],
                })
            }
            "sc.i" => {
                if let Some(b) = block.as_mut() {
                    b.instructions.push(instruction(&records, at, &opcodes));
                }
            }
            "sc.node" => {
                if let Some(b) = block.as_mut() {
                    let succs = r
                        .get("succ")
                        .unwrap_or("")
                        .split(',')
                        .filter_map(|e| e.split_once(':'))
                        .map(|(p, l)| {
                            (
                                u32::from_str_radix(p, 16).unwrap_or(0),
                                l.parse().unwrap_or(0),
                            )
                        })
                        .collect();
                    let node = (
                        r.int("latency") as u16,
                        r.int("height") as u16,
                        r.int("latest") as u16,
                        r.int("preds") as u16,
                        succs,
                    );
                    b.nodes.insert(r.hex("i"), node);
                }
            }
            "sc.pick" => {
                if let Some(b) = block.as_mut() {
                    let pick = r.hex("pick");
                    if pick != 0 {
                        b.picks.push((r.int("cycle") as u16, pick));
                    }
                }
            }
            "sc.after" => {
                if let Some(b) = block.take().and_then(|b| b.finish(&function, &opcodes)) {
                    t.sched.push(b);
                }
            }
            "st.stage" => listed = r.tail("name").to_string(),
            kind if kind.starts_with("st.") && !matches!(kind, "st.block" | "st.i" | "st.end") => {
                let stage = match kind {
                    "st.codegen" => "initial",
                    "st.presched" => "presched",
                    "st.precolor.gpr" => "precolor-gpr",
                    "st.precolor.fpr" => "precolor-fpr",
                    "st.listing" => listed.as_str(),
                    _ => "",
                };
                let wanted = if kind == "st.listing" {
                    let name = stage.to_lowercase();
                    opts.pcode.iter().any(|s| {
                        !STAGES.contains(&s.as_str())
                            && (s == "listing" || name.contains(&s.to_lowercase()))
                    })
                } else {
                    opts.pcode
                        .iter()
                        .any(|s| s == stage || s == "precolor" && stage.starts_with("precolor-"))
                };
                // (the debugger names the function select colours by its object, the others by the expansion)
                let name = if kind.starts_with("st.precolor") {
                    r.get("function").unwrap_or("?")
                } else {
                    &function
                };
                dump = wanted.then(|| PcodeDump {
                    stage: stage.into(),
                    function: name.to_string(),
                    lines: vec![],
                });
            }
            "st.block" => {
                if let Some(d) = dump.as_mut() {
                    d.lines
                        .push(format!("B{}: weight {}", r.int("index"), r.int("weight")));
                }
            }
            "st.i" => {
                if let Some(d) = dump.as_mut() {
                    d.lines.push(format!(
                        "   {}",
                        instruction(&records, at, &opcodes).rendered.text
                    ));
                }
            }
            "st.end" => t.pcode.extend(dump.take()),
            "cp.use" if opts.copyprop => {
                let copy = after(&records, at, "cp.copy");
                let into = copy.and_then(|c| after(&records, c, "cp.into"));
                let reason = usize::try_from(r.int("reason"))
                    .ok()
                    .and_then(|i| COPY_REASONS.get(i));
                if let (Some(copy), Some(into), Some(reason)) = (copy, into, reason) {
                    t.copyprop.push(CopyPropDecision {
                        function: function.clone(),
                        copy: instruction(&records, copy, &opcodes).rendered.text,
                        use_text: instruction(&records, into, &opcodes).rendered.text,
                        accepted: r.int("accepted") != 0,
                        reason: reason.to_string(),
                    });
                }
            }
            "cp.remove" if opts.copyprop => {
                if let Some(copy) = after(&records, at, "cp.copy") {
                    t.copyprop_removals.push(CopyRemoval {
                        function: function.clone(),
                        copy: instruction(&records, copy, &opcodes).rendered.text,
                        removed: r.int("removed") != 0,
                    });
                }
            }
            "al.begin" if opts.alias || opts.alias_queries => {
                alias_function = r.get("function").unwrap_or("?").to_string();
                accessed.clear();
                table = Some(AliasTable {
                    function: alias_function.clone(),
                    link: r.get("link").unwrap_or("?").to_string(),
                    coloring: r.int("coloring") != 0,
                    aliases: vec![],
                    accesses: vec![],
                });
            }
            "al.alias" => {
                if let Some(table) = table.as_mut() {
                    let kind = match r.int("type") {
                        0 => AliasKind::Object,
                        1 => AliasKind::Part,
                        _ => AliasKind::Set,
                    };
                    table.aliases.push(Alias {
                        index: r.int("index") as u32,
                        kind,
                        object: (r.hex("object") != 0).then(|| r.tail("name").to_string()),
                        offset: r.int("offset") as i32,
                        size: r.int("size") as i32,
                        members: r
                            .get("members")
                            .unwrap_or("")
                            .split(',')
                            .filter_map(|m| m.parse().ok())
                            .collect(),
                    });
                }
            }
            "al.access" => {
                if let (Some(table), Some(i)) = (table.as_mut(), after(&records, at, "al.i")) {
                    let text = instruction(&records, i, &opcodes).rendered.text;
                    accessed.insert(records[i].hex("p"), text.clone());
                    table.accesses.push(AliasAccess {
                        instruction: text,
                        alias: r.int("alias") as u32,
                    });
                }
            }
            "al.end" => {
                if let Some(table) = table.take()
                    && opts.alias
                {
                    t.alias.push(table);
                }
            }
            "al.query" if opts.alias_queries => {
                let access = |p: u32| {
                    accessed
                        .get(&p)
                        .cloned()
                        .unwrap_or_else(|| format!("{p:08x}"))
                };
                let kind = match r.int("kind") {
                    0 => "may_alias",
                    1 => "may_alias_scheduling",
                    _ => "may_alias_object",
                };
                t.alias_queries.push(AliasQuery {
                    function: alias_function.clone(),
                    kind: kind.into(),
                    a: access(r.hex("a")),
                    b: if r.get("object").is_some() {
                        r.tail("name").to_string()
                    } else {
                        access(r.hex("b"))
                    },
                    may: r.int("result") != 0,
                });
            }
            "vn.begin" if opts.value_numbering => {
                numbered.clear();
                killed.clear();
                killed_all = None;
                numbering = Some(VnPass {
                    function: r.get("function").unwrap_or("?").to_string(),
                    link: r.get("link").unwrap_or("?").to_string(),
                    aggressive: r.int("aggressive") != 0,
                    removed: false,
                    decisions: vec![],
                });
            }
            "vn.ebb" => {
                killed_all = None;
                vn_block = r.int("block") as i32;
                vn_extended = vn_block;
            }
            "vn.block" => vn_block = r.int("block") as i32,
            "vn.i" if numbering.is_some() => {
                let text = instruction(&records, at, &opcodes).rendered.text;
                numbered.insert(r.hex("p"), (text, r.hex("flags"), r.int("line") as i32));
            }
            "vn.kill" => {
                killed.insert(r.int("alias"), r.hex("p"));
            }
            "vn.killall" => killed_all = Some(r.hex("p")),
            "vn.decide" => {
                if let Some(pass) = numbering.as_mut() {
                    let text = |p: u32| {
                        numbered
                            .get(&p)
                            .map_or_else(|| format!("{p:08x}"), |(t, _, _)| t.clone())
                    };
                    let (instruction, flags, line) = numbered
                        .get(&r.hex("p"))
                        .cloned()
                        .unwrap_or_else(|| (format!("{:08x}", r.hex("p")), 0, 0));
                    let other = r.hex("other");
                    let alias = r.int("alias");
                    let reason = r.int("reason");
                    let (outcome, why, killed_by) = match r.int("outcome") {
                        0 => ("skipped", None, None),
                        1 => ("reused", None, None),
                        2 => (
                            "forwarded",
                            Some(format!("the stored value, reused as kind {reason}")),
                            None,
                        ),
                        _ => {
                            let other_flags = numbered.get(&other).map_or(flags, |o| o.1);
                            let why = match reason {
                                0 => "no value of its opcode is available".to_string(),
                                1 => "the available values of its opcode have other operands".to_string(),
                                2 => "an available value has its operands, but the memory's value changed since"
                                    .to_string(),
                                3 => {
                                    let mine = flag_names(flags);
                                    let theirs = flag_names(other_flags);
                                    let only = |a: &[String], b: &[String]| {
                                        a.iter().filter(|f| !b.contains(f)).cloned().collect::<Vec<_>>()
                                    };
                                    format!(
                                        "an available value has its operands, but other flags: it has {:?}, the value {:?}",
                                        only(&mine, &theirs),
                                        only(&theirs, &mine)
                                    )
                                }
                                _ => "an available value has its operands, but its registers hold other values now"
                                    .to_string(),
                            };
                            let killed_by = match reason {
                                2 => killed.get(&alias).map(|&p| text(p)),
                                0 => killed_all.map(text),
                                _ => None,
                            };
                            ("kept", Some(why), killed_by)
                        }
                    };
                    pass.decisions.push(VnDecision {
                        block: vn_block,
                        extended_block: vn_extended,
                        line,
                        instruction,
                        flags: flag_names(flags),
                        outcome: outcome.into(),
                        reason: why,
                        other: (other != 0).then(|| text(other)),
                        other_line: numbered.get(&other).map(|o| o.2),
                        killed_by,
                        alias: u32::try_from(alias).ok(),
                    });
                }
            }
            "vn.end" => {
                if let Some(mut pass) = numbering.take() {
                    pass.removed = r.int("removed") != 0;
                    t.value_numbering.push(pass);
                }
            }
            "ph.hit" | "ph.dead" if opts.peephole => {
                let Some(before) = after(&records, at, "ph.before") else {
                    continue;
                };
                let rule = match r.kind {
                    "ph.dead" => "dead_instruction".to_string(),
                    _ => {
                        let pattern = r.get("rule").unwrap_or("?");
                        PATTERNS
                            .iter()
                            .find(|p| p.0 == pattern)
                            .map_or_else(|| pattern.to_string(), |p| p.1.to_string())
                    }
                };
                t.peephole.push(PeepholeHit {
                    function: function.clone(),
                    rule,
                    before: instruction(&records, before, &opcodes).rendered.text,
                    after: after(&records, before, "ph.after")
                        .map(|a| instruction(&records, a, &opcodes).rendered.text),
                });
            }
            _ => {}
        }
    }
    t
}

/// The record of kind `kind` that follows `records[at]`, past the names (`pc.*`) after it.
fn after(records: &[Record], at: usize, kind: &str) -> Option<usize> {
    let next = (at + 1..records.len()).find(|&i| !records[i].kind.starts_with("pc."))?;
    (records[next].kind == kind).then_some(next)
}

/// Why a copy does or does not propagate into a use (`cp.use reason=`), as the debugger tracer says it.
const COPY_REASONS: [&str; 6] = [
    "use is a move (copies never propagate into moves)",
    "use reads and writes the copied register",
    "source redefined between copy and use",
    "copy does not reach the use on every path",
    "source redefined in the use's block before the use",
    "ok",
];

/// The peephole's patterns (gc2's names, `ph.hit rule=`) and the names the debugger tracer gives them
/// (mwdec's debugger tracer's `PEEPHOLE_RULES`). (gc2's sources have them in the executable's order, reversed.)
const PATTERNS: &[(&str, &str)] = &[
    ("RLWINM_RLWIMI_STH", "fold_rlwimi_rlwinm_to_sthbrx"),
    ("RLWINM_RLWIMI_STW", "fold_rlwimi_store_to_stwbrx"),
    ("SRW_SUBFIC_RLW_OR", "fold_to_rlwnm"),
    ("MR_ADDI", "rewrite_as_addi"),
    ("SRAWI_SRAWI", "combine_srawi"),
    ("ADDI_ADDI", "combine_addi"),
    ("MULLI_MULLI", "combine_mulli"),
    ("RLWINM_RLWINM", "fold_rlwinm_or_mr_reaching_def"),
    ("L_S", "eliminate_redundant_store"),
    ("L_FMR", "rewrite_reaching_def_reg"),
    ("L_MR", "retarget_reaching_def_register"),
    ("VSPLTIS_VMR", "merge_reaching_def_instruction"),
    ("LI_MR", "replace_with_reaching_li"),
    ("NOT_AND", "fold_not_into_andc"),
    ("LBZ_EXTSB_CMPI_BC", "remove_redundant_extsb"),
    ("RLWINM_CMPLI_BC", "bypass_cmpli_zero"),
    ("LI_CMP_BC", "fold_constant_compare_branch"),
    ("L_S_ADDI", "eliminate_matching_addi"),
    ("ADDI_LU_SU", "fold_reaching_addi"),
    ("ADDI_L_S", "fold_addi_or_mr_reaching_def"),
    ("LI_L_S", "fold_li_operand"),
    ("LHA_EXTSH", "fold_reaching_lha_or_extsb"),
    ("LHZ_RLWINM", "fold_lhz_lhzx_mask"),
    ("LBZ_RLWINM", "fold_lbz_lbzx_mask"),
    ("EXTSH_RLWINM", "bypass_extsh_for_rotated_mask"),
    ("EXTSB_RLWINM", "bypass_extsb_for_low_byte_rotated_mask"),
    ("MR_CMPI", "make_record_form"),
    ("VMR_VMRP", "peephole_rule_506ef0"),
    ("VMR_VMR", "unlink_vmr_matching_source"),
    ("FMR_FMR", "unlink_unmatched_fmr"),
    ("MR_MR", "unlink_mr_with_matching_def"),
    ("VMR_Vx_Vx", "unlink_same_reg_move"),
    ("FMR_Fx_Fx", "unlink_same_reg_instruction"),
    ("MR_Rx_Rx", "unlink_equal_reg_instruction"),
];
