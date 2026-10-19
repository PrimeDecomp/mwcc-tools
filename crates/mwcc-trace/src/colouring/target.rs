//! Instruction and target alignment used by colouring hypotheses.
use super::final_colours;
use crate::{
    ppc, sequence,
    trace::{Allocation, Instruction, Snapshot},
    version::class_name,
};
use anyhow::Result;
use serde::Serialize;
use std::collections::{BTreeMap, BTreeSet};

pub type Field = (&'static str, char, u32, &'static str);
pub fn field_shift(field: char) -> u32 {
    match field {
        'D' => 21,
        'A' => 16,
        'B' => 11,
        'C' => 6,
        _ => unreachable!("internal register field"),
    }
}
struct Decode {
    word: u32,
    fields: Vec<Field>,
}
impl Decode {
    fn add(&mut self, class: &'static str, field: char, role: &'static str) {
        self.fields
            .push((class, field, (self.word >> field_shift(field)) & 31, role));
    }
    fn base(&mut self, update: bool) {
        if (self.word >> 16) & 31 != 0 || update {
            self.add("GPR", 'A', if update { "du" } else { "u" });
        }
    }
    fn float(&mut self, fields: &str) {
        self.add("FPR", 'D', "d");
        for field in fields.chars() {
            self.add("FPR", field, "u");
        }
    }
}
fn a_form(x: u32) -> Option<&'static str> {
    match x {
        18 | 20 | 21 => Some("AB"),
        22 | 24 | 26 => Some("B"),
        23 | 28..=31 | 10 | 11 | 14 | 15 => Some("ACB"),
        25 | 12 | 13 => Some("AC"),
        _ => None,
    }
}
pub fn register_fields(word: u32) -> Option<Vec<Field>> {
    let op = word >> 26;
    let mut d = Decode {
        word,
        fields: vec![],
    };
    match op {
        7 | 8 | 12 | 13 => {
            d.add("GPR", 'D', "d");
            d.add("GPR", 'A', "u");
        }
        14 | 15 => {
            d.add("GPR", 'D', "d");
            d.base(false);
        }
        10 | 11 => d.add("GPR", 'A', "u"),
        24..=29 | 21 => {
            d.add("GPR", 'D', "u");
            d.add("GPR", 'A', "d");
        }
        20 => {
            d.add("GPR", 'D', "u");
            d.add("GPR", 'A', "du");
        }
        23 => {
            d.add("GPR", 'D', "u");
            d.add("GPR", 'B', "u");
            d.add("GPR", 'A', "d");
        }
        32..=45 | 48..=57 | 60 | 61 => {
            let float = matches!(op, 48..=57 | 60 | 61);
            let load = matches!(
                op,
                32 | 33 | 34 | 35 | 40 | 41 | 42 | 43 | 48 | 49 | 50 | 51 | 56 | 57
            );
            d.add(
                if float { "FPR" } else { "GPR" },
                'D',
                if load { "d" } else { "u" },
            );
            d.base(matches!(
                op,
                33 | 35 | 37 | 39 | 41 | 43 | 45 | 49 | 51 | 53 | 55 | 57 | 61
            ));
        }
        31 => {
            let xo = (word >> 1) & 0x3ff;
            let xo9 = (word >> 1) & 0x1ff;
            if matches!(
                xo9,
                266 | 10
                    | 138
                    | 234
                    | 202
                    | 40
                    | 8
                    | 136
                    | 232
                    | 200
                    | 104
                    | 235
                    | 75
                    | 11
                    | 491
                    | 459
            ) {
                d.add("GPR", 'D', "d");
                d.add("GPR", 'A', "u");
                if !matches!(xo9, 234 | 202 | 232 | 200 | 104) {
                    d.add("GPR", 'B', "u");
                }
            } else if matches!(
                xo,
                28 | 60 | 444 | 412 | 316 | 476 | 124 | 284 | 24 | 536 | 792 | 824 | 26 | 922 | 954
            ) {
                d.add("GPR", 'D', "u");
                d.add("GPR", 'A', "d");
                if !matches!(xo, 824 | 26 | 922 | 954) {
                    d.add("GPR", 'B', "u");
                }
            } else if matches!(xo, 0 | 32) {
                d.add("GPR", 'A', "u");
                d.add("GPR", 'B', "u");
            } else if matches!(
                xo,
                23 | 55
                    | 87
                    | 119
                    | 279
                    | 311
                    | 343
                    | 375
                    | 534
                    | 790
                    | 151
                    | 183
                    | 215
                    | 247
                    | 407
                    | 439
                    | 662
                    | 918
                    | 535
                    | 567
                    | 599
                    | 631
                    | 663
                    | 695
                    | 727
                    | 759
                    | 983
            ) {
                let float = matches!(xo, 535 | 567 | 599 | 631 | 663 | 695 | 727 | 759 | 983);
                let load = matches!(
                    xo,
                    23 | 55 | 87 | 119 | 279 | 311 | 343 | 375 | 534 | 790 | 535 | 567 | 599 | 631
                );
                d.add(
                    if float { "FPR" } else { "GPR" },
                    'D',
                    if load { "d" } else { "u" },
                );
                d.base(matches!(
                    xo,
                    55 | 119 | 311 | 375 | 183 | 247 | 439 | 567 | 631 | 695 | 759
                ));
                d.add("GPR", 'B', "u");
            } else if matches!(xo, 86 | 54 | 278 | 246 | 1014 | 470 | 982) {
                d.base(false);
                d.add("GPR", 'B', "u");
            } else if matches!(xo, 339 | 19 | 83) {
                d.add("GPR", 'D', "d");
            } else if matches!(xo, 467 | 144 | 146) {
                d.add("GPR", 'D', "u");
            } else {
                return None;
            }
        }
        59 | 63 => {
            let xo5 = (word >> 1) & 31;
            let xo = (word >> 1) & 0x3ff;
            if op == 63 && matches!(xo, 0 | 32) {
                d.add("FPR", 'A', "u");
                d.add("FPR", 'B', "u");
            } else if xo5 >= 18
                && let Some(fields) = a_form(xo5)
            {
                d.float(fields);
            } else if op == 63 && matches!(xo, 72 | 40 | 264 | 136 | 12 | 14 | 15) {
                d.float("B");
            } else {
                return None;
            }
        }
        4 => {
            let xo6 = (word >> 1) & 63;
            let xo5 = (word >> 1) & 31;
            let xo = (word >> 1) & 0x3ff;
            if matches!(xo6, 6 | 38 | 7 | 39) {
                d.add("FPR", 'D', if matches!(xo6, 6 | 38) { "d" } else { "u" });
                d.base(matches!(xo6, 38 | 39));
                d.add("GPR", 'B', "u");
            } else if matches!(xo, 40 | 72 | 136 | 264 | 528 | 560 | 592 | 624) {
                d.float(if matches!(xo, 40 | 72 | 136 | 264) {
                    "B"
                } else {
                    "AB"
                });
            } else if matches!(xo, 0 | 32 | 64 | 96) {
                d.add("FPR", 'A', "u");
                d.add("FPR", 'B', "u");
            } else if let Some(fields) = a_form(xo5) {
                d.float(fields);
            } else {
                return None;
            }
        }
        _ => return None,
    }
    Some(d.fields)
}
pub fn shape(mut word: u32) -> u32 {
    if let Some(fields) = register_fields(word) {
        for (_, field, _, _) in fields {
            word &= !(31 << field_shift(field));
        }
    }
    ppc::instruction_token(word)
}

#[derive(Debug)]
struct Operand {
    class: String,
    physical: u32,
    virtual_: u32,
    role: u8,
}
fn pcode_stream(allocation: &Allocation) -> Vec<(&Instruction, Vec<Operand>)> {
    let before: BTreeMap<_, _> = allocation
        .before
        .blocks
        .iter()
        .flat_map(|b| &b.instructions)
        .map(|i| (&i.pointer, i))
        .collect();
    let mut stream = vec![];
    for ins in allocation.after.blocks.iter().flat_map(|b| &b.instructions) {
        let registers: Vec<_> = ins.operands.iter().filter(|o| o.is_register()).collect();
        if registers.len() > 4
            || !registers
                .iter()
                .any(|o| matches!(o.class().as_str(), "GPR" | "FPR"))
        {
            continue;
        }
        let virtuals: Vec<_> = before.get(&ins.pointer).map_or_else(
            || vec![None; registers.len()],
            |original| {
                original
                    .operands
                    .iter()
                    .filter(|o| o.is_register())
                    .map(Some)
                    .collect()
            },
        );
        let mut ops = vec![];
        for (o, v) in registers.into_iter().zip(virtuals) {
            let class = o.class();
            if !matches!(class.as_str(), "GPR" | "FPR") {
                continue;
            }
            let role = u8::from(o.defines()) * 2 + u8::from(o.uses());
            ops.push(Operand {
                physical: o.register(),
                virtual_: v
                    .filter(|v| v.class() == class)
                    .map_or(o.register(), |v| v.register()),
                class,
                role: if role == 0 { 1 } else { role },
            });
        }
        stream.push((ins, ops));
    }
    stream
}
type Key = (String, u32);
type Counter = Vec<(u32, usize)>;
type Votes = BTreeMap<Key, Counter>;
fn vote(votes: &mut Votes, key: Key, target: u32) {
    let counter = votes.entry(key).or_default();
    if let Some((_, count)) = counter.iter_mut().find(|(v, _)| *v == target) {
        *count += 1;
    } else {
        counter.push((target, 1));
    }
}
fn counter_value(counter: &Counter) -> serde_json::Value {
    let mut out = serde_json::Map::new();
    for (v, n) in counter {
        out.insert(v.to_string(), serde_json::json!(n));
    }
    out.into()
}

#[derive(Clone, Debug, Default, Serialize)]
pub struct Stats {
    pub words: usize,
    pub target_words: usize,
    pub paired_with_pcode: usize,
    pub paired_with_target: usize,
}
pub fn target_votes(
    allocation: &Allocation,
    ours: &[u32],
    theirs: &[u32],
) -> (Votes, Stats, BTreeMap<Key, u32>) {
    let stream = pcode_stream(allocation);
    let fields: Vec<_> = ours.iter().map(|w| register_fields(*w)).collect();
    let a: Vec<_> = fields
        .iter()
        .map(|f| {
            f.as_ref().filter(|f| !f.is_empty()).map(|f| {
                f.iter()
                    .map(|(c, _, r, _)| (c.to_string(), *r))
                    .collect::<BTreeSet<_>>()
                    .into_iter()
                    .collect::<Vec<_>>()
            })
        })
        .collect();
    let b: Vec<_> = stream
        .iter()
        .map(|(_, ops)| {
            Some(
                ops.iter()
                    .map(|o| (o.class.clone(), o.physical))
                    .collect::<BTreeSet<_>>()
                    .into_iter()
                    .collect::<Vec<_>>(),
            )
        })
        .collect();
    let mut to_pcode = BTreeMap::new();
    for (x, y, size) in sequence::matching_blocks_with_autojunk(&a, &b, false) {
        for k in 0..size {
            if a[x + k].is_some() {
                to_pcode.insert(x + k, y + k);
            }
        }
    }
    let mut taken: BTreeSet<_> = to_pcode.values().copied().collect();
    let paired: Vec<_> = to_pcode.iter().map(|(x, y)| (*x, *y)).collect();
    for (x, fields) in a.iter().enumerate() {
        if fields.is_none() || to_pcode.contains_key(&x) {
            continue;
        }
        let at = paired.partition_point(|(px, _)| *px < x);
        let expected = if at > 0 {
            paired[at - 1].1 as i64 + (x - paired[at - 1].0) as i64
        } else {
            x as i64
        };
        let candidates = (0.max(expected - 48) as usize
            ..b.len().min((expected + 49).max(0) as usize))
            .filter(|y| !taken.contains(y) && b[*y] == *fields);
        if let Some(y) = candidates.min_by_key(|y| (*y as i64 - expected).unsigned_abs()) {
            to_pcode.insert(x, y);
            taken.insert(y);
        }
    }
    let mut to_target = BTreeMap::new();
    let shapes_a: Vec<_> = ours.iter().map(|w| shape(*w)).collect();
    let shapes_b: Vec<_> = theirs.iter().map(|w| shape(*w)).collect();
    for (x, y, size) in sequence::matching_blocks_with_autojunk(&shapes_a, &shapes_b, false) {
        for k in 0..size {
            to_target.insert(x + k, y + k);
        }
    }
    let mut votes = BTreeMap::new();
    let mut held = BTreeMap::new();
    for (index, fields) in fields.iter().enumerate() {
        let Some(fields) = fields.as_ref().filter(|f| !f.is_empty()) else {
            continue;
        };
        let (Some(pcode), Some(target)) = (to_pcode.get(&index), to_target.get(&index)) else {
            continue;
        };
        let Some(their_fields) =
            register_fields(theirs[*target]).filter(|f| f.len() == fields.len())
        else {
            continue;
        };
        let ops = &stream[*pcode].1;
        for ((class, _, reg, role), (_, _, target, _)) in fields.iter().zip(their_fields) {
            let mask = u8::from(role.contains('d')) * 2 + u8::from(role.contains('u'));
            let holders: BTreeSet<_> = ops
                .iter()
                .filter(|o| o.class == *class && o.physical == *reg && o.role & mask != 0)
                .map(|o| o.virtual_)
                .collect();
            if holders.len() == 1 {
                let virtual_ = *holders.first().unwrap();
                if virtual_ >= 32 {
                    let key = (class.to_string(), virtual_);
                    vote(&mut votes, key.clone(), target);
                    held.insert(key, *reg);
                }
            }
        }
    }
    (
        votes,
        Stats {
            words: ours.len(),
            target_words: theirs.len(),
            paired_with_pcode: to_pcode.len(),
            paired_with_target: to_target.len(),
        },
        held,
    )
}

#[derive(Clone, Debug, Default, Serialize)]
pub struct Derived {
    pub targets: BTreeMap<u32, i32>,
    pub votes: BTreeMap<u32, serde_json::Value>,
    pub split: BTreeMap<u32, serde_json::Value>,
    pub conflicts: Vec<(u32, u32)>,
    pub renumbered: Vec<u32>,
    pub stats: serde_json::Value,
}
pub fn derive(
    allocation: &Allocation,
    ours: &[u32],
    theirs: &[u32],
    class: &str,
    wants: &BTreeMap<u32, i32>,
) -> Result<Derived> {
    let (votes, stats, held) = target_votes(allocation, ours, theirs);
    let mut out = Derived {
        stats: serde_json::to_value(stats)?,
        ..Default::default()
    };
    for ((c, v), counter) in votes {
        if c != class {
            continue;
        }
        let mut best = counter[0];
        for entry in &counter {
            if entry.1 > best.1 {
                best = *entry;
            }
        }
        out.targets.insert(v, best.0 as i32);
        if counter.len() > 1 {
            out.split.insert(v, counter_value(&counter));
        }
        out.votes.insert(v, counter_value(&counter));
    }
    out.targets.extend(wants);
    if let Some(round) = allocation
        .before
        .rounds
        .iter()
        .rev()
        .find(|r| class_name(r.class) == class)
    {
        let mut colours: BTreeMap<_, _> = round
            .order
            .iter()
            .filter_map(|(i, _)| round.colors.get(i).filter(|c| c.0 >= 0).map(|c| (*i, c.0)))
            .collect();
        final_colours(round, &mut colours);
        out.renumbered = held
            .into_iter()
            .filter(|((c, v), p)| c == class && colours.get(v).is_some_and(|c| *c != *p as i32))
            .map(|((_, v), _)| v)
            .collect();
        out.renumbered.sort();
        for (i, node) in &round.nodes {
            if !out.targets.contains_key(i) || node.flags & 4 != 0 {
                continue;
            }
            for n in &node.neighbors {
                if (*n < 32 && out.targets[i] == *n as i32)
                    || (*n > *i
                        && out.targets.get(n) == out.targets.get(i)
                        && round.nodes.get(n).is_none_or(|n| n.flags & 4 == 0))
                {
                    out.conflicts.push((*i, *n));
                }
            }
        }
    }
    Ok(out)
}

pub struct Remapped {
    /// All instruction-operand votes, including losing and colliding candidates.
    pub votes: BTreeMap<(String, u32), Vec<(u32, usize)>>,
    pub mapping: BTreeMap<(String, u32), u32>,
    pub unsure: BTreeMap<(String, u32), serde_json::Value>,
    pub paired: usize,
    pub instructions: (usize, usize),
}
pub fn remap(
    first: &Allocation,
    second: &Allocation,
    version: crate::version::Version,
) -> Remapped {
    remap_snapshots(&first.before, &second.before, version)
}

pub fn remap_snapshots(
    first: &Snapshot,
    second: &Snapshot,
    version: crate::version::Version,
) -> Remapped {
    use regex::Regex;
    use std::sync::LazyLock;
    static VIRTUAL: LazyLock<Regex> = LazyLock::new(|| Regex::new(r"%(cr|[rfv])\d+").unwrap());
    let names = crate::version::opcode_names(version);
    let a: Vec<_> = first.blocks.iter().flat_map(|b| &b.instructions).collect();
    let b: Vec<_> = second.blocks.iter().flat_map(|b| &b.instructions).collect();
    let aa: Vec<_> = a
        .iter()
        .map(|i| VIRTUAL.replace_all(&i.text(names), "%$1").into_owned())
        .collect();
    let bb: Vec<_> = b
        .iter()
        .map(|i| VIRTUAL.replace_all(&i.text(names), "%$1").into_owned())
        .collect();
    let mut votes = BTreeMap::new();
    let mut paired = 0;
    for (x, y, size) in sequence::matching_blocks_with_autojunk(&aa, &bb, false) {
        for k in 0..size {
            paired += 1;
            for (p, q) in a[x + k].operands.iter().zip(&b[y + k].operands) {
                if p.is_register()
                    && q.is_register()
                    && p.class() == q.class()
                    && p.register() >= 32
                {
                    vote(&mut votes, (p.class(), p.register()), q.register());
                }
            }
        }
    }
    let mut mapping = BTreeMap::new();
    let mut unsure = BTreeMap::new();
    for (key, counter) in &votes {
        let mut best = counter[0];
        for entry in counter {
            if entry.1 > best.1 {
                best = *entry;
            }
        }
        mapping.insert(key.clone(), best.0);
        if counter.len() > 1 {
            unsure.insert(key.clone(), counter_value(counter));
        }
    }
    Remapped {
        votes,
        mapping,
        unsure,
        paired,
        instructions: (a.len(), b.len()),
    }
}

#[derive(Clone, Debug, Serialize)]
pub struct GraphDifference {
    pub moved: Vec<u32>,
    pub only_first: Vec<u32>,
    pub only_second: Vec<u32>,
    pub edges_added: BTreeSet<(u32, u32)>,
    pub edges_removed: BTreeSet<(u32, u32)>,
}
/// Historical mapped-subgraph projection for captured reference reports. This
/// omits unmatched boundary edges and instruction-invisible nodes; production
/// source objectives must use `correspondence::compare`, not infer elimination
/// from this projection's `only_first`/`only_second` fields.
pub fn graph_difference(
    first: &Allocation,
    second: &Allocation,
    mapping: &BTreeMap<(String, u32), u32>,
    class: &str,
) -> Option<GraphDifference> {
    let a = first
        .before
        .rounds
        .iter()
        .rev()
        .find(|r| class_name(r.class) == class)?;
    let b = second
        .before
        .rounds
        .iter()
        .rev()
        .find(|r| class_name(r.class) == class)?;
    let pairs: BTreeMap<_, _> = a
        .nodes
        .keys()
        .filter(|v| **v >= 32)
        .filter_map(|v| {
            mapping
                .get(&(class.into(), *v))
                .filter(|w| b.nodes.contains_key(w))
                .map(|w| (*v, *w))
        })
        .collect();
    let back: BTreeMap<_, _> = pairs.iter().map(|(v, w)| (*w, *v)).collect();
    let mut added = BTreeSet::new();
    let mut removed = BTreeSet::new();
    for (v, w) in &pairs {
        let mine: BTreeSet<_> = a.nodes[v]
            .neighbors
            .iter()
            .filter_map(|n| {
                if *n < 32 {
                    Some(*n)
                } else {
                    pairs.get(n).copied()
                }
            })
            .collect();
        let theirs: BTreeSet<_> = b.nodes[w]
            .neighbors
            .iter()
            .copied()
            .filter(|n| *n < 32 || back.contains_key(n))
            .collect();
        added.extend(
            theirs
                .difference(&mine)
                .map(|n| ((*w).min(*n), (*w).max(*n))),
        );
        removed.extend(
            mine.difference(&theirs)
                .map(|n| ((*w).min(*n), (*w).max(*n))),
        );
    }
    let registers = |a: &Allocation| {
        a.before
            .blocks
            .iter()
            .flat_map(|b| &b.instructions)
            .flat_map(|i| &i.operands)
            .filter(|o| o.is_register() && o.class() == class && o.register() >= 32)
            .map(|o| o.register())
            .collect::<BTreeSet<_>>()
    };
    let mut moved = super::renumbered(&pairs.values().copied().collect::<Vec<_>>());
    moved.sort();
    Some(GraphDifference {
        moved,
        only_first: registers(first)
            .into_iter()
            .filter(|v| a.nodes.contains_key(v) && !pairs.contains_key(v))
            .collect(),
        only_second: registers(second)
            .into_iter()
            .filter(|v| b.nodes.contains_key(v) && !back.contains_key(v))
            .collect(),
        edges_added: added,
        edges_removed: removed,
    })
}

impl Remapped {
    pub fn json(
        &self,
        first: &Allocation,
        second: &Allocation,
        class: &str,
    ) -> Result<serde_json::Value> {
        let mapping: BTreeMap<_, _> = self
            .mapping
            .iter()
            .filter(|((c, _), _)| c == class)
            .map(|((_, v), w)| (*v, *w))
            .collect();
        let changed: BTreeMap<_, _> = mapping
            .iter()
            .filter(|(v, w)| v != w)
            .map(|(v, w)| (*v, *w))
            .collect();
        let unsure: BTreeMap<_, _> = self
            .unsure
            .iter()
            .filter(|((c, _), _)| c == class)
            .map(|((_, v), value)| (*v, value.clone()))
            .collect();
        Ok(
            serde_json::json!({"class":class,"mapping":mapping,"changed":changed,"unsure":unsure,"paired":self.paired,"instructions":self.instructions,"graph":graph_difference(first,second,&self.mapping,class)}),
        )
    }
}
