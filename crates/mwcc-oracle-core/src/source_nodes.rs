//! Guarded, immutable observations of selected frontend fields and actual calls.
//! Allocation identity is separate from snapshot identity and mutable contents.
//! Display models never issue receipts: preparation replays the authoritative
//! trace, its independent source counters and the same trace's arena history.
//! `complete` means that the declared tag-specific fields were observed; it
//! never means that all 30 node bytes were initialized or observed. In
//! particular, Object nodes expose their pointer but no tagged byte offset,
//! and slot nodes expose an index without assigning it an array-element role.
use crate::{
    evidence::{Evaluation, EvaluationState, EvidenceGap},
    source_arenas::{self, HistoricalReference, Query},
    source_lifetimes::PoolDomain,
    trace::{self, Record},
};
use anyhow::{Result, anyhow, ensure};
use serde::{Deserialize, Serialize};
use std::{collections::BTreeMap, sync::Arc};

pub const SCOPE: &str = "guarded_source_nodes_v1";
const MAX_EVENTS: usize = 2_000_000;
const MAX_FRAMES: usize = 64;
const MAX_TREE_RECORDS: usize = 2049;
fn known() -> Evaluation {
    Evaluation {
        state: EvaluationState::Satisfied,
        reasons: vec![],
    }
}
fn gap(detail: impl Into<String>) -> Evaluation {
    Evaluation::indeterminate(EvidenceGap::SourceNodeCoverage, detail)
}
pub fn is_record(kind: &str) -> bool {
    kind.starts_with("pv.source.")
}

#[derive(Clone, Debug, Serialize, Deserialize)]
pub struct Node {
    pub snapshot: u32,
    pub frame: u32,
    pub stage: u8,
    pub node: u32,
    pub parent: u32,
    pub edge: u8,
    pub tag_known: bool,
    pub tag: u8,
    pub shape: u8,
    pub flags: u16,
    pub r#type: u32,
    pub data: [u32; 4],
    /// Completeness of the selected field policy, not whole-payload contents.
    pub complete: bool,
    pub reason: String,
    pub trace_position: usize,
    pub children: BTreeMap<u8, u32>,
}
#[derive(Clone, Debug, Serialize, Deserialize)]
pub struct Owner {
    pub owner: u32,
    pub parent_owner: u32,
    pub function: u32,
    pub begin_position: usize,
    pub end_position: Option<usize>,
}
#[derive(Clone, Debug, Serialize, Deserialize)]
pub struct Invocation {
    pub invocation: u32,
    pub parent: u32,
    pub owner: u32,
    pub input: u32,
    pub output: Option<u32>,
    pub mode_scope: Option<u32>,
    pub mode_word: Option<u32>,
    pub caller: Option<u32>,
    pub eval_mode: u8,
    pub alloc_state: u8,
    pub begin_position: usize,
    pub end_position: Option<usize>,
    pub end_eval_mode: Option<u8>,
    pub end_alloc_state: Option<u8>,
    pub input_snapshot: Option<u32>,
    pub after_snapshot: Option<u32>,
    pub output_snapshot: Option<u32>,
}
#[derive(Clone, Debug, Serialize, Deserialize)]
pub struct Emission {
    pub emission: u32,
    pub owner: u32,
    pub function: u32,
    pub current_function: u32,
    pub statement: u32,
    pub root: u32,
    pub site: u32,
    pub snapshot: Option<u32>,
    pub begin_position: usize,
    pub end_position: Option<usize>,
}
#[derive(Clone, Debug, Default, PartialEq, Eq, Serialize, Deserialize)]
pub struct Totals {
    pub checkpoints: u32,
    pub owners: u32,
    pub owner_ends: u32,
    pub modes: u32,
    pub mode_ends: u32,
    pub clones: u32,
    pub clone_ends: u32,
    pub emissions: u32,
    pub emission_ends: u32,
    pub snapshots: u32,
    pub gaps: u32,
}
#[derive(Clone, Debug, Serialize, Deserialize)]
pub struct Checkpoint {
    pub trace_position: usize,
    pub totals: Totals,
    pub depths: [usize; 4],
    pub unsupported: bool,
    pub observations: Evaluation,
    pub arena_checkpoint: Option<usize>,
}
#[derive(Clone, Debug, Serialize, Deserialize)]
pub struct CoverageInterval {
    pub begin_position: usize,
    pub end_position: usize,
    pub checkpoint_position: usize,
}
#[derive(Clone, Debug, Serialize, Deserialize)]
pub struct History {
    pub format_version: u32,
    pub version: Option<u8>,
    pub scope: Option<String>,
    pub nodes: Vec<Node>,
    pub owners: Vec<Owner>,
    pub modes: Vec<Invocation>,
    pub clones: Vec<Invocation>,
    pub emissions: Vec<Emission>,
    pub checkpoints: Vec<Checkpoint>,
    pub coverage_intervals: Vec<CoverageInterval>,
    pub observations: Evaluation,
    pub records: Vec<Record>,
    /// The authoritative complete trace; absent streams retain no extra copy.
    pub trace: String,
}
impl Default for History {
    fn default() -> Self {
        Self {
            format_version: 1,
            version: None,
            scope: None,
            nodes: vec![],
            owners: vec![],
            modes: vec![],
            clones: vec![],
            emissions: vec![],
            checkpoints: vec![],
            coverage_intervals: vec![],
            observations: gap("no guarded source-node stream"),
            records: vec![],
            trace: String::new(),
        }
    }
}
#[derive(Clone, Debug, Serialize)]
pub struct Answer<T> {
    pub value: Option<T>,
    pub observations: Evaluation,
}
impl<T> Answer<T> {
    fn unknown(detail: impl Into<String>) -> Self {
        Self {
            value: None,
            observations: gap(detail),
        }
    }
    fn known(value: T) -> Self {
        Self {
            value: Some(value),
            observations: known(),
        }
    }
}

fn hex(r: &Record, key: &str) -> Result<u32> {
    Ok(u32::from_str_radix(r.get(key)?, 16)?)
}
fn bit(r: &Record, key: &str) -> Result<bool> {
    let value: u8 = r.number(key)?;
    ensure!(value <= 1, "invalid {key} bit");
    Ok(value == 1)
}
fn fields(r: &Record) -> Result<()> {
    let decoded = trace::records(&r.raw);
    ensure!(
        decoded.len() == 1 && decoded[0].kind == r.kind && decoded[0].fields == r.fields,
        "source raw/display record disagreement"
    );
    let mut keys = std::collections::BTreeSet::new();
    for token in r.raw.split_whitespace().skip(1) {
        let (key, value) = token
            .split_once('=')
            .ok_or_else(|| anyhow!("invalid source field"))?;
        ensure!(
            !value.is_empty() && keys.insert(key),
            "duplicate/empty source field"
        );
    }
    let expected: &[&str] = match r.kind.as_str() {
        "pv.source.begin" => &["scope", "version"],
        "pv.source.owner.begin" | "pv.source.owner.end" => {
            &["owner", "parent_owner", "function", "depth"]
        }
        "pv.source.mode.begin" => &[
            "invocation",
            "parent_mode",
            "owner",
            "input",
            "mode_word",
            "eval_mode",
            "alloc_state",
            "depth",
        ],
        "pv.source.mode.end" => &[
            "invocation",
            "parent_mode",
            "owner",
            "input",
            "eval_mode",
            "alloc_state",
            "depth",
            "output",
            "input_snapshot",
            "after_snapshot",
            "output_snapshot",
        ],
        "pv.source.clone.begin" => &[
            "invocation",
            "parent_clone",
            "owner",
            "mode_scope",
            "input",
            "caller",
            "eval_mode",
            "alloc_state",
            "depth",
        ],
        "pv.source.clone.end" => &[
            "invocation",
            "parent_clone",
            "owner",
            "mode_scope",
            "input",
            "eval_mode",
            "alloc_state",
            "depth",
            "output",
            "input_snapshot",
            "after_snapshot",
            "output_snapshot",
        ],
        "pv.source.emission.begin" => &[
            "emission",
            "owner",
            "function",
            "current_function",
            "statement",
            "root",
            "site",
            "depth",
        ],
        "pv.source.emission.end" => &[
            "emission",
            "owner",
            "function",
            "current_function",
            "statement",
            "root",
            "site",
            "depth",
            "snapshot",
        ],
        "pv.source.node" => &[
            "snapshot",
            "frame",
            "stage",
            "node",
            "parent",
            "edge",
            "tag_known",
            "tag",
            "shape",
            "flags",
            "type",
            "data0",
            "data1",
            "data2",
            "data3",
            "complete",
            "reason",
        ],
        "pv.source.gap" => &["reason", "owner", "frame", "stage", "snapshot", "site"],
        "pv.source.checkpoint" => &[
            "checkpoints",
            "owners",
            "owner_ends",
            "modes",
            "mode_ends",
            "clones",
            "clone_ends",
            "emissions",
            "emission_ends",
            "snapshots",
            "gaps",
            "owner_depth",
            "mode_depth",
            "clone_depth",
            "emission_depth",
            "unsupported",
        ],
        _ => return Err(anyhow!("unknown source record {}", r.kind)),
    };
    ensure!(
        r.fields.len() == expected.len() + 3
            && r.fields
                .keys()
                .all(|k| matches!(k.as_str(), "schema" | "stream" | "event")
                    || expected.contains(&k.as_str())),
        "unknown/missing source field"
    );
    ensure!(
        r.number::<u8>("schema")? == 1 && r.number::<u8>("stream")? == 1,
        "unsupported source schema/stream"
    );
    Ok(())
}
fn slots(shape: u8) -> usize {
    match shape {
        1 => 1,
        2 => 2,
        3 => 3,
        7 => 4,
        _ => 0,
    }
}
fn node(r: &Record) -> Result<Node> {
    let node = Node {
        snapshot: r.number("snapshot")?,
        frame: r.number("frame")?,
        stage: r.number("stage")?,
        node: hex(r, "node")?,
        parent: r.number("parent")?,
        edge: r.number("edge")?,
        tag_known: bit(r, "tag_known")?,
        tag: r.number("tag")?,
        shape: r.number("shape")?,
        flags: u16::try_from(hex(r, "flags")?)?,
        r#type: hex(r, "type")?,
        data: [
            hex(r, "data0")?,
            hex(r, "data1")?,
            hex(r, "data2")?,
            hex(r, "data3")?,
        ],
        complete: bit(r, "complete")?,
        reason: r.get("reason")?.into(),
        trace_position: r.line,
        children: BTreeMap::new(),
    };
    ensure!(node.node != 0, "null node observation");
    let shape = match node.tag {
        0..=8 | 48 => 1,
        9..=47 => 2,
        53 => 3,
        56 => 4,
        50 => 5,
        61 | 62 => 6,
        67 => 7,
        _ => 0,
    };
    if node.complete {
        ensure!(
            node.tag_known && shape != 0 && node.shape == shape && node.reason == "ok",
            "unsupported complete node shape"
        );
        let used = match node.shape {
            1 | 4 | 6 => 1,
            2 | 5 => 2,
            3 => 3,
            7 => 4,
            _ => 0,
        };
        ensure!(
            node.data[used..].iter().all(|v| *v == 0),
            "nonzero unused node fields"
        );
    } else {
        ensure!(
            node.shape == 0 && node.flags == 0 && node.r#type == 0 && node.data == [0; 4],
            "opaque node exposed unobserved payload"
        );
        ensure!(
            if node.reason == "unsupported" {
                node.tag_known && shape == 0
            } else {
                matches!(
                    node.reason.as_str(),
                    "depth" | "cycle" | "capacity" | "pointer"
                ) && !node.tag_known
                    && node.tag == 0
            },
            "invalid opaque node reason/tag"
        );
    }
    Ok(node)
}

struct Walk {
    frame: u32,
    stage: u8,
    root: u32,
    ancestors: Vec<u32>,
    nodes: Vec<u32>,
}
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
enum Frame {
    Owner(u32),
    Mode(u32),
    Clone(u32),
    Emission(u32),
}
struct Validator {
    history: History,
    event: u32,
    totals: Totals,
    owner_stack: Vec<u32>,
    mode_stack: Vec<u32>,
    clone_stack: Vec<u32>,
    emission_stack: Vec<u32>,
    frames: Vec<Frame>,
    roots: BTreeMap<(u8, u32), u32>,
    walk: Option<Walk>,
    first_position: usize,
    first_gap: Option<usize>,
    last_position: usize,
    last_kind: String,
    next_arena: BTreeMap<usize, usize>,
}
impl Validator {
    fn new() -> Self {
        Self {
            history: History::default(),
            event: 0,
            totals: Totals::default(),
            owner_stack: vec![],
            mode_stack: vec![],
            clone_stack: vec![],
            emission_stack: vec![],
            frames: vec![],
            roots: BTreeMap::new(),
            walk: None,
            first_position: 0,
            first_gap: None,
            last_position: 0,
            last_kind: String::new(),
            next_arena: BTreeMap::new(),
        }
    }
    fn owner(&self) -> u32 {
        self.owner_stack.last().copied().unwrap_or(0)
    }
    fn depths(&self) -> [usize; 4] {
        [
            self.owner_stack.len(),
            self.mode_stack.len(),
            self.clone_stack.len(),
            self.emission_stack.len(),
        ]
    }
    fn retail_boundary(&self) -> Result<()> {
        let (frame, first, input) = match self.frames.last() {
            Some(Frame::Mode(id)) => (*id, 5, self.history.modes[(*id - 1) as usize].input),
            Some(Frame::Clone(id)) => (*id, 2, self.history.clones[(*id - 1) as usize].input),
            Some(Frame::Emission(_)) => {
                return Err(anyhow!("nested retail call during emission observation"));
            }
            _ => return Ok(()),
        };
        ensure!(
            (input == 0 || self.roots.contains_key(&(first, frame)))
                && !self.roots.contains_key(&(first + 1, frame))
                && !self.roots.contains_key(&(first + 2, frame)),
            "nested retail call outside the invocation input/after boundary"
        );
        Ok(())
    }
    fn end_frame(&mut self, frame: Frame) -> Result<()> {
        ensure!(
            self.frames.last() == Some(&frame),
            "crossed source frame lifetimes"
        );
        self.frames.pop();
        Ok(())
    }
    fn finish_walk(&mut self) -> Result<()> {
        if let Some(walk) = self.walk.take() {
            for id in &walk.nodes {
                let n = &self.history.nodes[(*id - 1) as usize];
                for edge in 1..=slots(n.shape) {
                    ensure!(
                        (n.data[edge - 1] != 0) == n.children.contains_key(&(edge as u8)),
                        "missing/extra observed child"
                    );
                }
            }
            ensure!(
                self.roots
                    .insert((walk.stage, walk.frame), walk.root)
                    .is_none(),
                "duplicate stage tree"
            );
        }
        Ok(())
    }
    fn root(&self, stage: u8, frame: u32, pointer: u32, id: u32) -> Result<()> {
        ensure!(
            if pointer == 0 {
                id == 0 && !self.roots.contains_key(&(stage, frame))
            } else {
                self.roots.get(&(stage, frame)) == Some(&id)
                    && self.history.nodes[(id - 1) as usize].node == pointer
            },
            "stage root reference mismatch"
        );
        Ok(())
    }
    fn accept(&mut self, r: &Record) -> Result<()> {
        fields(r)?;
        ensure!(r.line > self.last_position, "source positions reordered");
        ensure!(
            r.number::<u32>("event")?
                == self
                    .event
                    .checked_add(1)
                    .ok_or_else(|| anyhow!("source event overflow"))?,
            "source events missing/reordered"
        );
        ensure!(
            self.history.version.is_some() || r.kind == "pv.source.begin",
            "source record before header"
        );
        if r.kind != "pv.source.node" {
            self.finish_walk()?;
        }
        if matches!(
            r.kind.as_str(),
            "pv.source.owner.begin"
                | "pv.source.mode.begin"
                | "pv.source.clone.begin"
                | "pv.source.emission.begin"
        ) {
            self.retail_boundary()?;
        }
        match r.kind.as_str() {
            "pv.source.begin" => {
                ensure!(
                    self.event == 0 && r.get("scope")? == SCOPE,
                    "duplicate/unsupported source begin"
                );
                let version = r.number("version")?;
                ensure!(
                    (1..=3).contains(&version),
                    "unsupported source compiler version"
                );
                self.history.version = Some(version);
                self.history.scope = Some(SCOPE.into());
                self.first_position = r.line;
            }
            "pv.source.owner.begin" => {
                let id: u32 = r.number("owner")?;
                ensure!(
                    id == self.totals.owners + 1
                        && r.number::<u32>("parent_owner")? == self.owner()
                        && r.number::<usize>("depth")? == self.owner_stack.len() + 1
                        && self.owner_stack.len() < MAX_FRAMES,
                    "owner begin mismatch"
                );
                self.history.owners.push(Owner {
                    owner: id,
                    parent_owner: self.owner(),
                    function: hex(r, "function")?,
                    begin_position: r.line,
                    end_position: None,
                });
                self.owner_stack.push(id);
                self.frames.push(Frame::Owner(id));
                self.totals.owners += 1;
            }
            "pv.source.owner.end" => {
                let id: u32 = r.number("owner")?;
                self.end_frame(Frame::Owner(id))?;
                ensure!(
                    self.owner_stack.last() == Some(&id),
                    "owner end without begin"
                );
                self.owner_stack.pop();
                let owner = &mut self.history.owners[(id - 1) as usize];
                ensure!(
                    r.number::<u32>("parent_owner")? == owner.parent_owner
                        && hex(r, "function")? == owner.function
                        && r.number::<usize>("depth")? == self.owner_stack.len(),
                    "owner end mismatch"
                );
                owner.end_position = Some(r.line);
                self.totals.owner_ends += 1;
            }
            "pv.source.mode.begin" | "pv.source.clone.begin" => self.begin_invocation(r)?,
            "pv.source.mode.end" | "pv.source.clone.end" => self.end_invocation(r)?,
            "pv.source.emission.begin" => {
                let id: u32 = r.number("emission")?;
                ensure!(
                    id == self.totals.emissions + 1
                        && r.number::<u32>("owner")? == self.owner()
                        && r.number::<usize>("depth")? == self.emission_stack.len() + 1
                        && self.emission_stack.len() < MAX_FRAMES,
                    "emission begin mismatch"
                );
                let site = hex(r, "site")?;
                ensure!(
                    crate::source_node_probe::valid_site(self.history.version.unwrap(), site),
                    "unsupported emission site"
                );
                self.history.emissions.push(Emission {
                    emission: id,
                    owner: self.owner(),
                    function: hex(r, "function")?,
                    current_function: hex(r, "current_function")?,
                    statement: hex(r, "statement")?,
                    root: hex(r, "root")?,
                    site,
                    snapshot: None,
                    begin_position: r.line,
                    end_position: None,
                });
                self.emission_stack.push(id);
                self.frames.push(Frame::Emission(id));
                self.totals.emissions += 1;
            }
            "pv.source.emission.end" => {
                let id: u32 = r.number("emission")?;
                self.end_frame(Frame::Emission(id))?;
                ensure!(
                    self.emission_stack.last() == Some(&id),
                    "emission end without begin"
                );
                let e = &self.history.emissions[(id - 1) as usize];
                ensure!(
                    r.number::<u32>("owner")? == e.owner
                        && self.owner() == e.owner
                        && hex(r, "function")? == e.function
                        && hex(r, "current_function")? == e.current_function
                        && hex(r, "statement")? == e.statement
                        && hex(r, "root")? == e.root
                        && hex(r, "site")? == e.site,
                    "emission end mismatch"
                );
                let snapshot = r.number("snapshot")?;
                self.root(1, id, e.root, snapshot)?;
                self.emission_stack.pop();
                ensure!(
                    r.number::<usize>("depth")? == self.emission_stack.len(),
                    "emission end depth mismatch"
                );
                let e = &mut self.history.emissions[(id - 1) as usize];
                e.snapshot = Some(snapshot);
                e.end_position = Some(r.line);
                self.totals.emission_ends += 1;
            }
            "pv.source.node" => self.observe_node(r)?,
            "pv.source.gap" => {
                let _: u32 = r.number("owner")?;
                let _: u32 = r.number("frame")?;
                let _: u8 = r.number("stage")?;
                let _: u32 = r.number("snapshot")?;
                let _ = hex(r, "site")?;
                let _ = r.get("reason")?;
                self.totals.gaps += 1;
                self.first_gap.get_or_insert(r.line);
            }
            "pv.source.checkpoint" => self.checkpoint(r)?,
            _ => unreachable!(),
        }
        self.event += 1;
        self.last_position = r.line;
        self.last_kind = r.kind.clone();
        Ok(())
    }
    fn begin_invocation(&mut self, r: &Record) -> Result<()> {
        let mode = r.kind == "pv.source.mode.begin";
        let stack = if mode {
            &self.mode_stack
        } else {
            &self.clone_stack
        };
        let count = if mode {
            self.totals.modes
        } else {
            self.totals.clones
        };
        let id: u32 = r.number("invocation")?;
        let parent = stack.last().copied().unwrap_or(0);
        ensure!(
            id == count + 1
                && r.number::<u32>(if mode { "parent_mode" } else { "parent_clone" })? == parent
                && r.number::<u32>("owner")? == self.owner()
                && r.number::<usize>("depth")? == stack.len() + 1
                && stack.len() < MAX_FRAMES,
            "invocation begin mismatch"
        );
        let item = Invocation {
            invocation: id,
            parent,
            owner: self.owner(),
            input: hex(r, "input")?,
            output: None,
            mode_scope: if mode {
                None
            } else {
                Some(r.number("mode_scope")?)
            },
            mode_word: if mode {
                Some(r.number("mode_word")?)
            } else {
                None
            },
            caller: if mode { None } else { Some(hex(r, "caller")?) },
            eval_mode: r.number("eval_mode")?,
            alloc_state: r.number("alloc_state")?,
            begin_position: r.line,
            end_position: None,
            end_eval_mode: None,
            end_alloc_state: None,
            input_snapshot: None,
            after_snapshot: None,
            output_snapshot: None,
        };
        if !mode {
            ensure!(
                item.mode_scope == Some(self.mode_stack.last().copied().unwrap_or(0)),
                "clone mode scope mismatch"
            );
        }
        if mode {
            self.mode_stack.push(id);
            self.frames.push(Frame::Mode(id));
            self.history.modes.push(item);
            self.totals.modes += 1;
        } else {
            self.clone_stack.push(id);
            self.frames.push(Frame::Clone(id));
            self.history.clones.push(item);
            self.totals.clones += 1;
        }
        Ok(())
    }
    fn end_invocation(&mut self, r: &Record) -> Result<()> {
        let mode = r.kind == "pv.source.mode.end";
        let id: u32 = r.number("invocation")?;
        self.end_frame(if mode {
            Frame::Mode(id)
        } else {
            Frame::Clone(id)
        })?;
        let stack = if mode {
            &self.mode_stack
        } else {
            &self.clone_stack
        };
        ensure!(stack.last() == Some(&id), "invocation end without begin");
        let item = if mode {
            &self.history.modes[(id - 1) as usize]
        } else {
            &self.history.clones[(id - 1) as usize]
        };
        ensure!(
            r.number::<u32>(if mode { "parent_mode" } else { "parent_clone" })? == item.parent
                && r.number::<u32>("owner")? == item.owner
                && self.owner() == item.owner
                && hex(r, "input")? == item.input
                && r.number::<usize>("depth")? + 1 == stack.len(),
            "invocation end mismatch"
        );
        if !mode {
            ensure!(
                Some(r.number("mode_scope")?) == item.mode_scope,
                "invocation mode metadata mismatch"
            );
        }
        let input_snapshot = r.number("input_snapshot")?;
        let after_snapshot = r.number("after_snapshot")?;
        let output_snapshot = r.number("output_snapshot")?;
        let output = hex(r, "output")?;
        let first = if mode { 5 } else { 2 };
        self.root(first, id, item.input, input_snapshot)?;
        self.root(first + 1, id, item.input, after_snapshot)?;
        self.root(first + 2, id, output, output_snapshot)?;
        let item = if mode {
            &mut self.history.modes[(id - 1) as usize]
        } else {
            &mut self.history.clones[(id - 1) as usize]
        };
        item.output = Some(output);
        item.end_position = Some(r.line);
        item.end_eval_mode = Some(r.number("eval_mode")?);
        item.end_alloc_state = Some(r.number("alloc_state")?);
        item.input_snapshot = Some(input_snapshot);
        item.after_snapshot = Some(after_snapshot);
        item.output_snapshot = Some(output_snapshot);
        if mode {
            self.mode_stack.pop();
            self.totals.mode_ends += 1;
        } else {
            self.clone_stack.pop();
            self.totals.clone_ends += 1;
        }
        Ok(())
    }
    fn observe_node(&mut self, r: &Record) -> Result<()> {
        let n = node(r)?;
        ensure!(
            n.snapshot == self.totals.snapshots + 1,
            "snapshot id mismatch"
        );
        let frame = match n.stage {
            1 => self.emission_stack.last(),
            2..=4 => self.clone_stack.last(),
            5..=7 => self.mode_stack.last(),
            _ => None,
        };
        ensure!(frame == Some(&n.frame), "snapshot has no exact frame");
        let expected_frame = match n.stage {
            1 => Frame::Emission(n.frame),
            2..=4 => Frame::Clone(n.frame),
            _ => Frame::Mode(n.frame),
        };
        ensure!(
            self.frames.last() == Some(&expected_frame),
            "snapshot crosses an active retail frame"
        );
        if self
            .walk
            .as_ref()
            .is_some_and(|w| w.frame != n.frame || w.stage != n.stage)
        {
            self.finish_walk()?;
        }
        if self.walk.is_none() {
            ensure!(
                n.parent == 0 && n.edge == 0 && !self.roots.contains_key(&(n.stage, n.frame)),
                "invalid stage root"
            );
            if n.stage != 1 {
                let (first, input) = if n.stage <= 4 {
                    (2, self.history.clones[(n.frame - 1) as usize].input)
                } else {
                    (5, self.history.modes[(n.frame - 1) as usize].input)
                };
                ensure!(
                    (first..n.stage)
                        .all(|stage| input == 0 || self.roots.contains_key(&(stage, n.frame)))
                        && (n.stage + 1..=first + 2)
                            .all(|stage| !self.roots.contains_key(&(stage, n.frame))),
                    "invocation stage trees reordered"
                );
            }
            self.walk = Some(Walk {
                frame: n.frame,
                stage: n.stage,
                root: n.snapshot,
                ancestors: vec![],
                nodes: vec![],
            });
        }
        let w = self.walk.as_mut().unwrap();
        ensure!(
            w.nodes.len() < MAX_TREE_RECORDS,
            "tree record capacity exceeded"
        );
        if n.parent != 0 {
            while w.ancestors.last().is_some_and(|id| *id != n.parent) {
                w.ancestors.pop();
            }
            ensure!(
                w.ancestors.last() == Some(&n.parent) && n.edge > 0,
                "non-preorder snapshot parent"
            );
            let parent = &mut self.history.nodes[(n.parent - 1) as usize];
            ensure!(
                n.edge as usize <= slots(parent.shape)
                    && parent.data[(n.edge - 1) as usize] == n.node
                    && parent.children.insert(n.edge, n.snapshot).is_none(),
                "child slot/address mismatch"
            );
            ensure!(
                (1..n.edge).all(|edge| parent.data[(edge - 1) as usize] == 0
                    || parent.children.contains_key(&edge)),
                "child observations not in slot order"
            );
        } else {
            ensure!(w.nodes.is_empty(), "multiple stage roots");
        }
        let reason = if w.ancestors.len() >= 32 {
            Some("depth")
        } else if w.nodes.len() >= 512 {
            Some("capacity")
        } else if w
            .ancestors
            .iter()
            .any(|id| self.history.nodes[(*id - 1) as usize].node == n.node)
        {
            Some("cycle")
        } else if n.node < 0x10000 {
            Some("pointer")
        } else {
            None
        };
        ensure!(
            if let Some(reason) = reason {
                !n.complete && n.reason == reason
            } else {
                n.tag_known
            },
            "node walker guard/reason mismatch"
        );
        w.ancestors.push(n.snapshot);
        w.nodes.push(n.snapshot);
        self.history.nodes.push(n);
        self.totals.snapshots += 1;
        Ok(())
    }
    fn checkpoint(&mut self, r: &Record) -> Result<()> {
        let arena_checkpoint = if matches!(
            self.last_kind.as_str(),
            "pv.source.owner.end" | "pv.source.mode.end"
        ) {
            None
        } else {
            Some(
                *self
                    .next_arena
                    .get(&r.line)
                    .ok_or_else(|| anyhow!("source checkpoint has no guarded origin"))?,
            )
        };
        self.totals.checkpoints += 1;
        let actual = Totals {
            checkpoints: r.number("checkpoints")?,
            owners: r.number("owners")?,
            owner_ends: r.number("owner_ends")?,
            modes: r.number("modes")?,
            mode_ends: r.number("mode_ends")?,
            clones: r.number("clones")?,
            clone_ends: r.number("clone_ends")?,
            emissions: r.number("emissions")?,
            emission_ends: r.number("emission_ends")?,
            snapshots: r.number("snapshots")?,
            gaps: r.number("gaps")?,
        };
        let depths = [
            r.number("owner_depth")?,
            r.number("mode_depth")?,
            r.number("clone_depth")?,
            r.number("emission_depth")?,
        ];
        let unsupported = bit(r, "unsupported")?;
        ensure!(
            actual == self.totals && depths == self.depths(),
            "source checkpoint totals/depth mismatch"
        );
        ensure!(
            unsupported == self.first_gap.is_some(),
            "source unsupported state unlocalized or cleared"
        );
        let observations = if depths == [0; 4] {
            let end = self.first_gap.map_or(r.line - 1, |p| p.saturating_sub(1));
            if end >= self.first_position {
                self.history.coverage_intervals.push(CoverageInterval {
                    begin_position: self.first_position,
                    end_position: end,
                    checkpoint_position: r.line,
                });
            }
            if unsupported {
                gap("source checkpoint covers only the prefix before a gap")
            } else {
                known()
            }
        } else {
            gap("source checkpoint has active frames")
        };
        self.history.checkpoints.push(Checkpoint {
            trace_position: r.line,
            totals: actual,
            depths,
            unsupported,
            observations,
            arena_checkpoint,
        });
        Ok(())
    }
}

fn source_records(text: &str) -> Vec<Record> {
    text.lines()
        .enumerate()
        .filter_map(|(index, line)| {
            if !trace::record_kind(line).is_some_and(is_record) {
                return None;
            }
            let mut r = trace::records(line).pop()?;
            r.line = index + 1;
            Some(r)
        })
        .take(MAX_EVENTS + 1)
        .collect()
}
pub fn parse(text: &str) -> Result<History> {
    let records = source_records(text);
    if records.is_empty() {
        return Ok(History::default());
    }
    let mut v = Validator::new();
    let mut failure = None;
    let mut pending = None;
    for (index, line) in text.lines().enumerate() {
        if line.trim().is_empty() {
            continue;
        }
        if let Some(position) = pending.take()
            && trace::record_kind(line) == Some("pv.arena.checkpoint")
        {
            v.next_arena.insert(position, index + 1);
        }
        if trace::record_kind(line) == Some("pv.source.checkpoint") {
            pending = Some(index + 1);
        }
    }
    for r in &records {
        if v.event as usize == MAX_EVENTS {
            failure = Some("source event capacity exceeded".into());
            break;
        }
        if let Err(error) = v.accept(r) {
            failure = Some(format!("source trace line {}: {error:#}", r.line));
            break;
        }
    }
    if failure.is_none()
        && let Err(error) = v.finish_walk()
    {
        failure = Some(format!("source trailing tree: {error:#}"));
    }
    v.history.observations = if let Some(error) = failure {
        gap(error)
    } else if v.history.checkpoints.last().is_none_or(|c| {
        c.trace_position != v.last_position || c.observations.state != EvaluationState::Satisfied
    }) {
        gap("source stream has no final balanced reconciled checkpoint")
    } else {
        known()
    };
    v.history.records = records;
    v.history.trace = text.into();
    Ok(v.history)
}

/// An immutable read observation, not a live node handle or a payload revision.
#[derive(Clone, Debug, Serialize)]
pub struct SnapshotReference {
    #[serde(skip)]
    capture: Arc<()>,
    node: Node,
    allocation: HistoricalReference,
    /// Every supported descendant and Object leaf has its own historical receipt.
    descendants: Vec<HistoricalReference>,
}
impl SnapshotReference {
    pub fn snapshot(&self) -> u32 {
        self.node.snapshot
    }
    pub fn observation(&self) -> &Node {
        &self.node
    }
    pub fn allocation(&self) -> &HistoricalReference {
        &self.allocation
    }
    pub fn same_observation(&self, other: &Self) -> Answer<bool> {
        if !Arc::ptr_eq(&self.capture, &other.capture) {
            return Answer::unknown("snapshot receipt belongs to another prepared source capture");
        }
        Answer::known(self.snapshot() == other.snapshot())
    }
    pub fn same_allocation(&self, other: &Self) -> Answer<bool> {
        if !Arc::ptr_eq(&self.capture, &other.capture) {
            return Answer::unknown("snapshot receipt belongs to another prepared source capture");
        }
        match self.allocation.same_allocation(&other.allocation).value {
            Some(same) => Answer::known(same),
            None => Answer::unknown("foreign arena allocation receipt"),
        }
    }
}
/// Actual emission of a supported assignment root. This is not a declaration
/// initializer, source token binding, dominance proof or backend origin path.
#[derive(Clone, Debug, Serialize)]
pub struct SourceWriteReference {
    #[serde(skip)]
    capture: Arc<()>,
    emission: u32,
    root: SnapshotReference,
    lhs: SnapshotReference,
    rhs: SnapshotReference,
    statement: HistoricalReference,
    owner: HistoricalReference,
}
impl SourceWriteReference {
    pub fn emission(&self) -> u32 {
        self.emission
    }
    pub fn root(&self) -> &SnapshotReference {
        &self.root
    }
    pub fn lhs(&self) -> &SnapshotReference {
        &self.lhs
    }
    pub fn rhs(&self) -> &SnapshotReference {
        &self.rhs
    }
    pub fn statement(&self) -> &HistoricalReference {
        &self.statement
    }
    pub fn owner(&self) -> &HistoricalReference {
        &self.owner
    }
    pub fn same_observation(&self, other: &Self) -> Answer<bool> {
        if !Arc::ptr_eq(&self.capture, &other.capture) {
            return Answer::unknown("foreign source-write receipt");
        }
        Answer::known(self.emission == other.emission)
    }
}
/// One actually observed guarded clone/substitution invocation. Before, after
/// and output remain different reads even when their pointers/allocations agree.
/// The edge establishes an actual call relationship, not semantic equivalence.
#[derive(Clone, Debug, Serialize)]
pub struct CloneEdgeReference {
    #[serde(skip)]
    capture: Arc<()>,
    invocation: u32,
    parent_clone: u32,
    mode_scope: u32,
    input: Option<SnapshotReference>,
    after: Option<SnapshotReference>,
    output: Option<SnapshotReference>,
    owner: Option<HistoricalReference>,
    owner_observations: Evaluation,
}
impl CloneEdgeReference {
    pub fn invocation(&self) -> u32 {
        self.invocation
    }
    pub fn parent_clone(&self) -> u32 {
        self.parent_clone
    }
    pub fn mode_scope(&self) -> u32 {
        self.mode_scope
    }
    pub fn input(&self) -> Option<&SnapshotReference> {
        self.input.as_ref()
    }
    pub fn after(&self) -> Option<&SnapshotReference> {
        self.after.as_ref()
    }
    pub fn output(&self) -> Option<&SnapshotReference> {
        self.output.as_ref()
    }
    pub fn owner(&self) -> Option<&HistoricalReference> {
        self.owner.as_ref()
    }
    pub fn owner_observations(&self) -> &Evaluation {
        &self.owner_observations
    }
    pub fn same_observation(&self, other: &Self) -> Answer<bool> {
        if !Arc::ptr_eq(&self.capture, &other.capture) {
            return Answer::unknown("foreign clone-edge receipt");
        }
        Answer::known(self.invocation == other.invocation)
    }
}

struct PreparedData {
    history: History,
    intervals: Vec<CoverageInterval>,
    capture: Arc<()>,
    nodes: BTreeMap<u32, HistoricalReference>,
    objects: BTreeMap<u32, HistoricalReference>,
    owners: BTreeMap<u32, HistoricalReference>,
    statements: BTreeMap<u32, HistoricalReference>,
    invalid: Option<Evaluation>,
}
/// Immutable validation result. Clones share receipt identities; preparing the
/// same raw trace again deliberately creates another source/arena capture.
#[derive(Clone)]
pub struct PreparedNodes {
    data: Arc<PreparedData>,
}
#[derive(Clone, Copy)]
enum Role {
    Node(u32),
    Object(u32),
    Owner(u32),
    Statement(u32),
}
const POOLS: [PoolDomain; 5] = [
    PoolDomain::Heap,
    PoolDomain::Optimizer,
    PoolDomain::Block,
    PoolDomain::Local,
    PoolDomain::Global,
];
impl History {
    pub fn prepare(&self) -> PreparedNodes {
        let mut invalid = None;
        let mut rebuilt = match parse(&self.trace) {
            Ok(history) => history,
            Err(error) => {
                invalid = Some(gap(format!("source reparsing failed: {error:#}")));
                History::default()
            }
        };
        if self.records.len() > MAX_EVENTS
            || self.records.len() != rebuilt.records.len()
            || self.records.iter().zip(&rebuilt.records).any(|(a, b)| {
                a.line != b.line || a.kind != b.kind || a.fields != b.fields || a.raw != b.raw
            })
        {
            invalid = Some(gap(
                "source positioned records disagree with authoritative full trace",
            ));
        }
        let arenas = source_arenas::parse(&self.trace).unwrap_or_default();
        let arena_version = arenas.events.iter().find_map(|event| {
            if let source_arenas::EventKind::Begin { version, .. } = event.observation {
                Some(version)
            } else {
                None
            }
        });
        if rebuilt.version.is_none() || rebuilt.version != arena_version {
            invalid = Some(gap("source/arena compiler scope absent or different"));
        }
        let intervals = rebuilt
            .coverage_intervals
            .iter()
            .filter(|interval| {
                rebuilt
                    .checkpoints
                    .iter()
                    .find(|c| c.trace_position == interval.checkpoint_position)
                    .is_some_and(|checkpoint| {
                        checkpoint.arena_checkpoint.is_none_or(|position| {
                            arenas.checkpoints.iter().any(|c| {
                                c.trace_position == position
                                    && c.observations.state == EvaluationState::Satisfied
                            })
                        })
                    })
            })
            .cloned()
            .collect();
        let arena = arenas.prepare();
        let mut requests = vec![];
        for node in &rebuilt.nodes {
            requests.push((
                Role::Node(node.snapshot),
                node.trace_position,
                node.node,
                30,
            ));
            if node.complete && node.shape == 4 {
                requests.push((
                    Role::Object(node.snapshot),
                    node.trace_position,
                    node.data[0],
                    1,
                ));
            }
        }
        for owner in &rebuilt.owners {
            requests.push((
                Role::Owner(owner.owner),
                owner.begin_position,
                owner.function,
                1,
            ));
        }
        for emission in &rebuilt.emissions {
            requests.push((
                Role::Statement(emission.emission),
                emission.begin_position,
                emission.statement,
                30,
            ));
        }
        let queries = requests
            .iter()
            .flat_map(|(_, trace_position, address, extent)| {
                POOLS.iter().map(move |pool| Query {
                    trace_position: *trace_position,
                    pool: *pool,
                    address: *address,
                    extent: *extent,
                })
            })
            .collect::<Vec<_>>();
        let mut results = arena.resolve_references(&queries).into_iter();
        let mut nodes = BTreeMap::new();
        let mut objects = BTreeMap::new();
        let mut owners = BTreeMap::new();
        let mut statements = BTreeMap::new();
        for (role, _, _, _) in requests {
            let found = results
                .by_ref()
                .take(5)
                .filter_map(|answer| {
                    if answer.observations.state == EvaluationState::Satisfied {
                        answer.reference
                    } else {
                        None
                    }
                })
                .collect::<Vec<_>>();
            if found.len() == 1 {
                let receipt = found.into_iter().next().unwrap();
                match role {
                    Role::Node(id) => {
                        nodes.insert(id, receipt);
                    }
                    Role::Object(id) => {
                        objects.insert(id, receipt);
                    }
                    Role::Owner(id) => {
                        owners.insert(id, receipt);
                    }
                    Role::Statement(id) => {
                        statements.insert(id, receipt);
                    }
                }
            }
        }
        // The complete trace remains solely in the display History outside the
        // prepared immutable timeline; no second retained trace copy is needed.
        rebuilt.trace.clear();
        PreparedNodes {
            data: Arc::new(PreparedData {
                history: rebuilt,
                intervals,
                capture: Arc::new(()),
                nodes,
                objects,
                owners,
                statements,
                invalid,
            }),
        }
    }
}
impl PreparedData {
    fn covered(&self, position: usize) -> bool {
        self.invalid.is_none()
            && self
                .intervals
                .iter()
                .any(|i| i.begin_position <= position && position <= i.end_position)
    }
    fn frame_end(&self, node: &Node) -> Option<usize> {
        match node.stage {
            1 => self
                .history
                .emissions
                .get((node.frame - 1) as usize)
                .and_then(|f| f.end_position),
            2..=4 => self
                .history
                .clones
                .get((node.frame - 1) as usize)
                .and_then(|f| f.end_position),
            5..=7 => self
                .history
                .modes
                .get((node.frame - 1) as usize)
                .and_then(|f| f.end_position),
            _ => None,
        }
    }
    fn snapshot(&self, id: u32) -> Answer<SnapshotReference> {
        let Some(root) = id
            .checked_sub(1)
            .and_then(|index| self.history.nodes.get(index as usize))
        else {
            return Answer::unknown("snapshot observation absent");
        };
        let mut pending = vec![id];
        let mut descendants = vec![];
        while let Some(id) = pending.pop() {
            let node = &self.history.nodes[(id - 1) as usize];
            if !node.complete
                || !self.covered(node.trace_position)
                || self
                    .frame_end(node)
                    .is_none_or(|position| !self.covered(position))
            {
                return Answer::unknown(
                    "snapshot tree is unsupported, incomplete or outside reconciled frame coverage",
                );
            }
            let Some(receipt) = self.nodes.get(&id) else {
                return Answer::unknown(
                    "snapshot node has no unique historical allocation receipt",
                );
            };
            descendants.push(receipt.clone());
            if node.shape == 4 {
                let Some(object) = self.objects.get(&id) else {
                    return Answer::unknown(
                        "Object leaf has no unique historical allocation receipt",
                    );
                };
                descendants.push(object.clone());
            }
            pending.extend(node.children.values().copied());
        }
        Answer::known(SnapshotReference {
            capture: self.capture.clone(),
            node: root.clone(),
            allocation: self.nodes[&root.snapshot].clone(),
            descendants,
        })
    }
    fn owner(&self, id: u32) -> Answer<HistoricalReference> {
        let Some(owner) = id
            .checked_sub(1)
            .and_then(|index| self.history.owners.get(index as usize))
        else {
            return Answer::unknown("explicit frontend owner absent");
        };
        if !self.covered(owner.begin_position)
            || owner
                .end_position
                .is_none_or(|position| !self.covered(position))
        {
            return Answer::unknown("frontend owner frame is outside reconciled coverage");
        }
        match self.owners.get(&id) {
            Some(reference) => Answer::known(reference.clone()),
            None => Answer::unknown("frontend owner has no unique historical allocation receipt"),
        }
    }
}
impl PreparedNodes {
    pub fn history(&self) -> &History {
        &self.data.history
    }
    pub fn observations(&self) -> &Evaluation {
        self.data
            .invalid
            .as_ref()
            .unwrap_or(&self.data.history.observations)
    }
    pub fn snapshot(&self, id: u32) -> Answer<SnapshotReference> {
        self.data.snapshot(id)
    }
    pub fn source_write(&self, id: u32) -> Answer<SourceWriteReference> {
        let Some(emission) = id
            .checked_sub(1)
            .and_then(|index| self.data.history.emissions.get(index as usize))
        else {
            return Answer::unknown("emission observation absent");
        };
        if !self.data.covered(emission.begin_position)
            || emission
                .end_position
                .is_none_or(|position| !self.data.covered(position))
        {
            return Answer::unknown("emission frame outside independent coverage");
        }
        let Some(owner) = emission
            .owner
            .checked_sub(1)
            .and_then(|index| self.data.history.owners.get(index as usize))
        else {
            return Answer::unknown("source write has no explicit frontend owner");
        };
        if owner.function == 0
            || owner.function != emission.function
            || emission.current_function != owner.function
        {
            return Answer::unknown("emission/current/owner function identities do not agree");
        }
        let owner = match self.data.owner(emission.owner) {
            Answer {
                value: Some(owner), ..
            } => owner,
            answer => {
                return Answer {
                    value: None,
                    observations: answer.observations,
                };
            }
        };
        let Some(statement) = self.data.statements.get(&id) else {
            return Answer::unknown("statement has no unique 30-byte allocation receipt");
        };
        let root = match self.snapshot(emission.snapshot.unwrap_or(0)) {
            Answer {
                value: Some(root), ..
            } => root,
            answer => {
                return Answer {
                    value: None,
                    observations: answer.observations,
                };
            }
        };
        if root.node.tag != 30 || root.node.stage != 1 || root.node.frame != id {
            return Answer::unknown("emission root is not a supported observed assignment");
        }
        let Some(lhs) = root
            .node
            .children
            .get(&1)
            .and_then(|id| self.snapshot(*id).value)
        else {
            return Answer::unknown("assignment LHS snapshot absent");
        };
        let Some(rhs) = root
            .node
            .children
            .get(&2)
            .and_then(|id| self.snapshot(*id).value)
        else {
            return Answer::unknown("assignment RHS snapshot absent");
        };
        Answer::known(SourceWriteReference {
            capture: self.data.capture.clone(),
            emission: id,
            root,
            lhs,
            rhs,
            statement: statement.clone(),
            owner,
        })
    }
    pub fn clone_edge(&self, id: u32) -> Answer<CloneEdgeReference> {
        let Some(frame) = id
            .checked_sub(1)
            .and_then(|index| self.data.history.clones.get(index as usize))
        else {
            return Answer::unknown("actual clone invocation absent");
        };
        if !self.data.covered(frame.begin_position)
            || frame
                .end_position
                .is_none_or(|position| !self.data.covered(position))
        {
            return Answer::unknown("clone invocation lacks balanced independent coverage");
        }
        if frame.eval_mode > 4
            || frame.alloc_state > 1
            || frame.end_eval_mode.is_none_or(|mode| mode > 4)
            || frame.end_alloc_state.is_none_or(|state| state > 1)
        {
            return Answer::unknown("clone evaluation/allocation mode unsupported");
        }
        if let Some(mode) = frame
            .mode_scope
            .filter(|id| *id != 0)
            .and_then(|id| self.data.history.modes.get((id - 1) as usize))
            && mode.mode_word.is_none_or(|word| word & 255 > 4)
        {
            return Answer::unknown("enclosing mode argument unsupported");
        }
        let mut references = vec![];
        for id in [
            frame.input_snapshot,
            frame.after_snapshot,
            frame.output_snapshot,
        ] {
            let Some(id) = id else {
                return Answer::unknown("clone stage root absent");
            };
            if id == 0 {
                references.push(None);
            } else {
                let answer = self.snapshot(id);
                if answer.value.is_none() {
                    return Answer {
                        value: None,
                        observations: answer.observations,
                    };
                }
                references.push(answer.value);
            }
        }
        let owner = self.data.owner(frame.owner);
        Answer::known(CloneEdgeReference {
            capture: self.data.capture.clone(),
            invocation: id,
            parent_clone: frame.parent,
            mode_scope: frame.mode_scope.unwrap_or(0),
            input: references.remove(0),
            after: references.remove(0),
            output: references.remove(0),
            owner: owner.value,
            owner_observations: owner.observations,
        })
    }
}
