//! What-if graph replay. This predicts claims rather than reading them back
//! from the traced result. Graph edits describe hypotheses for source changes.
use crate::{
    allocation,
    trace::{Allocation, Round},
    version::class_name,
};
use anyhow::{Context, Result, ensure};
use serde::Serialize;
use serde_json::{Value, json};
use std::collections::{BTreeMap, BTreeSet};
pub mod search;
pub mod target;

#[derive(Clone, Debug, PartialEq, Eq)]
pub enum Edit {
    Move { node: u32, after: bool, anchor: u32 },
    Order(Vec<u32>),
    Drop(u32),
    Degree(u32, i32),
    Unlink(u32, u32),
    Link(u32, u32),
}

impl Edit {
    pub fn kind(&self) -> &'static str {
        match self {
            Self::Move { .. } => "move",
            Self::Order(_) => "order",
            Self::Drop(_) => "drop",
            Self::Degree(..) => "degree",
            Self::Unlink(..) => "unlink",
            Self::Link(..) => "link",
        }
    }
    pub fn value(&self) -> Value {
        match self {
            Self::Move {
                node,
                after,
                anchor,
            } => json!([
                "move",
                node,
                if *after { "after" } else { "before" },
                anchor
            ]),
            Self::Order(v) => json!(["order", v]),
            Self::Drop(v) => json!(["drop", v]),
            Self::Degree(v, d) => json!(["degree", v, d]),
            Self::Unlink(a, b) => json!(["unlink", a, b]),
            Self::Link(a, b) => json!(["link", a, b]),
        }
    }
}
impl Serialize for Edit {
    fn serialize<S: serde::Serializer>(
        &self,
        serializer: S,
    ) -> std::result::Result<S::Ok, S::Error> {
        self.value().serialize(serializer)
    }
}
impl Ord for Edit {
    fn cmp(&self, other: &Self) -> std::cmp::Ordering {
        self.kind()
            .cmp(other.kind())
            .then_with(|| match (self, other) {
                (
                    Self::Move {
                        node: a,
                        after: x,
                        anchor: b,
                    },
                    Self::Move {
                        node: c,
                        after: y,
                        anchor: d,
                    },
                ) => a.cmp(c).then_with(|| y.cmp(x)).then_with(|| b.cmp(d)),
                (Self::Order(a), Self::Order(b)) => a.cmp(b),
                (Self::Drop(a), Self::Drop(b)) => a.cmp(b),
                (Self::Degree(a, b), Self::Degree(c, d)) => a.cmp(c).then_with(|| b.cmp(d)),
                (Self::Unlink(a, b), Self::Unlink(c, d)) | (Self::Link(a, b), Self::Link(c, d)) => {
                    a.cmp(c).then_with(|| b.cmp(d))
                }
                _ => std::cmp::Ordering::Equal,
            })
    }
}
impl PartialOrd for Edit {
    fn partial_cmp(&self, other: &Self) -> Option<std::cmp::Ordering> {
        Some(self.cmp(other))
    }
}

pub fn renumbered(order: &[u32]) -> Vec<u32> {
    let mut best: Vec<Vec<usize>> = vec![];
    for (k, v) in order.iter().enumerate() {
        let mut longest = vec![];
        for j in 0..k {
            if order[j] < *v && best[j].len() > longest.len() {
                longest = best[j].clone();
            }
        }
        longest.push(k);
        best.push(longest);
    }
    let mut keep = vec![];
    for run in best {
        if run.len() > keep.len() {
            keep = run;
        }
    }
    let keep: BTreeSet<_> = keep.into_iter().map(|k| order[k]).collect();
    order
        .iter()
        .filter(|v| !keep.contains(v))
        .copied()
        .collect()
}

pub fn edited(round: &Round, edits: &[Edit]) -> Result<(Round, BTreeMap<u32, u32>)> {
    let mut rename = BTreeMap::new();
    if edits
        .iter()
        .any(|e| matches!(e, Edit::Move { .. } | Edit::Order(_)))
    {
        let slots: Vec<_> = round.nodes.keys().filter(|v| **v >= 32).copied().collect();
        let mut sequence = slots.clone();
        for edit in edits {
            match edit {
                Edit::Order(order) => {
                    ensure!(
                        order.iter().all(|v| sequence.contains(v)),
                        "order names an absent or physical node"
                    );
                    ensure!(
                        order.iter().collect::<BTreeSet<_>>().len() == order.len(),
                        "duplicate node in order"
                    );
                    let moved: BTreeSet<_> = renumbered(order).into_iter().collect();
                    let staying = order.iter().find(|v| !moved.contains(v));
                    for (k, v) in order.iter().enumerate() {
                        if moved.contains(v) {
                            let old = sequence
                                .iter()
                                .position(|n| n == v)
                                .context("order node missing")?;
                            sequence.remove(old);
                            let anchor = if k > 0 {
                                order[k - 1]
                            } else {
                                **staying.as_ref().context("order has no staying node")?
                            };
                            let at = sequence
                                .iter()
                                .position(|n| *n == anchor)
                                .context("order anchor missing")?
                                + usize::from(k > 0);
                            sequence.insert(at, *v);
                        }
                    }
                }
                Edit::Move {
                    node,
                    after,
                    anchor,
                } => {
                    ensure!(node != anchor, "cannot move a node relative to itself");
                    let old = sequence
                        .iter()
                        .position(|n| n == node)
                        .context("move node missing or physical")?;
                    sequence.remove(old);
                    let at = sequence
                        .iter()
                        .position(|n| n == anchor)
                        .context("move anchor missing or physical")?
                        + usize::from(*after);
                    sequence.insert(at, *node);
                }
                _ => (),
            }
        }
        rename = sequence
            .into_iter()
            .zip(slots)
            .filter(|(v, slot)| v != slot)
            .collect();
    }
    let name = |v: u32| rename.get(&v).copied().unwrap_or(v);
    let mut graph = round.clone();
    graph.nodes.clear();
    for (v, node) in &round.nodes {
        let mut node = node.clone();
        node.neighbors = node.neighbors.into_iter().map(name).collect();
        if node.flags & 4 != 0 && node.color >= 0 {
            node.color = name(node.color as u32) as i32;
        }
        graph.nodes.insert(name(*v), node);
    }
    graph.order = round.order.iter().map(|(v, c)| (name(*v), *c)).collect();
    graph.colors = round.colors.iter().map(|(v, c)| (name(*v), *c)).collect();
    graph.color_order = round.color_order.iter().copied().map(name).collect();
    for edit in edits {
        match *edit {
            Edit::Drop(v) => {
                let v = name(v);
                let node = graph.nodes.remove(&v).context("drop node missing")?;
                for n in node.neighbors {
                    if let Some(n) = graph.nodes.get_mut(&n)
                        && n.neighbors.contains(&v)
                    {
                        n.neighbors.retain(|n| *n != v);
                        n.degree = n.degree.checked_sub(1).context("degree underflow")?;
                    }
                }
                graph.order.retain(|(i, _)| *i != v);
            }
            Edit::Degree(v, delta) => {
                let node = graph
                    .nodes
                    .get_mut(&name(v))
                    .context("degree node missing")?;
                node.degree = node.degree.checked_add(delta).context("degree overflow")?;
            }
            Edit::Unlink(a, b) | Edit::Link(a, b) => {
                let (a, b) = (name(a), name(b));
                let unlink = matches!(edit, Edit::Unlink(..));
                for (x, y) in [(a, b), (b, a)] {
                    if let Some(node) = graph.nodes.get_mut(&x) {
                        if unlink {
                            node.neighbors.retain(|n| *n != y);
                        } else if !node.neighbors.contains(&y) {
                            node.neighbors.push(y);
                        }
                        // Degree changes are deliberately independent of adjacency:
                        // the reference permits degree edits even for absent edges.
                        node.degree = node
                            .degree
                            .checked_add(if unlink { -1 } else { 1 })
                            .context("degree overflow")?;
                    }
                }
            }
            _ => (),
        }
    }
    Ok((graph, rename))
}

fn floor(class: &str) -> u32 {
    if class == "VR" { 20 } else { 14 }
}

pub fn claim_gaps(round: &Round, class: &str) -> (Vec<u32>, BTreeSet<u32>) {
    let mut mask = round.mask.unwrap_or(0);
    let mut claims = vec![];
    for (v, _) in &round.order {
        let c = round.colors.get(v).map_or(-1, |c| c.0);
        if c >= floor(class) as i32 && c < 32 && mask & (1u32 << c) == 0 {
            claims.push(c as u32);
            mask |= 1u32 << c;
        }
    }
    let mut skipped = BTreeSet::new();
    let mut above = 31;
    for c in &claims {
        if *c < above {
            skipped.extend(c + 1..=above);
        }
        above = c.saturating_sub(1);
    }
    (claims, skipped)
}

pub fn preused(round: &Round, class: &str) -> (BTreeSet<u32>, bool) {
    let mut named: BTreeSet<_> = round
        .nodes
        .values()
        .flat_map(|n| &n.neighbors)
        .copied()
        .filter(|n| *n >= floor(class) && *n < 32)
        .collect();
    let (claims, skipped) = claim_gaps(round, class);
    named.extend(skipped);
    (named, claims.is_empty())
}

#[derive(Clone, Debug, Serialize)]
pub struct Selection {
    pub colour: BTreeMap<u32, i32>,
    pub spilled: Vec<u32>,
    pub claims: Vec<u32>,
}

pub fn select(
    round: &Round,
    order: &[u32],
    used: &BTreeSet<u32>,
    class: &str,
) -> Result<Selection> {
    let mut used = used.clone();
    let mut mask = round.mask.unwrap_or(0);
    let mut colours = BTreeMap::new();
    let mut spilled = vec![];
    let mut claims = vec![];
    for i in order {
        let mut taken = 0u32;
        for n in &round.nodes.get(i).context("select node missing")?.neighbors {
            let c = if *n < 32 {
                *n as i32
            } else {
                colours
                    .get(n)
                    .copied()
                    .unwrap_or_else(|| round.nodes.get(n).map_or(-1, |n| n.color))
            };
            if (0..32).contains(&c) {
                taken |= 1u32 << c;
            }
        }
        let free = mask & !taken;
        if free != 0 {
            colours.insert(*i, free.trailing_zeros() as i32);
            continue;
        }
        let claim = (floor(class)..32).rev().find(|c| !used.contains(c));
        if let Some(claim) = claim {
            used.insert(claim);
            claims.push(claim);
            mask |= 1u32 << claim;
            colours.insert(*i, claim as i32);
        } else {
            spilled.push(*i);
        }
    }
    Ok(Selection {
        colour: colours,
        spilled,
        claims,
    })
}

fn final_colours(round: &Round, colours: &mut BTreeMap<u32, i32>) {
    for (v, node) in &round.nodes {
        if colours.contains_key(v) || node.flags & 4 == 0 {
            continue;
        }
        let mut into = node.color;
        let mut seen = BTreeSet::new();
        while into >= 32 && seen.insert(into) {
            into = colours
                .get(&(into as u32))
                .copied()
                .unwrap_or_else(|| round.nodes.get(&(into as u32)).map_or(-1, |n| n.color));
        }
        if (0..32).contains(&into) {
            colours.insert(*v, into);
        }
    }
}

#[derive(Clone, Debug, Serialize)]
pub struct Replay {
    pub order: Vec<u32>,
    pub colour: BTreeMap<u32, i32>,
    pub spilled: Vec<u32>,
    pub claims: Vec<u32>,
    pub pushes: BTreeMap<u32, Value>,
}

pub fn replay(
    round: &Round,
    class: &str,
    count: Option<u32>,
    edits: &[Edit],
    used: Option<&BTreeSet<u32>>,
) -> Result<Replay> {
    let (graph, rename) = edited(round, edits)?;
    let simplified = allocation::simplify(&graph, count);
    let order: Vec<_> = simplified.pushes.iter().rev().map(|(i, _)| *i).collect();
    let inferred = preused(round, class).0;
    let mut selected = select(&graph, &order, used.unwrap_or(&inferred), class)?;
    final_colours(&graph, &mut selected.colour);
    let back: BTreeMap<_, _> = rename.into_iter().map(|(old, new)| (new, old)).collect();
    let name = |i: u32| back.get(&i).copied().unwrap_or(i);
    Ok(Replay {
        order: order.into_iter().map(name).collect(),
        colour: selected
            .colour
            .into_iter()
            .map(|(i, c)| (name(i), c))
            .collect(),
        spilled: selected.spilled.into_iter().map(name).collect(),
        claims: selected.claims,
        pushes: simplified
            .pushes
            .into_iter()
            .map(|(i, p)| (name(i), p))
            .collect(),
    })
}

pub fn verify(allocation: &Allocation, class: &str) -> Result<Option<Value>> {
    let rounds: Vec<_> = allocation
        .before
        .rounds
        .iter()
        .filter(|r| class_name(r.class) == class)
        .collect();
    let Some(round) = rounds.last() else {
        return Ok(None);
    };
    let result = replay(round, class, Some(rounds[0].count), &[], None)?;
    let traced: Vec<_> = round.order.iter().map(|(i, _)| *i).collect();
    let (used, inferred) = preused(round, class);
    let disagree: Vec<_> = traced
        .iter()
        .filter_map(|i| {
            let c = round.colors.get(i).map_or(-1, |c| c.0);
            (c >= 0 && result.colour.get(i) != Some(&c))
                .then(|| json!([i, result.colour.get(i), c]))
        })
        .collect();
    let spilled: Vec<_> = traced
        .iter()
        .copied()
        .filter(|i| round.colors.get(i).is_some_and(|c| c.1 & 1 != 0))
        .collect();
    Ok(Some(
        json!({"function":allocation.before.function,"link":allocation.before.link,"class":class,"nodes":round.order.len(),"rounds":rounds.len(),"order_agrees":result.order==traced,
        "colour_disagreements":disagree,"claims":result.claims,"traced_claims":claim_gaps(round,class).0,"preused":used,"inferred":inferred,"spilled":result.spilled,"traced_spilled":spilled,
        "ok":result.order==traced&&disagree.is_empty()&&result.spilled==spilled}),
    ))
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::trace::Node;
    fn graph() -> Round {
        let mut r = Round {
            class: 4,
            k: 8,
            start: 32,
            count: 35,
            mask: Some(9),
            ..Default::default()
        };
        for v in 32..35 {
            let neighbors = vec![0, 3, 32, 33, 34]
                .into_iter()
                .filter(|n| *n != v)
                .collect::<Vec<_>>();
            r.nodes.insert(
                v,
                Node {
                    pointer: format!("{v:x}"),
                    degree: neighbors.len() as i32,
                    flags: 0,
                    color: -1,
                    neighbors,
                },
            );
        }
        r
    }
    #[test]
    fn claims_and_renumbering_match_reference_rules() {
        let r = graph();
        let result = replay(&r, "GPR", None, &[], None).unwrap();
        assert_eq!(result.order, [34, 33, 32]);
        assert_eq!(result.claims, [31, 30, 29]);
        assert_eq!(result.colour[&32], 29);
        let moved = replay(
            &r,
            "GPR",
            None,
            &[Edit::Move {
                node: 32,
                after: true,
                anchor: 34,
            }],
            None,
        )
        .unwrap();
        assert_eq!(moved.order, [32, 34, 33]);
        assert_eq!(moved.colour[&32], 31);
        let before = replay(
            &r,
            "GPR",
            None,
            &[Edit::Move {
                node: 34,
                after: false,
                anchor: 32,
            }],
            None,
        )
        .unwrap();
        assert_eq!(
            before.colour,
            BTreeMap::from([(33, 31), (32, 30), (34, 29)])
        );
        assert_eq!(renumbered(&[51, 32, 44, 45]), [51]);
    }
    #[test]
    fn invalid_edits_return_errors_without_mutating_graph() {
        let r = graph();
        assert!(edited(&r, &[Edit::Drop(99)]).is_err());
        assert!(edited(&r, &[Edit::Order(vec![32, 32])]).is_err());
        assert_eq!(r.nodes.len(), 3);
    }
}
