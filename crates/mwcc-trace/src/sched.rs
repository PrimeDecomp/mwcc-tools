//! The real list scheduler, observed: per basic block the dependence DAG GC/2.7 built (with
//! latencies, heights, deadlines) and the sequence of picks (cycle, instruction), recorded by the
//! tracer (`TraceOptions::sched`). This module replays the pick rule on that data to say *why* each
//! instruction was issued where it was, and answers "why is X before Y" questions:
//! - a dependence path from X to Y (data / memory / anti / output / ordering edge): no source
//!   reordering can swap them;
//! - Y was not ready when X issued (a predecessor or a latency held it back);
//! - both were ready and the pick rule chose X: deadline (urgency), number of successors uncovered,
//!   height (longest path to the block end), opcode rank (pre-RA only), or **program order**: only
//!   in that last case does the order of the source statements decide.
//!
//! Pick rule (GC/2.7 `select_ready_coloring_node` `0x507e70`): scan ready nodes in program order;
//! the first is the pick; a later candidate replaces it if it is urgent (deadline <= cycle) and the
//! pick is not; an urgent pick is never replaced by a non-urgent one; otherwise the candidate wins
//! with more uncovered successors, else greater height, else (pre-RA only) strictly lower opcode rank.

use crate::records::{Alias, AliasKind};
use serde::Serialize;

#[derive(Clone, Copy, Debug, PartialEq, Eq, Serialize)]
pub enum EdgeKind {
    /// true register dependence (the successor reads what the node writes)
    Data,
    /// memory order between two memory accesses (alias analysis said they may alias)
    Memory,
    /// the successor overwrites a register the node writes
    Output,
    /// the successor overwrites a register the node reads
    Anti,
    /// serialisation (calls, side effects, the block's branch)
    Order,
}

#[derive(Clone, Debug, Serialize)]
pub struct SchedEdge {
    pub to: usize,
    /// cycles the successor must wait after the node issues
    pub latency: u16,
    pub kind: EdgeKind,
}

#[derive(Clone, Debug, Serialize)]
pub struct SchedNode {
    /// PCode text (virtual registers in the pre-RA pass)
    pub text: String,
    /// opcode rank (pre-RA tie-break, lower wins)
    pub opcode_rank: u8,
    pub latency: u16,
    /// longest latency-weighted path to the block end
    pub height: u16,
    /// latest cycle before the node becomes urgent (max height - height)
    pub deadline: u16,
    /// number of predecessors
    pub preds: u16,
    pub succs: Vec<SchedEdge>,
    /// a memory access's alias in [`SchedBlock::aliases`] (with the alias stream)
    #[serde(skip_serializing_if = "Option::is_none")]
    pub alias: Option<u32>,
    /// the PCode opcode and flags (for the machine model)
    #[serde(skip)]
    pub op: i16,
    #[serde(skip)]
    pub flags: u32,
    /// the GPRs the instruction reads or writes, and the one its first operand writes (`is_dependent`)
    #[serde(skip)]
    pub gprs: Vec<i16>,
    #[serde(skip)]
    pub first_def: Option<i16>,
    #[serde(skip)]
    pub(crate) raw_succs: Vec<(usize, u16)>,
}

/// One alias query the scheduler made while it built a block's DAG: may the access `first` touch what the later
/// access `second` does? When it may, `second` waits `first`'s latency after it.
#[derive(Clone, Copy, Debug, Serialize)]
pub struct MemoryQuery {
    pub first: usize,
    pub second: usize,
    pub may: bool,
}

/// Two memory accesses the scheduler left unordered (the alias analysis said they cannot alias) and that nothing
/// else orders.
#[derive(Clone, Debug, Serialize)]
pub struct UnorderedAccesses {
    /// in program order
    pub first: usize,
    pub second: usize,
    /// the cycles they issued at
    pub first_cycle: u16,
    pub second_cycle: u16,
    /// what a may-alias answer would have added: `second` at least this many cycles after `first`
    pub latency: u16,
    /// the edge would have changed the schedule: `second` issued before `first_cycle + latency`
    pub binding: bool,
    /// the edge would have changed the order (scheduling the block again with it), when scheduling the block again
    /// without it gives the recorded order
    pub reorders: Option<bool>,
    /// the block scheduled again with the edge added (in issue order), when scheduling it again without the edge
    /// gives the recorded order
    pub with_edge: Option<Vec<SchedPick>>,
}

/// The offset make_distinct_virtual_alias gives the object it makes; its accesses are offsets from it.
const DISTINCT_BASE: i32 = 0x7F_FFFF;

/// Whether alias `index` is part of the object of a read-only or restrict pointer's pointee
/// (make_distinct_virtual_alias: an object no other pointer reaches).
pub fn is_distinct_pointee(aliases: &[Alias], index: u32) -> bool {
    aliases.iter().any(|a| {
        a.index == index && a.kind == AliasKind::Part && a.offset >= DISTINCT_BASE - 0x10_0000
    })
}

/// What an alias stands for, as far as the scheduler's order is concerned.
pub fn describe_alias(aliases: &[Alias], index: u32) -> String {
    let Some(a) = aliases.iter().find(|a| a.index == index) else {
        return format!("alias {index}");
    };
    let object = a.object.as_deref().unwrap_or("?");
    match a.kind {
        AliasKind::Set => {
            let names: Vec<String> = a
                .members
                .iter()
                .map(|&m| match aliases.iter().find(|b| b.index == m) {
                    Some(b) if b.kind != AliasKind::Set => describe_alias(aliases, m),
                    _ => format!("alias {m}"),
                })
                .collect();
            if names.iter().any(|n| n == "@worst_case@") {
                "any memory a pointer can reach (the worst case)".into()
            } else {
                format!("one of {{{}}}", names.join(", "))
            }
        }
        AliasKind::Part if is_distinct_pointee(aliases, index) => format!(
            "{object}+{:#x} ({} bytes), the pointee of a read-only or restrict pointer",
            a.offset - DISTINCT_BASE,
            a.size
        ),
        AliasKind::Part => format!("{object}+{:#x} ({} bytes)", a.offset, a.size),
        AliasKind::Object => object.to_string(),
    }
}

#[derive(Clone, Copy, Debug, Serialize)]
pub struct SchedPick {
    pub cycle: u16,
    pub node: usize,
}

#[derive(Clone, Debug, Serialize)]
pub struct SchedBlock {
    pub function: String,
    /// first scheduling pass (virtual registers, decides interference) vs post-RA (final order)
    pub pre_ra: bool,
    pub block: u32,
    /// in input (program) order
    pub nodes: Vec<SchedNode>,
    /// in issue order
    pub picks: Vec<SchedPick>,
    /// the alias table in force (with the alias stream): the nodes' `alias` are its indices
    #[serde(skip_serializing_if = "Vec::is_empty")]
    pub aliases: Vec<Alias>,
    /// the alias queries made while the DAG was built (with the alias queries' stream)
    #[serde(skip_serializing_if = "Vec::is_empty")]
    pub memory: Vec<MemoryQuery>,
}

fn mnemonic(text: &str) -> &str {
    text.split_whitespace().next().unwrap_or("")
}

fn is_mem(text: &str) -> bool {
    let m = mnemonic(text);
    (m.starts_with('l') && !matches!(m, "li" | "lis"))
        || m.starts_with("st")
        || m.starts_with("psq_")
}

/// A register operand of an instruction: its class, its number, and whether it is read and written.
type Operand = (u8, i16, bool, bool);

/// Fill `succs` with edge kinds from the register operands `(class, reg, read, write)`.
pub(crate) fn classify_edges(mut nodes: Vec<SchedNode>, regs: &[Vec<Operand>]) -> Vec<SchedNode> {
    for a in 0..nodes.len() {
        let raw = std::mem::take(&mut nodes[a].raw_succs);
        let mut out = vec![];
        for &(s, lat) in &raw {
            let ra = &regs[a];
            let rs = &regs[s];
            let any = |f: &dyn Fn(&Operand, &Operand) -> bool| {
                ra.iter()
                    .any(|x| rs.iter().any(|y| x.0 == y.0 && x.1 == y.1 && f(x, y)))
            };
            // register relations first: a memory edge is only certain when no register explains it
            let kind = if any(&|x, y| x.3 && y.2) {
                EdgeKind::Data
            } else if any(&|x, y| x.3 && y.3) {
                EdgeKind::Output
            } else if any(&|x, y| x.2 && y.3) {
                EdgeKind::Anti
            } else if is_mem(&nodes[a].text) && is_mem(&nodes[s].text) {
                EdgeKind::Memory
            } else {
                EdgeKind::Order
            };
            out.push(SchedEdge {
                to: s,
                latency: lat,
                kind,
            });
        }
        out.sort_by_key(|e| e.to);
        nodes[a].succs = out;
    }
    nodes
}

/// A ready instruction as the pick rule compares it.
#[derive(Clone, Copy, Debug)]
pub(crate) struct Priority {
    /// the cycle from which it is urgent
    pub deadline: i64,
    /// the successors issuing it makes ready
    pub uncovers: i64,
    pub height: i64,
    pub opcode_rank: i64,
}

/// What decided a comparison of the pick rule.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub(crate) enum Criterion {
    /// one of the two is urgent (deadline reached) and the other is not
    Urgent,
    Uncovers,
    Height,
    OpcodeRank,
    /// nothing: the earlier one in program order stays
    Tie,
}

/// The pick rule's comparison at `cycle`: does the later ready candidate `cand` replace the pick `best`, and what
/// decided it. `rank`: the opcode-rank tie-break applies (pre-RA).
pub(crate) fn pick_rule(
    best: Priority,
    cand: Priority,
    cycle: i64,
    rank: bool,
) -> (bool, Criterion) {
    let (b_urgent, c_urgent) = (best.deadline <= cycle, cand.deadline <= cycle);
    if b_urgent != c_urgent {
        (c_urgent, Criterion::Urgent)
    } else if cand.uncovers != best.uncovers {
        (cand.uncovers > best.uncovers, Criterion::Uncovers)
    } else if cand.height != best.height {
        (cand.height > best.height, Criterion::Height)
    } else if rank && cand.opcode_rank != best.opcode_rank {
        (cand.opcode_rank < best.opcode_rank, Criterion::OpcodeRank)
    } else {
        (false, Criterion::Tie)
    }
}

/// The recorded picks replayed: which instructions are ready, and what each one uncovers.
struct Replay<'a> {
    nodes: &'a [SchedNode],
    preds_left: Vec<u16>,
    earliest: Vec<u16>,
    done: Vec<bool>,
}

impl<'a> Replay<'a> {
    fn new(nodes: &'a [SchedNode]) -> Self {
        let n = nodes.len();
        let mut preds_left: Vec<u16> = vec![0; n];
        for a in nodes {
            for e in &a.succs {
                preds_left[e.to] += 1;
            }
        }
        Replay {
            nodes,
            preds_left,
            earliest: vec![0; n],
            done: vec![false; n],
        }
    }

    /// The nodes ready at `cycle`, in program order.
    fn ready(&self, cycle: u16) -> Vec<usize> {
        (0..self.nodes.len())
            .filter(|&i| !self.done[i] && self.preds_left[i] == 0 && self.earliest[i] <= cycle)
            .collect()
    }

    /// The successors of `i` that issuing it makes ready.
    fn uncover(&self, i: usize) -> usize {
        self.nodes[i]
            .succs
            .iter()
            .filter(|e| self.preds_left[e.to] == 1)
            .count()
    }

    fn issue(&mut self, x: usize, cycle: u16) {
        self.done[x] = true;
        for e in &self.nodes[x].succs {
            self.preds_left[e.to] = self.preds_left[e.to].saturating_sub(1);
            self.earliest[e.to] = self.earliest[e.to].max(cycle + e.latency);
        }
    }
}

#[derive(Clone, Copy, Debug, PartialEq, Eq, Serialize)]
pub enum PickReason {
    /// the only ready instruction
    OnlyReady,
    /// first ready in program order and nothing beat it: statement order decided
    ProgramOrder,
    /// it was urgent (deadline reached) and the earlier ready one was not
    Urgent,
    /// it uncovers more successors than the earlier ready one
    Uncovers,
    /// greater height (longer path to the block end)
    Height,
    /// pre-RA: lower opcode rank
    OpcodeRank,
    /// the rule preferred another ready instruction that the 750 unit model could not issue
    UnitBusy,
    /// replay diverged from the recorded picks (should not happen)
    Unknown,
}

#[derive(Clone, Debug, Serialize)]
pub struct PickInfo {
    pub cycle: u16,
    pub node: usize,
    /// ready nodes at that moment (program order), ignoring unit availability
    pub ready: Vec<usize>,
    pub reason: PickReason,
    /// the node it was preferred over (first ready in program order), or for `UnitBusy` the node
    /// the rule would have picked
    pub over: Option<usize>,
}

#[derive(Clone, Debug, Serialize)]
pub enum WhyKind {
    /// a dependence path first -> ... -> second (edge kinds along the path)
    Dependence(Vec<EdgeKind>),
    /// `second` was not ready when `first` issued: waiting for this predecessor / until this cycle
    NotReady {
        waiting_for: Option<usize>,
        ready_at: u16,
    },
    /// both ready; the pick rule chose `first` for this reason
    Priority(PickReason),
    /// `first` was not issued before `second`
    NotBefore,
}

#[derive(Clone, Debug, Serialize)]
pub struct Explanation {
    pub first: usize,
    pub second: usize,
    pub why: WhyKind,
    /// true when swapping the two source statements can change the order (program-order tie-break,
    /// or the waiting predecessor is itself movable), false when a dependence or a priority decides
    pub statement_order_matters: bool,
    pub text: String,
}

/// See [`SchedBlock::rule_evidence`].
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq, Serialize)]
pub struct RuleEvidence {
    pub agree: u32,
    pub contradict: u32,
    pub other: u32,
}

impl std::ops::AddAssign for RuleEvidence {
    fn add_assign(&mut self, o: Self) {
        self.agree += o.agree;
        self.contradict += o.contradict;
        self.other += o.other;
    }
}

impl SchedBlock {
    /// The issue width the picks show (the most instructions issued in one cycle; at least 1).
    pub fn issue_width(&self) -> usize {
        let mut most = 1;
        let mut run = 0;
        let mut last = None;
        for p in &self.picks {
            run = if last == Some(p.cycle) { run + 1 } else { 1 };
            last = Some(p.cycle);
            most = most.max(run);
        }
        most
    }

    /// Schedule the block again as `scheduleblock` does, with the edges `extra` (from, to, latency) added: each
    /// cycle up to `width` picks by the pick rule, among the ready instructions the 750 machine model can issue
    /// (GC 2.x's `machine750`). Heights and deadlines are the recorded ones, raised where the edges lengthen a path.
    /// Gives the picks in issue order.
    pub fn simulate(&self, extra: &[(usize, usize, u16)], width: usize) -> Vec<SchedPick> {
        let n = self.nodes.len();
        let mut succs: Vec<Vec<(usize, u16)>> = self
            .nodes
            .iter()
            .map(|x| x.succs.iter().map(|e| (e.to, e.latency)).collect())
            .collect();
        for &(a, b, latency) in extra {
            match succs[a].iter_mut().find(|e| e.0 == b) {
                Some(e) => e.1 = e.1.max(latency),
                None => succs[a].push((b, latency)),
            }
        }
        // (the successors are later instructions: a backward pass raises each height over its successors')
        let mut height: Vec<u16> = self.nodes.iter().map(|x| x.height).collect();
        for a in (0..n).rev() {
            for &(b, l) in &succs[a] {
                height[a] = height[a].max(height[b] + l);
            }
        }
        let critical = self.nodes.iter().map(|x| x.height).max().unwrap_or(0);
        let raised = height.iter().copied().max().unwrap_or(0);
        let deadline: Vec<u16> = (0..n)
            .map(|i| {
                (self.nodes[i].deadline + (raised - critical))
                    .saturating_sub(height[i] - self.nodes[i].height)
            })
            .collect();
        let mut preds = vec![0u16; n];
        for list in &succs {
            for &(b, _) in list {
                preds[b] += 1;
            }
        }
        let mut machine = crate::machine::M750::default();
        let mut earliest = vec![0u16; n];
        let mut done = vec![false; n];
        let mut out = vec![];
        let mut cycle = 0u16;
        while out.len() < n && cycle < 10_000 {
            for _ in 0..width.max(1) {
                let uncover = |i: usize| succs[i].iter().filter(|e| preds[e.0] == 1).count();
                let mut best: Option<usize> = None;
                for i in (0..n).filter(|&i| {
                    !done[i]
                        && preds[i] == 0
                        && earliest[i] <= cycle
                        && machine.can_issue(&self.nodes, i)
                }) {
                    let Some(b) = best else {
                        best = Some(i);
                        continue;
                    };
                    let priority = |x: usize| Priority {
                        deadline: deadline[x].into(),
                        uncovers: uncover(x) as i64,
                        height: height[x].into(),
                        opcode_rank: self.nodes[x].opcode_rank.into(),
                    };
                    if pick_rule(priority(b), priority(i), cycle.into(), self.pre_ra).0 {
                        best = Some(i);
                    }
                }
                let Some(x) = best else { break };
                done[x] = true;
                for &(b, l) in &succs[x] {
                    preds[b] -= 1;
                    earliest[b] = earliest[b].max(cycle + l);
                }
                machine.issue(&self.nodes, x);
                out.push(SchedPick { cycle, node: x });
            }
            machine.advance_clock(&self.nodes);
            cycle += 1;
        }
        out
    }

    /// Whether scheduling the block again (no edge added) gives the recorded order: the condition for trusting
    /// [`simulate`](Self::simulate)'s what-ifs on it.
    pub fn simulation_agrees(&self) -> bool {
        self.simulate(&[], self.issue_width())
            .iter()
            .map(|p| p.node)
            .eq(self.order())
    }

    /// The memory accesses the scheduler asked about and was told cannot alias, and that nothing else orders:
    /// what a may-alias answer would have added, and whether it would have changed the schedule.
    pub fn unordered_memory(&self) -> Vec<UnorderedAccesses> {
        if self.memory.is_empty() {
            return vec![];
        }
        let cycle_of = |i: usize| self.picks.iter().find(|p| p.node == i).map(|p| p.cycle);
        // (scheduling the block again only when an access pair needs it)
        let agrees_once = std::cell::OnceCell::new();
        let mut out: Vec<UnorderedAccesses> = vec![];
        for q in &self.memory {
            if q.may
                || q.first >= q.second
                || out
                    .iter()
                    .any(|u| u.first == q.first && u.second == q.second)
                || self.dependence_path(q.first, q.second).is_some()
            {
                continue;
            }
            let (Some(first_cycle), Some(second_cycle)) = (cycle_of(q.first), cycle_of(q.second))
            else {
                continue;
            };
            let latency = self.nodes[q.first].latency;
            let binding = second_cycle < first_cycle + latency;
            let agrees = *agrees_once.get_or_init(|| self.simulation_agrees());
            let with_edge = (binding && agrees)
                .then(|| self.simulate(&[(q.first, q.second, latency)], self.issue_width()));
            out.push(UnorderedAccesses {
                reorders: agrees.then(|| {
                    with_edge
                        .as_ref()
                        .is_some_and(|picks| !picks.iter().map(|p| p.node).eq(self.order()))
                }),
                first: q.first,
                second: q.second,
                first_cycle,
                second_cycle,
                latency,
                binding,
                with_edge,
            });
        }
        out
    }

    /// Node indices in issue order.
    pub fn order(&self) -> Vec<usize> {
        self.picks.iter().map(|p| p.node).collect()
    }

    /// First node whose text contains `pat`.
    pub fn find(&self, pat: &str) -> Option<usize> {
        self.nodes.iter().position(|n| n.text.contains(pat))
    }

    /// The pick rule's comparison: does `cand` replace `best` at `cycle`?
    fn beats(
        &self,
        cand: usize,
        best: usize,
        cycle: u16,
        uncover: &dyn Fn(usize) -> usize,
    ) -> Option<PickReason> {
        self.beats_with(cand, best, cycle, uncover, self.pre_ra)
    }

    /// [`beats`](Self::beats) with the opcode-rank tie-break on or off (`rank`), for testing rule
    /// variants against the recorded picks.
    fn beats_with(
        &self,
        cand: usize,
        best: usize,
        cycle: u16,
        uncover: &dyn Fn(usize) -> usize,
        rank: bool,
    ) -> Option<PickReason> {
        let priority = |x: usize| Priority {
            deadline: self.nodes[x].deadline.into(),
            uncovers: uncover(x) as i64,
            height: self.nodes[x].height.into(),
            opcode_rank: self.nodes[x].opcode_rank.into(),
        };
        match pick_rule(priority(best), priority(cand), cycle.into(), rank) {
            (false, _) | (true, Criterion::Tie) => None,
            (true, Criterion::Urgent) => Some(PickReason::Urgent),
            (true, Criterion::Uncovers) => Some(PickReason::Uncovers),
            (true, Criterion::Height) => Some(PickReason::Height),
            (true, Criterion::OpcodeRank) => Some(PickReason::OpcodeRank),
        }
    }

    /// Evidence for a variant of the pick rule (opcode-rank tie-break on or off): replays the
    /// recorded picks and counts, for every pick with another ready node, whether the variant
    /// chooses the recorded node (`agree`), or a node that was issued later in the same cycle
    /// (`contradict`: the unit model accepted it, so the variant would have issued it first), or
    /// another node (`other`: unit availability may explain it).
    pub fn rule_evidence(&self, rank: bool) -> RuleEvidence {
        let mut replay = Replay::new(&self.nodes);
        let mut ev = RuleEvidence::default();
        for (k, pk) in self.picks.iter().enumerate() {
            let cycle = pk.cycle;
            let ready = replay.ready(cycle);
            let uncover = |i: usize| replay.uncover(i);
            if ready.len() > 1 && ready.contains(&pk.node) {
                let mut best = ready[0];
                for &r in &ready[1..] {
                    if self.beats_with(r, best, cycle, &uncover, rank).is_some() {
                        best = r;
                    }
                }
                if best == pk.node {
                    ev.agree += 1;
                } else if self.picks[k + 1..]
                    .iter()
                    .take_while(|p| p.cycle == cycle)
                    .any(|p| p.node == best)
                {
                    ev.contradict += 1;
                } else {
                    ev.other += 1;
                }
            }
            replay.issue(pk.node, cycle);
        }
        ev
    }

    /// Replay the recorded picks with the exact rule and give each pick its reason.
    pub fn analyze(&self) -> Vec<PickInfo> {
        let mut replay = Replay::new(&self.nodes);
        let mut out = vec![];
        for pk in &self.picks {
            let cycle = pk.cycle;
            let ready = replay.ready(cycle);
            let uncover = |i: usize| replay.uncover(i);
            // the rule over all ready nodes (unit availability unknown)
            let mut best: Option<usize> = None;
            let mut best_reason = PickReason::ProgramOrder;
            for &r in &ready {
                match best {
                    None => best = Some(r),
                    Some(b) => {
                        if let Some(why) = self.beats(r, b, cycle, &uncover) {
                            best = Some(r);
                            best_reason = why;
                        }
                    }
                }
            }
            let x = pk.node;
            let (reason, over) = if !ready.contains(&x) {
                (PickReason::Unknown, None)
            } else if ready.len() == 1 {
                (PickReason::OnlyReady, None)
            } else if best == Some(x) {
                if ready[0] == x {
                    (PickReason::ProgramOrder, None)
                } else {
                    // the reason x beat the first ready node
                    (
                        self.beats(x, ready[0], cycle, &uncover)
                            .unwrap_or(best_reason),
                        Some(ready[0]),
                    )
                }
            } else {
                (PickReason::UnitBusy, best)
            };
            out.push(PickInfo {
                cycle,
                node: x,
                ready,
                reason,
                over,
            });
            replay.issue(x, cycle);
        }
        out
    }

    /// Edge kinds along a dependence path from `a` to `b` (None if `b` does not depend on `a`).
    pub fn dependence_path(&self, a: usize, b: usize) -> Option<Vec<EdgeKind>> {
        let n = self.nodes.len();
        let mut prev: Vec<Option<(usize, EdgeKind)>> = vec![None; n];
        let mut seen = vec![false; n];
        let mut q = std::collections::VecDeque::from([a]);
        seen[a] = true;
        while let Some(x) = q.pop_front() {
            if x == b {
                let mut path = vec![];
                let mut cur = b;
                while let Some((p, k)) = prev[cur] {
                    path.push(k);
                    cur = p;
                }
                path.reverse();
                return Some(path);
            }
            for e in &self.nodes[x].succs {
                if !seen[e.to] {
                    seen[e.to] = true;
                    prev[e.to] = Some((x, e.kind));
                    q.push_back(e.to);
                }
            }
        }
        None
    }

    /// Why was node `first` issued before node `second`?
    pub fn why_before(&self, first: usize, second: usize) -> Explanation {
        let t = |i: usize| self.nodes[i].text.clone();
        let mk = |why: WhyKind, m: bool, text: String| Explanation {
            first,
            second,
            why,
            statement_order_matters: m,
            text,
        };
        let order = self.order();
        let (pf, ps) = (
            order.iter().position(|&x| x == first),
            order.iter().position(|&x| x == second),
        );
        if let (Some(pf), Some(ps)) = (pf, ps)
            && pf > ps
        {
            return mk(
                WhyKind::NotBefore,
                false,
                format!("[{}] is issued after [{}]", t(first), t(second)),
            );
        }
        if let Some(path) = self.dependence_path(first, second) {
            return mk(
                WhyKind::Dependence(path.clone()),
                false,
                format!(
                    "[{}] depends on [{}] ({path:?}): no statement order can swap them",
                    t(second),
                    t(first)
                ),
            );
        }
        let info = self.analyze();
        let Some(pi) = info.iter().find(|p| p.node == first) else {
            return mk(WhyKind::NotBefore, false, "not recorded".into());
        };
        if !pi.ready.contains(&second) {
            // which predecessor of `second` was still pending, or its latency
            let issued_before: Vec<usize> = info
                .iter()
                .take_while(|p| p.node != first)
                .map(|p| p.node)
                .collect();
            let preds: Vec<usize> = (0..self.nodes.len())
                .filter(|&a| self.nodes[a].succs.iter().any(|e| e.to == second))
                .collect();
            let pending = preds.iter().copied().find(|p| !issued_before.contains(p));
            // the predecessor whose result arrives last (latency)
            let (ready_at, slowest) = preds
                .iter()
                .filter_map(|&p| {
                    let c = info.iter().find(|q| q.node == p)?.cycle;
                    Some((
                        c + self.nodes[p].succs.iter().find(|e| e.to == second)?.latency,
                        p,
                    ))
                })
                .max()
                .map_or((0, None), |(r, p)| (r, Some(p)));
            let text = match (pending, slowest) {
                (Some(p), _) => format!(
                    "[{}] was not ready at cycle {}: its predecessor [{}] had not issued yet",
                    t(second),
                    pi.cycle,
                    t(p)
                ),
                (None, Some(p)) => format!(
                    "[{}] was not ready at cycle {}: waiting for the result of [{}] until cycle {ready_at}",
                    t(second),
                    pi.cycle,
                    t(p)
                ),
                (None, None) => format!("[{}] was not ready at cycle {}", t(second), pi.cycle),
            };
            // moving the source statement of the predecessor it waits for can help
            let w = pending.or(slowest);
            return mk(
                WhyKind::NotReady {
                    waiting_for: w,
                    ready_at,
                },
                w.is_some(),
                text,
            );
        }
        let reason = match pi.reason {
            PickReason::ProgramOrder | PickReason::OnlyReady => {
                // both ready, `first` earlier in program order and nothing beat it
                PickReason::ProgramOrder
            }
            r => r,
        };
        let matters = reason == PickReason::ProgramOrder;
        let text = match reason {
            PickReason::ProgramOrder => format!(
                "both ready at cycle {}; equal priority, [{}] comes first in program order: swap the source statements",
                pi.cycle,
                t(first)
            ),
            PickReason::UnitBusy => format!(
                "cycle {}: the preferred instruction could not issue (unit busy)",
                pi.cycle
            ),
            r => format!(
                "both ready at cycle {}; [{}] wins by {r:?}",
                pi.cycle,
                t(first)
            ),
        };
        mk(WhyKind::Priority(reason), matters, text)
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    /// Echoes' CKnockBackMgr::GetConditionValue, block 66 before the allocation (`CRuleValue(mCharacterState ==
    /// kCS_Alive)` in a const method): its DAG and picks as GC 2.7 recorded them, and the scheduler's alias
    /// queries. The load through `this` and the store to the result cannot alias.
    fn block66() -> SchedBlock {
        // (text, op, flags, GPRs, first operand's GPR written, latency, height, deadline, successors)
        type Row = (
            &'static str,
            i16,
            u32,
            &'static [i16],
            Option<i16>,
            u16,
            u16,
            u16,
            &'static [(usize, u16)],
        );
        let rows: [Row; 8] = [
            (
                "lwz r131=, r33, #88",
                34,
                0x42,
                &[131, 33],
                Some(131),
                2,
                6,
                0,
                &[(7, 0), (1, 2)],
            ),
            (
                "cntlzw r132=, r131",
                102,
                0x8800_4000,
                &[132, 131],
                Some(132),
                1,
                4,
                2,
                &[(7, 0), (5, 1), (2, 1)],
            ),
            (
                "rlwinm r66=, r132, #27, #5, #31",
                103,
                0x8800_4000,
                &[66, 132],
                Some(66),
                1,
                1,
                5,
                &[(7, 0)],
            ),
            (
                "li r133=, #0",
                137,
                0x8000_4000,
                &[133],
                Some(133),
                1,
                5,
                1,
                &[(7, 0), (4, 1)],
            ),
            (
                "stw r133, r32, #0",
                49,
                0x24,
                &[133, 32],
                None,
                2,
                4,
                2,
                &[(7, 0), (6, 2)],
            ),
            (
                "rlwinm r134=, r132, #27, #24, #31",
                103,
                0x8800_4000,
                &[134, 132],
                Some(134),
                1,
                3,
                3,
                &[(7, 0), (6, 1)],
            ),
            (
                "stb r134, r32, #4",
                40,
                0x24,
                &[134, 32],
                None,
                2,
                2,
                4,
                &[(7, 0)],
            ),
            ("b", 0, 0x2200_0001, &[], None, 0, 0, 6, &[]),
        ];
        let nodes = rows
            .iter()
            .map(
                |&(text, op, flags, gprs, first_def, latency, height, deadline, succs)| SchedNode {
                    text: text.into(),
                    opcode_rank: 0,
                    latency,
                    height,
                    deadline,
                    preds: 0,
                    succs: succs
                        .iter()
                        .map(|&(to, latency)| SchedEdge {
                            to,
                            latency,
                            kind: if to == 7 {
                                EdgeKind::Order
                            } else {
                                EdgeKind::Data
                            },
                        })
                        .collect(),
                    alias: None,
                    op,
                    flags,
                    gprs: gprs.to_vec(),
                    first_def,
                    raw_succs: vec![],
                },
            )
            .collect();
        SchedBlock {
            function: "GetConditionValue".into(),
            pre_ra: true,
            block: 66,
            nodes,
            picks: [
                (0, 0),
                (0, 3),
                (1, 4),
                (2, 1),
                (3, 5),
                (4, 6),
                (4, 2),
                (5, 7),
            ]
            .map(|(cycle, node)| SchedPick { cycle, node })
            .into(),
            aliases: vec![],
            memory: [(0, 4, false), (0, 6, false), (4, 6, true)]
                .map(|(first, second, may)| MemoryQuery { first, second, may })
                .into(),
        }
    }

    #[test]
    fn the_750_model_replays_the_recorded_picks() {
        let block = block66();
        assert_eq!(block.issue_width(), 2);
        let picks: Vec<_> = block
            .simulate(&[], 2)
            .iter()
            .map(|p| (p.cycle, p.node))
            .collect();
        let recorded: Vec<_> = block.picks.iter().map(|p| (p.cycle, p.node)).collect();
        assert_eq!(picks, recorded);
    }

    #[test]
    fn a_may_alias_answer_keeps_the_store_after_the_compare() {
        let pairs = block66().unordered_memory();
        // (the store of mType is the one that moved: the stb waits on the stw anyway)
        let moved: Vec<_> = pairs.iter().filter(|p| p.reorders == Some(true)).collect();
        assert_eq!(moved.len(), 1);
        let p = moved[0];
        assert_eq!((p.first, p.second, p.latency, p.second_cycle), (0, 4, 2, 1));
        // lwz, li, cntlzw, stw, ...: the order GC 2.7 gives when `this` is not read-only (the retail order)
        let order: Vec<_> = p
            .with_edge
            .as_ref()
            .unwrap()
            .iter()
            .map(|q| q.node)
            .collect();
        assert_eq!(order, [0, 3, 1, 4, 5, 6, 2, 7]);
    }
}
