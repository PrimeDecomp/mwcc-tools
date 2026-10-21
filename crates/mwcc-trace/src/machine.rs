//! The PowerPC 750 machine model GC 2.x's list scheduler consults (gc2/src/backend/MachineSimulation750.c's
//! `can_issue`, `issue` and `advance_clock`), over a block's [`SchedNode`]s: which ready instruction can issue
//! this cycle. The timing table is [`crate::m750`].
use crate::m750::{
    BPU, FPU1, FPU2, FPU3, IU1, IU2, LSU1, LSU2, PC_FDIV, PC_FDIVS, SRU, STAGES, TIMING,
};
use crate::sched::SchedNode;

/// The completion queue's entries.
const ENTRIES: usize = 6;
/// `fIsWrite`: the instruction writes memory.
const WRITES_MEMORY: u32 = 0x4;

/// An opcode's first stage and its cycles in its first three stages (an unknown opcode: an integer one).
fn timing(op: i16) -> (usize, [u8; 3]) {
    usize::try_from(op)
        .ok()
        .and_then(|op| TIMING.get(op))
        .map_or((IU2, [1, 0, 0]), |&(stage, cycles)| {
            (stage as usize, cycles)
        })
}

/// `is_dependent(a, b, GPR)`: `b`'s first operand writes a GPR that `a` reads or writes.
fn dependent(nodes: &[SchedNode], a: usize, b: Option<usize>) -> bool {
    b.and_then(|b| nodes[b].first_def)
        .is_some_and(|reg| nodes[a].gprs.contains(&reg))
}

/// The pipeline: each stage's instruction (a node) and the cycles it has left there, the instructions the integer
/// units completed this cycle, and the completion queue.
#[derive(Clone, Debug)]
pub(crate) struct M750 {
    pipeline: [Option<(usize, u8)>; STAGES],
    iu1_completed: Option<usize>,
    iu2_completed: Option<usize>,
    /// (node, completed), in a ring
    entries: [Option<(usize, bool)>; ENTRIES],
    used: usize,
    next_to_retire: usize,
    next_free: usize,
}

impl Default for M750 {
    fn default() -> Self {
        M750 {
            pipeline: [None; STAGES],
            iu1_completed: None,
            iu2_completed: None,
            entries: [None; ENTRIES],
            used: 0,
            next_to_retire: 0,
            next_free: 0,
        }
    }
}

impl M750 {
    pub(crate) fn can_issue(&self, nodes: &[SchedNode], x: usize) -> bool {
        if self.used == ENTRIES {
            return false;
        }
        let (stage, _) = timing(nodes[x].op);
        if stage == IU2 {
            let (clear1, clear2) = (self.pipeline[IU1].is_none(), self.pipeline[IU2].is_none());
            if !clear1 && !clear2 {
                return false;
            }
            if clear1 && clear2 {
                return true;
            }
            let busy = if clear1 { IU2 } else { IU1 };
            if dependent(nodes, x, self.pipeline[busy].map(|p| p.0))
                || dependent(nodes, x, self.iu1_completed)
                || dependent(nodes, x, self.iu2_completed)
            {
                return false;
            }
        } else if self.pipeline[stage].is_some() {
            return false;
        }
        !(nodes[x].flags & WRITES_MEMORY != 0
            && self.pipeline[LSU2].is_some_and(|(i, _)| nodes[i].flags & WRITES_MEMORY != 0))
    }

    pub(crate) fn issue(&mut self, nodes: &[SchedNode], x: usize) {
        let (mut stage, cycles) = timing(nodes[x].op);
        self.used += 1;
        self.entries[self.next_free] = Some((x, false));
        self.next_free = (self.next_free + 1) % ENTRIES;
        if stage == IU2 && self.pipeline[IU1].is_none() {
            stage = IU1;
        }
        self.pipeline[stage] = Some((x, cycles[0]));
    }

    fn complete(&mut self, stage: usize) {
        let Some((x, _)) = self.pipeline[stage].take() else {
            return;
        };
        if let Some(entry) = self.entries.iter_mut().flatten().find(|e| e.0 == x) {
            entry.1 = true;
        }
        if stage == IU1 {
            self.iu1_completed = Some(x);
        } else if stage == IU2 {
            self.iu2_completed = Some(x);
        }
    }

    fn retire(&mut self) -> bool {
        if self.used > 0 && self.entries[self.next_to_retire].is_some_and(|e| e.1) {
            self.entries[self.next_to_retire] = None;
            self.used -= 1;
            self.next_to_retire = (self.next_to_retire + 1) % ENTRIES;
            return true;
        }
        false
    }

    /// Move the instruction in `old` on to `new` (stages of the unit whose first is `first`), when it is done.
    fn advance(&mut self, nodes: &[SchedNode], first: usize, old: usize, new: usize) {
        if let Some((x, 0)) = self.pipeline[old]
            && self.pipeline[new].is_none()
        {
            self.pipeline[new] = Some((x, timing(nodes[x].op).1[new - first]));
            self.pipeline[old] = None;
        }
    }

    pub(crate) fn advance_clock(&mut self, nodes: &[SchedNode]) {
        self.iu1_completed = None;
        self.iu2_completed = None;
        for (_, remaining) in self.pipeline.iter_mut().flatten() {
            *remaining = remaining.saturating_sub(1);
        }
        if self.retire() {
            self.retire();
        }
        for stage in [IU1, LSU2, FPU3, SRU, BPU, IU2] {
            if self.pipeline[stage].is_some_and(|p| p.1 == 0) {
                self.complete(stage);
            }
        }
        if self.pipeline[FPU1]
            .is_some_and(|(x, r)| r == 0 && matches!(nodes[x].op, PC_FDIV | PC_FDIVS))
        {
            self.complete(FPU1);
        }
        self.advance(nodes, FPU1, FPU2, FPU3);
        self.advance(nodes, FPU1, FPU1, FPU2);
        self.advance(nodes, LSU1, LSU1, LSU2);
    }
}
