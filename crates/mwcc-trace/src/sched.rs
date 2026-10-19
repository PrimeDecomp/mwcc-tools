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
    #[serde(skip)]
    pub(crate) raw_succs: Vec<(usize, u16)>,
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
        let (b, c) = (&self.nodes[best], &self.nodes[cand]);
        let b_urgent = b.deadline <= cycle;
        let c_urgent = c.deadline <= cycle;
        if b_urgent && !c_urgent {
            return None;
        }
        if !b_urgent && c_urgent {
            return Some(PickReason::Urgent);
        }
        let (ub, uc) = (uncover(best), uncover(cand));
        if uc != ub {
            return if uc > ub {
                Some(PickReason::Uncovers)
            } else {
                None
            };
        }
        if c.height != b.height {
            return if c.height > b.height {
                Some(PickReason::Height)
            } else {
                None
            };
        }
        if rank && c.opcode_rank < b.opcode_rank {
            return Some(PickReason::OpcodeRank);
        }
        None
    }

    /// Evidence for a variant of the pick rule (opcode-rank tie-break on or off): replays the
    /// recorded picks and counts, for every pick with another ready node, whether the variant
    /// chooses the recorded node (`agree`), or a node that was issued later in the same cycle
    /// (`contradict`: the unit model accepted it, so the variant would have issued it first), or
    /// another node (`other`: unit availability may explain it).
    pub fn rule_evidence(&self, rank: bool) -> RuleEvidence {
        let n = self.nodes.len();
        let mut preds_left: Vec<u16> = vec![0; n];
        for a in &self.nodes {
            for e in &a.succs {
                preds_left[e.to] += 1;
            }
        }
        let mut earliest = vec![0u16; n];
        let mut done = vec![false; n];
        let mut ev = RuleEvidence::default();
        for (k, pk) in self.picks.iter().enumerate() {
            let cycle = pk.cycle;
            let ready: Vec<usize> = (0..n)
                .filter(|&i| !done[i] && preds_left[i] == 0 && earliest[i] <= cycle)
                .collect();
            let uncover = |i: usize| {
                self.nodes[i]
                    .succs
                    .iter()
                    .filter(|e| preds_left[e.to] == 1)
                    .count()
            };
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
            done[pk.node] = true;
            for e in &self.nodes[pk.node].succs {
                preds_left[e.to] = preds_left[e.to].saturating_sub(1);
                earliest[e.to] = earliest[e.to].max(cycle + e.latency);
            }
        }
        ev
    }

    /// Replay the recorded picks with the exact rule and give each pick its reason.
    pub fn analyze(&self) -> Vec<PickInfo> {
        let n = self.nodes.len();
        let mut preds_left: Vec<u16> = vec![0; n];
        for a in &self.nodes {
            for e in &a.succs {
                preds_left[e.to] += 1;
            }
        }
        let mut earliest = vec![0u16; n];
        let mut done = vec![false; n];
        let mut out = vec![];
        for pk in &self.picks {
            let cycle = pk.cycle;
            let ready: Vec<usize> = (0..n)
                .filter(|&i| !done[i] && preds_left[i] == 0 && earliest[i] <= cycle)
                .collect();
            let uncover = |i: usize| {
                self.nodes[i]
                    .succs
                    .iter()
                    .filter(|e| preds_left[e.to] == 1)
                    .count()
            };
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
                ready: ready.clone(),
                reason,
                over,
            });
            done[x] = true;
            for e in &self.nodes[x].succs {
                preds_left[e.to] = preds_left[e.to].saturating_sub(1);
                earliest[e.to] = earliest[e.to].max(cycle + e.latency);
            }
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
