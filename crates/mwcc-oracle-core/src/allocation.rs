//! Allocation analysis with the schema-6 semantics of tools/oracle.py.
use crate::trace::{Allocation, Instruction, Round, Snapshot};
use crate::version::{Version, class_name, opcode_names};
use serde_json::{Value, json};
use std::collections::{BTreeMap, BTreeSet};

type Register = (String, u32);
type Registers = BTreeSet<Register>;

#[derive(Default)]
struct Liveness {
    across: BTreeMap<Register, Vec<i32>>,
    ranges: BTreeMap<Register, BTreeSet<i32>>,
    counts: BTreeMap<Register, (usize, usize)>,
}

fn register_sets(instruction: &Instruction) -> (Registers, Registers) {
    let mut uses = BTreeSet::new();
    let mut defs = BTreeSet::new();
    for op in &instruction.operands {
        if op.uses() {
            uses.insert((op.class(), op.register()));
        }
        if op.defines() {
            defs.insert((op.class(), op.register()));
        }
    }
    (uses, defs)
}

fn liveness(snapshot: &Snapshot) -> Liveness {
    let mut uses = BTreeMap::new();
    let mut defs = BTreeMap::new();
    let mut live_in: BTreeMap<String, Registers> = BTreeMap::new();
    let mut live_out: BTreeMap<String, Registers> = BTreeMap::new();
    for b in &snapshot.blocks {
        let mut u = Registers::new();
        let mut d = Registers::new();
        for i in &b.instructions {
            let (iu, id) = register_sets(i);
            u.extend(iu.difference(&d).cloned());
            d.extend(id);
        }
        uses.insert(b.pointer.clone(), u);
        defs.insert(b.pointer.clone(), d);
        live_in.insert(b.pointer.clone(), Registers::new());
        live_out.insert(b.pointer.clone(), Registers::new());
    }
    loop {
        let mut changed = false;
        for b in snapshot.blocks.iter().rev() {
            let mut out = Registers::new();
            for s in &b.successors {
                if let Some(r) = live_in.get(s) {
                    out.extend(r.iter().cloned());
                }
            }
            let new_in: Registers = uses[&b.pointer]
                .union(&out.difference(&defs[&b.pointer]).cloned().collect())
                .cloned()
                .collect();
            if out != live_out[&b.pointer] || new_in != live_in[&b.pointer] {
                live_out.insert(b.pointer.clone(), out);
                live_in.insert(b.pointer.clone(), new_in);
                changed = true;
            }
        }
        if !changed {
            break;
        }
    }
    let mut result = Liveness::default();
    for b in &snapshot.blocks {
        let mut live = live_out[&b.pointer].clone();
        for i in b.instructions.iter().rev() {
            let (uses, defs) = register_sets(i);
            // Preserve the reference's count: a read/write operand contributes
            // to uses rather than being counted once in both columns.
            for r in uses.union(&defs) {
                let counts = result.counts.entry(r.clone()).or_default();
                if uses.contains(r) {
                    counts.0 += 1;
                } else {
                    counts.1 += 1;
                }
            }
            if i.operands.iter().any(|o| o.kind() == 3) && defs.len() > 12 {
                for r in live.difference(&defs) {
                    result.across.entry(r.clone()).or_default().push(b.index);
                }
            }
            live = live.difference(&defs).cloned().chain(uses).collect();
            for r in &live {
                result.ranges.entry(r.clone()).or_default().insert(b.index);
            }
        }
    }
    result
}

#[derive(Debug)]
pub struct Simplification {
    /// Push order is significant. Select traverses it backwards.
    pub pushes: Vec<(u32, Value)>,
    pub agrees: bool,
}

struct SimplifyState<'a> {
    round: &'a Round,
    degree: BTreeMap<u32, i64>,
    done: BTreeSet<u32>,
    pushes: Vec<(u32, Value)>,
    unlocked: BTreeMap<u32, u32>,
    pass: usize,
}

impl SimplifyState<'_> {
    fn push(&mut self, i: u32, mut reason: Value) {
        reason["pass"] = json!(self.pass);
        if let Some(by) = self.unlocked.get(&i) {
            reason["unlocked_by"] = json!(by);
        }
        self.pushes.push((i, reason));
        for n in &self.round.nodes[&i].neighbors {
            if let Some(d) = self.degree.get_mut(n) {
                *d -= 1;
                if *d == i64::from(self.round.k) - 1 && !self.done.contains(n) {
                    self.unlocked.insert(*n, i);
                }
            }
        }
        self.done.insert(i);
    }
}

pub fn simplify(round: &Round, register_count: Option<u32>) -> Simplification {
    let mut state = SimplifyState {
        round,
        degree: round
            .nodes
            .iter()
            .map(|(i, n)| (*i, i64::from(n.degree)))
            .collect(),
        done: round
            .nodes
            .iter()
            .filter(|(_, n)| n.flags & 6 != 0)
            .map(|(i, _)| *i)
            .collect(),
        pushes: vec![],
        unlocked: BTreeMap::new(),
        pass: 0,
    };
    let costs: BTreeMap<_, _> = round.order.iter().copied().collect();
    loop {
        let remaining = loop {
            state.pass += 1;
            let mut changed = false;
            let mut remaining = vec![];
            for i in round.nodes.keys() {
                if state.done.contains(i) {
                    continue;
                }
                if state.degree[i] < i64::from(round.k) {
                    state.push(
                        *i,
                        json!({"why": "degree", "degree": state.degree[i], "k": round.k}),
                    );
                    changed = true;
                } else {
                    remaining.push(*i);
                }
            }
            if !changed {
                remaining.reverse();
                break remaining;
            }
        };
        if remaining.is_empty() {
            break;
        }
        let mut best = remaining[0];
        let mut best_cost = f64::INFINITY;
        for i in remaining {
            let c = if register_count.is_some_and(|count| i >= count) || state.degree[&i] == 0 {
                f64::INFINITY
            } else {
                *costs.get(&i).unwrap_or(&0) as f64 / state.degree[&i] as f64
            };
            if c < best_cost {
                best = i;
                best_cost = c;
            }
        }
        state.push(best,json!({"why": "spill candidate", "degree": state.degree[&best], "k": round.k, "cost": costs.get(&best).copied().unwrap_or(0)}));
    }
    let agrees = state
        .pushes
        .iter()
        .rev()
        .map(|(i, _)| i)
        .eq(round.order.iter().map(|(i, _)| i));
    Simplification {
        pushes: state.pushes,
        agrees,
    }
}

pub fn replay_select(round: &Round) -> BTreeMap<u32, Value> {
    let mut mask = round.mask.unwrap_or(0);
    let mut colored = BTreeSet::new();
    let mut out = BTreeMap::new();
    for (position, (i, _)) in round.order.iter().enumerate() {
        let mut taken: BTreeMap<u32, Vec<u32>> = BTreeMap::new();
        if let Some(node) = round.nodes.get(i) {
            for n in &node.neighbors {
                let c = if *n < 32 {
                    *n as i32
                } else if colored.contains(n) {
                    round.colors.get(n).map_or(-1, |c| c.0)
                } else {
                    round.nodes.get(n).map_or(-1, |node| node.color)
                };
                if (0..32).contains(&c) {
                    taken.entry(c as u32).or_default().push(*n);
                }
            }
        }
        let taken_mask = taken.keys().fold(0u32, |mask, c| mask | (1u32 << c));
        let free = mask & !taken_mask;
        let mask_then = mask;
        let got = round.colors.get(i).map_or(-1, |c| c.0);
        let (predicted, how, agrees) = if free != 0 {
            let predicted = free.trailing_zeros() as i32;
            (predicted, "lowest free", predicted == got)
        } else {
            let agrees = got < 0
                || (got < 32 && mask & (1u32 << got) == 0 && !taken.contains_key(&(got as u32)));
            if (0..32).contains(&got) {
                mask |= 1u32 << got;
            }
            (got, if got >= 0 { "claimed" } else { "spilled" }, agrees)
        };
        colored.insert(*i);
        out.insert(
            *i,
            json!({"position": position, "taken": taken, "mask": mask_then, "free": free,
            "predicted": predicted, "how": how, "agrees": agrees, "color": got}),
        );
    }
    out
}

fn colors_of(
    register: &Register,
    mapping: &BTreeMap<Register, BTreeSet<u32>>,
    coalesced: &BTreeMap<Register, Register>,
    seen: &mut BTreeSet<Register>,
) -> BTreeSet<u32> {
    if register.1 < 32 {
        return BTreeSet::from([register.1]);
    }
    if let Some(c) = mapping.get(register) {
        return c.clone();
    }
    if !seen.insert(register.clone()) {
        return BTreeSet::new();
    }
    if let Some(into) = coalesced.get(register) {
        return colors_of(into, mapping, coalesced, seen);
    }
    BTreeSet::new()
}

/// Return exactly the allocation portion of the legacy report. Other analyses
/// (inlining/scheduling/etc.) are layered on top by the document builder.
pub fn analyze(allocation: &Allocation, version: Version) -> Value {
    let before = &allocation.before;
    let after = &allocation.after;
    let names = opcode_names(version);
    let before_pointers: BTreeSet<_> = before
        .blocks
        .iter()
        .flat_map(|b| &b.instructions)
        .map(|i| &i.pointer)
        .collect();
    let after_instructions: BTreeMap<_, _> = after
        .blocks
        .iter()
        .flat_map(|b| &b.instructions)
        .map(|i| (&i.pointer, i))
        .collect();
    let mut mapping: BTreeMap<Register, BTreeSet<u32>> = BTreeMap::new();
    let mut definitions = BTreeMap::new();
    for b in &before.blocks {
        for i in &b.instructions {
            for op in &i.operands {
                if op.defines() {
                    definitions
                        .entry((op.class(), op.register()))
                        .or_insert((b.index, i));
                }
            }
            if let Some(other) = after_instructions.get(&i.pointer) {
                for (a, b) in i.operands.iter().zip(&other.operands) {
                    if a.is_register()
                        && b.is_register()
                        && a.class() == b.class()
                        && a.register() >= 32
                        && b.register() < 32
                    {
                        mapping
                            .entry((a.class(), a.register()))
                            .or_default()
                            .insert(b.register());
                    }
                }
            }
        }
    }
    let removed: Vec<_> = before
        .blocks
        .iter()
        .flat_map(|b| &b.instructions)
        .filter(|i| !after_instructions.contains_key(&i.pointer))
        .collect();
    let inserted: Vec<_> = after
        .blocks
        .iter()
        .flat_map(|b| &b.instructions)
        .filter(|i| !before_pointers.contains(&i.pointer))
        .collect();
    let mut coalesced = BTreeMap::new();
    for i in &removed {
        if names
            .get(&i.opcode)
            .is_some_and(|name| name == "mr" || name == "fmr")
            && i.operands.len() >= 2
        {
            let d = &i.operands[0];
            let src = &i.operands[1];
            if d.is_register() && src.is_register() && d.register() >= 32 {
                coalesced.insert((d.class(), d.register()), (src.class(), src.register()));
            }
        }
    }
    let live = liveness(before);
    let mut result = json!({"function": before.function, "link": before.link, "blocks": before.blocks.len(),
        "instructions": before.blocks.iter().map(|b| b.instructions.len()).sum::<usize>(), "classes": {},
        "removed": removed.iter().map(|i| i.text(names)).collect::<Vec<_>>(), "inserted": inserted.iter().map(|i| i.text(names)).collect::<Vec<_>>(),
        "frame": before.frame, "layout": before.layout});
    for class in ["GPR", "FPR", "VR", "CR"] {
        let rounds: Vec<_> = before
            .rounds
            .iter()
            .filter(|r| class_name(r.class) == class)
            .collect();
        let last = rounds.last().copied();
        let mut numbers: BTreeSet<_> = live
            .counts
            .keys()
            .filter(|r| r.0 == class && r.1 >= 32)
            .map(|r| r.1)
            .collect();
        if let Some(r) = last {
            numbers.extend(r.nodes.keys());
        }
        if numbers.is_empty() && last.is_none() {
            continue;
        }
        let mut info = json!({"rounds": rounds.iter().map(|r| json!({"k": r.k, "mask": r.mask, "ok": r.ok, "nodes": r.nodes.len(),
            "spilled": r.color_order.iter().filter(|i|r.colors.get(i).is_some_and(|c|c.1&1!=0)).copied().collect::<Vec<_>>()})).collect::<Vec<_>>(), "vregs": []});
        let mut pushes = BTreeMap::new();
        let mut select = BTreeMap::new();
        if let Some(r) = last {
            let simplified = simplify(r, Some(rounds[0].count));
            pushes = simplified.pushes.into_iter().collect();
            select = replay_select(r);
            info["k"] = json!(r.k);
            info["mask"] = json!(r.mask);
            info["order"] = json!(r.order.iter().map(|(i, _)| *i).collect::<Vec<_>>());
            info["simplify_replay_agrees"] = json!(simplified.agrees);
            info["select_replay_agrees"] =
                json!(select.values().filter(|s| s["agrees"] == true).count());
            info["select_replayed"] = json!(select.len());
        }
        let mut entries = vec![];
        for number in numbers {
            let register = (class.to_owned(), number);
            let node = last.and_then(|r| r.nodes.get(&number));
            let colors: Vec<_> = colors_of(&register, &mapping, &coalesced, &mut BTreeSet::new())
                .into_iter()
                .collect();
            let (uses, defs) = live.counts.get(&register).copied().unwrap_or_default();
            let phys = match colors.len() {
                0 => Value::Null,
                1 => json!(colors[0]),
                _ => json!(colors),
            };
            let mut entry = json!({"vreg": number, "class": class, "phys": phys, "defs": defs, "uses": uses,
                "live_blocks": live.ranges.get(&register).map_or(0, |s| s.len()), "across_call": live.across.get(&register).map_or(0, |s| s.len()),
                "first_call_block": live.across.get(&register).and_then(|s| s.first())});
            if let Some((block, instruction)) = definitions.get(&register) {
                entry["def"] = json!({"block": block, "line": instruction.line.filter(|l| *l > 0), "instruction": instruction.text(names)});
            }
            if let Some(n) = node {
                let r = last.unwrap();
                entry["degree"] = json!(n.degree);
                entry["graph_flags"] = json!(n.flags);
                entry["neighbors"] = json!(n.neighbors.iter().map(|v| json!({"vreg": v, "phys": if *v < 32 { *v as i32 }
                    else { r.colors.get(v).map_or_else(|| r.nodes.get(v).map_or(-1, |n| n.color), |c| c.0) }})).collect::<Vec<_>>());
            }
            if !live.counts.contains_key(&register) {
                entry["in_pcode"] = json!(false);
            }
            let traced = last.and_then(|r| r.colors.get(&number));
            if let Some(n) = node.filter(|n| n.flags & 4 != 0) {
                entry["state"] = json!("coalesced");
                entry["coalesced_into"] = json!((n.color >= 0).then_some(n.color));
                if colors.is_empty() && n.color >= 0 {
                    let mut into = n.color;
                    let mut seen = BTreeSet::new();
                    let r = last.unwrap();
                    while into >= 32 && seen.insert(into) {
                        into = r.colors.get(&(into as u32)).map_or_else(
                            || r.nodes.get(&(into as u32)).map_or(-1, |n| n.color),
                            |c| c.0,
                        );
                    }
                    entry["phys"] = json!((0..32).contains(&into).then_some(into));
                }
            } else if traced.is_some_and(|c| c.1 & 1 != 0) {
                entry["state"] = json!("spilled");
            } else if colors.is_empty() && traced.is_some_and(|c| c.0 >= 0) {
                entry["state"] = json!("colored");
                entry["phys"] = json!(traced.unwrap().0);
            } else if colors.is_empty() {
                entry["state"] = json!("spilled");
            } else if !mapping.contains_key(&register) && coalesced.contains_key(&register) {
                entry["state"] = json!("coalesced");
                entry["coalesced_into"] = json!(coalesced[&register].1);
            } else if colors.len() > 1 {
                entry["state"] = json!("split");
            } else {
                entry["state"] = json!("colored");
            }
            if let Some(s) = select.get(&number) {
                entry["order"] = s["position"].clone();
                let mut selected = s.clone();
                selected.as_object_mut().unwrap().remove("position");
                selected["flags"] = json!(traced.map_or(0, |c| c.1));
                entry["select"] = selected;
            }
            if let Some(p) = pushes.get(&number) {
                entry["pushed"] = p.clone();
            }
            if rounds
                .iter()
                .take(rounds.len().saturating_sub(1))
                .any(|r| r.colors.get(&number).is_some_and(|c| c.1 & 1 != 0))
            {
                entry["spilled_in_earlier_round"] = json!(true);
            }
            entries.push(entry);
        }
        info["vregs"] = json!(entries);
        result["classes"][class] = info;
    }
    result
}
