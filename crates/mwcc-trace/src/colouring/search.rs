use super::{Edit, Replay, final_colours, floor, preused, renumbered, replay, select};
use crate::{python_random::Random, trace::Round};
use anyhow::{Context, Result, ensure};
use serde::Serialize;
use std::collections::{BTreeMap, BTreeSet};

#[derive(Clone, Debug, Serialize)]
pub struct Scored {
    #[serde(flatten)]
    pub replay: Replay,
    pub wrong: Vec<u32>,
}
impl Scored {
    pub fn score(&self) -> Option<usize> {
        self.replay.spilled.is_empty().then_some(self.wrong.len())
    }
}

pub struct Problem<'a> {
    pub round: &'a Round,
    pub class: &'a str,
    pub count: Option<u32>,
    pub used: BTreeSet<u32>,
    pub targets: BTreeMap<u32, i32>,
    pub base: Scored,
}
impl<'a> Problem<'a> {
    pub fn new(
        round: &'a Round,
        class: &'a str,
        count: Option<u32>,
        targets: BTreeMap<u32, i32>,
    ) -> Result<Self> {
        let targets: BTreeMap<_, _> = targets
            .into_iter()
            .filter(|(v, _)| round.nodes.contains_key(v))
            .collect();
        let used = preused(round, class).0;
        let replay = replay(round, class, count, &[], Some(&used))?;
        let wrong = targets
            .iter()
            .filter(|(v, t)| replay.colour.get(v) != Some(t))
            .map(|(v, _)| *v)
            .collect();
        Ok(Self {
            round,
            class,
            count,
            used,
            targets,
            base: Scored { replay, wrong },
        })
    }
    pub fn run(&self, edits: &[Edit]) -> Result<Scored> {
        let replay = replay(self.round, self.class, self.count, edits, Some(&self.used))?;
        let dropped: BTreeSet<_> = edits
            .iter()
            .filter_map(|e| {
                if let Edit::Drop(v) = e {
                    Some(*v)
                } else {
                    None
                }
            })
            .collect();
        let wrong = self
            .targets
            .iter()
            .filter(|(v, t)| !dropped.contains(v) && replay.colour.get(v) != Some(t))
            .map(|(v, _)| *v)
            .collect();
        Ok(Scored { replay, wrong })
    }
}

#[derive(Debug, Serialize)]
pub struct Forced {
    pub order: Vec<u32>,
    pub colour: BTreeMap<u32, i32>,
    pub wrong: Vec<u32>,
    pub spilled: Vec<u32>,
    pub exact: bool,
}
pub fn force(problem: &Problem<'_>) -> Result<Forced> {
    let claims =
        |i: &u32| problem.targets.get(i).copied().unwrap_or(-1) >= floor(problem.class) as i32;
    let mut ranked: Vec<_> = problem
        .base
        .replay
        .order
        .iter()
        .copied()
        .filter(claims)
        .collect();
    ranked.sort_by_key(|i| -problem.targets[i]);
    let mut ranked = ranked.into_iter();
    let order: Vec<_> = problem
        .base
        .replay
        .order
        .iter()
        .map(|i| {
            if claims(i) {
                ranked.next().unwrap()
            } else {
                *i
            }
        })
        .collect();
    let mut result = select(problem.round, &order, &problem.used, problem.class)?;
    final_colours(problem.round, &mut result.colour);
    let wrong: Vec<_> = problem
        .targets
        .iter()
        .filter(|(v, t)| result.colour.get(v) != Some(t))
        .map(|(v, _)| *v)
        .collect();
    let exact = wrong.is_empty() && result.spilled.is_empty();
    Ok(Forced {
        order,
        colour: result.colour,
        wrong,
        spilled: result.spilled,
        exact,
    })
}

pub fn candidate_edits(problem: &Problem<'_>, kinds: &[String]) -> Vec<Edit> {
    let r = problem.round;
    let wrong: BTreeSet<_> = problem.base.wrong.iter().copied().collect();
    let floor = floor(problem.class) as i32;
    let movers: BTreeSet<_> = r
        .nodes
        .iter()
        .filter(|(i, n)| {
            **i >= 32
                && n.flags & 4 == 0
                && (wrong.contains(i)
                    || problem.base.replay.colour.get(i).copied().unwrap_or(-1) >= floor
                    || problem.targets.get(i).copied().unwrap_or(-1) >= floor)
        })
        .map(|(i, _)| *i)
        .collect();
    let has = |kind: &str| kinds.iter().any(|s| s == kind);
    let mut edits = vec![];
    if has("drop") {
        let near: BTreeSet<_> = wrong
            .iter()
            .flat_map(|v| &r.nodes[v].neighbors)
            .copied()
            .filter(|n| *n >= 32)
            .collect();
        edits.extend(
            near.into_iter()
                .filter(|n| r.nodes.get(n).is_some_and(|n| n.flags & 4 != 0))
                .map(Edit::Drop),
        );
    }
    if has("degree") {
        for v in &movers {
            edits.push(Edit::Degree(*v, 1));
            edits.push(Edit::Degree(*v, -1));
        }
    }
    if has("neighbours") {
        for v in &movers {
            edits.push(Edit::Neighbours(*v, 1));
        }
    }
    if has("unlink") {
        for v in &wrong {
            for n in &r.nodes[v].neighbors {
                if movers.contains(n) {
                    edits.push(Edit::Unlink((*v).min(*n), (*v).max(*n)));
                }
            }
        }
    }
    if has("link") {
        for v in &wrong {
            for n in &movers {
                if n != v && !r.nodes[v].neighbors.contains(n) {
                    edits.push(Edit::Link((*v).min(*n), (*v).max(*n)));
                }
            }
        }
    }
    let mut seen = BTreeSet::new();
    edits.retain(|e| seen.insert(e.clone()));
    edits
}

pub fn claimers(problem: &Problem<'_>) -> Vec<u32> {
    let positions: BTreeMap<_, _> = problem
        .base
        .replay
        .order
        .iter()
        .enumerate()
        .map(|(k, v)| (*v, k))
        .collect();
    let mut nodes: Vec<_> = problem
        .targets
        .iter()
        .filter(|(v, c)| **c >= floor(problem.class) as i32 && positions.contains_key(v))
        .map(|(v, _)| *v)
        .collect();
    nodes.sort_by_key(|v| (-problem.targets[v], positions[v]));
    let mut seen = BTreeSet::new();
    nodes.retain(|v| seen.insert(problem.targets[v]));
    nodes
}

pub fn search_nodes(problem: &Problem<'_>, fixed: &BTreeSet<u32>) -> Vec<u32> {
    let mut nodes: BTreeSet<_> = claimers(problem)
        .into_iter()
        .chain(problem.base.wrong.iter().copied())
        .collect();
    nodes.extend(
        problem
            .base
            .replay
            .colour
            .iter()
            .filter(|(v, c)| **c >= floor(problem.class) as i32 && !problem.targets.contains_key(v))
            .map(|(v, _)| *v),
    );
    nodes
        .into_iter()
        .filter(|v| {
            *v >= 32
                && problem.round.nodes.get(v).is_some_and(|n| n.flags & 4 == 0)
                && !fixed.contains(v)
        })
        .collect()
}

pub fn compatible(edits: &BTreeSet<Edit>, edit: &Edit) -> bool {
    if edits.contains(edit)
        || matches!(edit,Edit::Move{node,..}if edits.iter().any(|e|matches!(e,Edit::Move{node:n,..}if n==node)))
    {
        return false;
    }
    fn scalar_arguments(e: &Edit) -> Vec<i64> {
        match e {
            Edit::Move { node, anchor, .. } => vec![i64::from(*node), i64::from(*anchor)],
            Edit::Drop(v) => vec![i64::from(*v)],
            Edit::Degree(v, d) | Edit::Neighbours(v, d) => vec![i64::from(*v), i64::from(*d)],
            Edit::Unlink(a, b) | Edit::Link(a, b) => vec![i64::from(*a), i64::from(*b)],
            Edit::Order(_) => vec![],
        }
    }
    let dropped: BTreeSet<_> = edits
        .iter()
        .filter_map(|e| {
            if let Edit::Drop(v) = e {
                Some(i64::from(*v))
            } else {
                None
            }
        })
        .collect();
    if scalar_arguments(edit).iter().any(|v| dropped.contains(v)) {
        return false;
    }
    !matches!(edit,Edit::Drop(v)if edits.iter().any(|e|scalar_arguments(e).contains(&i64::from(*v))))
}

pub fn inverted(order: &[u32], original: &[u32]) -> usize {
    let rank: BTreeMap<_, _> = original.iter().enumerate().map(|(k, v)| (*v, k)).collect();
    let ranks: Vec<_> = order.iter().map(|v| rank[v]).collect();
    ranks
        .iter()
        .enumerate()
        .map(|(k, x)| ranks[k + 1..].iter().filter(|y| x > *y).count())
        .sum()
}

#[derive(Clone, Debug)]
pub struct Options {
    pub kinds: Vec<String>,
    pub iterations: usize,
    pub seed: String,
    pub nodes: Option<Vec<u32>>,
    pub fixed: BTreeSet<u32>,
    pub keep: usize,
    pub restarts: usize,
}
impl Default for Options {
    fn default() -> Self {
        Self {
            kinds: vec!["drop".into(), "degree".into(), "unlink".into()],
            iterations: 20000,
            seed: "1".into(),
            nodes: None,
            fixed: BTreeSet::new(),
            keep: 8,
            restarts: 10,
        }
    }
}
#[derive(Debug, Serialize)]
pub struct Found {
    pub exact: Vec<(usize, Vec<Edit>)>,
    pub best: Option<(usize, Vec<Edit>)>,
    pub singles: Vec<(usize, Edit)>,
    pub nodes: Vec<u32>,
}

struct Search<'a, 'b> {
    problem: &'a Problem<'b>,
    nodes: Vec<u32>,
    chain: Vec<u32>,
    rank: BTreeMap<u32, usize>,
}
type Chosen = BTreeSet<Edit>;
type State = (Vec<u32>, Chosen);
impl Search<'_, '_> {
    fn edits(&self, order: &[u32], chosen: &Chosen) -> Vec<Edit> {
        let mut out = vec![];
        if order != self.nodes {
            out.push(Edit::Order(order.to_vec()));
        }
        out.extend(chosen.iter().cloned());
        out
    }
    fn jumped(&self, edit: &Edit) -> usize {
        if let Edit::Move {
            node,
            after,
            anchor,
        } = edit
        {
            let (low, high) = ((*node).min(*anchor), (*node).max(*anchor));
            let passed = *after == (*anchor >= *node);
            self.nodes
                .iter()
                .filter(|v| low < **v && **v < high)
                .count()
                + usize::from(passed && self.rank.contains_key(anchor))
        } else {
            0
        }
    }
    fn evaluate(&self, order: &[u32], chosen: &Chosen) -> Result<Option<(usize, usize)>> {
        let result = self.problem.run(&self.edits(order, chosen))?;
        let Some(wrong) = result.score() else {
            return Ok(None);
        };
        let cost = if wrong > 0 {
            let positions: BTreeMap<_, _> = result
                .replay
                .order
                .iter()
                .enumerate()
                .map(|(k, v)| (*v, k as i64))
                .collect();
            let ranks: Vec<_> = self
                .chain
                .iter()
                .map(|v| positions.get(v).copied().unwrap_or(-1))
                .collect();
            let inversions: usize = ranks
                .iter()
                .enumerate()
                .map(|(k, x)| ranks[k + 1..].iter().filter(|y| x > *y).count())
                .sum();
            1_000_000 + wrong * 1000 + inversions + 50 * chosen.len()
        } else {
            100 * (renumbered(order).len() + chosen.len())
                + inverted(order, &self.nodes)
                + chosen.iter().map(|e| self.jumped(e)).sum::<usize>()
        };
        Ok(Some((wrong, cost)))
    }
    fn put_back(&self, order: &[u32], v: u32) -> Vec<u32> {
        let moved: BTreeSet<_> = renumbered(order).into_iter().collect();
        let mut rest: Vec<_> = order.iter().copied().filter(|x| *x != v).collect();
        let at = rest
            .iter()
            .position(|x| !moved.contains(x) && self.rank[x] > self.rank[&v])
            .unwrap_or(rest.len());
        rest.insert(at, v);
        rest
    }
    fn tighten(&self, mut order: Vec<u32>, mut chosen: Chosen, limit: usize) -> Result<State> {
        loop {
            let mut improved = false;
            for v in renumbered(&order) {
                let moved = renumbered(&order);
                let trial = self.put_back(&order, v);
                if renumbered(&trial).len() < moved.len()
                    && self
                        .evaluate(&trial, &chosen)?
                        .is_some_and(|(wrong, _)| wrong <= limit)
                {
                    order = trial;
                    improved = true;
                }
            }
            for k in 0..order.len().saturating_sub(1) {
                if self.rank[&order[k]] > self.rank[&order[k + 1]] {
                    let mut trial = order.clone();
                    trial.swap(k, k + 1);
                    if self
                        .evaluate(&trial, &chosen)?
                        .is_some_and(|(wrong, _)| wrong <= limit)
                    {
                        order = trial;
                        improved = true;
                    }
                }
            }
            for edit in chosen.clone() {
                let mut trial = chosen.clone();
                trial.remove(&edit);
                if self
                    .evaluate(&order, &trial)?
                    .is_some_and(|(wrong, _)| wrong <= limit)
                {
                    chosen = trial;
                    improved = true;
                }
            }
            if !improved {
                break;
            }
        }
        Ok((order, chosen))
    }
    fn merge(&self, mut order: Vec<u32>, chosen: &Chosen) -> Result<Vec<u32>> {
        loop {
            let moved = renumbered(&order);
            let mut improved = None;
            'groups: for size in [2, 3] {
                for group in combinations(&moved, size) {
                    let mut trial = order.clone();
                    for v in group {
                        trial = self.put_back(&trial, v);
                    }
                    if renumbered(&trial).len() < moved.len()
                        && self
                            .evaluate(&trial, chosen)?
                            .is_some_and(|(wrong, _)| wrong == 0)
                    {
                        improved = Some(trial);
                        break 'groups;
                    }
                }
            }
            if let Some(trial) = improved {
                order = trial;
                continue;
            }
            'pairs: for pair in combinations(&moved, 2) {
                let back = self.put_back(&self.put_back(&order, pair[0]), pair[1]);
                for u in &back {
                    let rest: Vec<_> = back.iter().copied().filter(|x| x != u).collect();
                    for at in 0..back.len() {
                        let mut trial = rest.clone();
                        trial.insert(at, *u);
                        if renumbered(&trial).len() < moved.len()
                            && self
                                .evaluate(&trial, chosen)?
                                .is_some_and(|(wrong, _)| wrong == 0)
                        {
                            improved = Some(trial);
                            break 'pairs;
                        }
                    }
                }
            }
            if let Some(trial) = improved {
                order = trial;
            } else {
                break;
            }
        }
        Ok(order)
    }
}

fn combinations(values: &[u32], size: usize) -> Vec<Vec<u32>> {
    fn visit(
        values: &[u32],
        size: usize,
        start: usize,
        current: &mut Vec<u32>,
        out: &mut Vec<Vec<u32>>,
    ) {
        if current.len() == size {
            out.push(current.clone());
            return;
        }
        for i in start..values.len() {
            current.push(values[i]);
            visit(values, size, i + 1, current, out);
            current.pop();
        }
    }
    let mut out = vec![];
    visit(values, size, 0, &mut vec![], &mut out);
    out
}
fn remember(values: &mut Vec<(Vec<Edit>, usize)>, edits: Vec<Edit>, cost: usize) {
    if !values.iter().any(|(e, _)| *e == edits) {
        values.push((edits, cost));
    }
}

pub fn search(problem: &Problem<'_>, options: &Options) -> Result<Found> {
    search_with_progress(problem, options, |_, _, _, _| {})
}
pub fn search_with_progress(
    problem: &Problem<'_>,
    options: &Options,
    mut progress: impl FnMut(usize, usize, usize, Option<usize>),
) -> Result<Found> {
    let nodes = options
        .nodes
        .clone()
        .unwrap_or_else(|| search_nodes(problem, &options.fixed));
    ensure!(
        nodes.iter().collect::<BTreeSet<_>>().len() == nodes.len()
            && nodes
                .iter()
                .all(|v| *v >= 32 && problem.round.nodes.contains_key(v)),
        "search nodes must be distinct virtual graph nodes"
    );
    let context = Search {
        problem,
        chain: claimers(problem),
        rank: nodes.iter().enumerate().map(|(k, v)| (*v, k)).collect(),
        nodes: nodes.clone(),
    };
    let mut rng = Random::new(&options.seed)?;
    let mut extras = candidate_edits(problem, &options.kinds);
    let base = context
        .evaluate(&nodes, &Chosen::new())?
        .context("search cannot model a baseline spill")?
        .0;
    let mut singles = vec![];
    let mut starts: Vec<(usize, State)> = vec![];
    for edit in &extras {
        let chosen = Chosen::from([edit.clone()]);
        if let Some((wrong, _)) = context.evaluate(&nodes, &chosen)?
            && wrong < base
        {
            singles.push((wrong, edit.clone()));
            starts.push((wrong, (nodes.clone(), chosen)));
        }
    }
    for v in &nodes {
        let mut moves = vec![Edit::Move {
            node: *v,
            after: false,
            anchor: nodes[0],
        }];
        moves.extend(nodes.iter().map(|w| Edit::Move {
            node: *v,
            after: true,
            anchor: *w,
        }));
        for edit in moves {
            if matches!(&edit,Edit::Move{node,anchor,..}if node==anchor) {
                continue;
            }
            let chosen = Chosen::from([edit.clone()]);
            if let Some((wrong, _)) = context.evaluate(&nodes, &chosen)?
                && wrong < base
            {
                singles.push((wrong, edit.clone()));
                starts.push((wrong, (nodes.clone(), chosen)));
                extras.push(edit);
            }
        }
    }
    singles.sort_by_key(|(wrong, e)| (*wrong, context.jumped(e)));
    starts.sort_by_key(|(wrong, (_, chosen))| {
        (
            *wrong,
            chosen.iter().map(|e| context.jumped(e)).sum::<usize>(),
        )
    });
    let start = starts
        .first()
        .map(|(_, s)| s.clone())
        .unwrap_or((nodes.clone(), Chosen::new()));
    let (wrong, _) = context
        .evaluate(&start.0, &start.1)?
        .context("starting edits cause a spill")?;
    let mut best_wrong = (wrong, start.clone());
    let mut exact = vec![];
    for (w, (order, chosen)) in &starts {
        if *w == 0 {
            remember(
                &mut exact,
                context.edits(order, chosen),
                context.evaluate(order, chosen)?.unwrap().1,
            );
        }
    }
    for restart in 0..if wrong > 0 { options.restarts } else { 0 } {
        let (mut order, mut chosen) = start.clone();
        let mut wrong = best_wrong.0;
        let mut cost = context.evaluate(&order, &chosen)?.unwrap().1;
        let mut temperature = 200.0f64;
        for step in 0..options.iterations / options.restarts {
            let (mut new_order, mut new_chosen) = (order.clone(), chosen.clone());
            let roll = rng.random();
            if !extras.is_empty() && (roll < 0.2 || nodes.len() < 2) {
                let edit = rng.choice(&extras)?.clone();
                if !new_chosen.remove(&edit) {
                    new_chosen.insert(edit);
                }
            } else if nodes.len() >= 2 {
                let (i, j) = rng.sample_two(nodes.len())?;
                if roll < 0.6 {
                    new_order.swap(i, j);
                } else {
                    let v = new_order.remove(i);
                    new_order.insert(j, v);
                }
            } else {
                break;
            }
            if !new_chosen.iter().all(|x| {
                let mut others = new_chosen.clone();
                others.remove(x);
                compatible(&others, x)
            }) {
                continue;
            }
            let Some((new_wrong, new_cost)) = context.evaluate(&new_order, &new_chosen)? else {
                continue;
            };
            if new_cost <= cost
                || rng.random() < ((cost as f64 - new_cost as f64) / temperature).exp()
            {
                order = new_order;
                chosen = new_chosen;
                wrong = new_wrong;
                cost = new_cost;
                if wrong > 0 && wrong < best_wrong.0 {
                    best_wrong = (wrong, (order.clone(), chosen.clone()));
                }
                if wrong == 0 {
                    remember(&mut exact, context.edits(&order, &chosen), cost);
                }
            }
            temperature = (temperature * 0.999).max(0.5);
            if step % 2000 == 0 {
                progress(
                    restart,
                    step,
                    wrong,
                    exact.iter().map(|(_, cost)| *cost).min(),
                );
            }
        }
    }
    exact.sort_by_key(|(_, cost)| *cost);
    let mut tightened = vec![];
    for (edits, _) in exact.into_iter().take(3 * options.keep) {
        let order = edits
            .iter()
            .find_map(|e| {
                if let Edit::Order(v) = e {
                    Some(v.clone())
                } else {
                    None
                }
            })
            .unwrap_or_else(|| nodes.clone());
        let chosen = edits
            .into_iter()
            .filter(|e| !matches!(e, Edit::Order(_)))
            .collect();
        let (order, chosen) = context.tighten(order, chosen, 0)?;
        remember(
            &mut tightened,
            context.edits(&order, &chosen),
            context.evaluate(&order, &chosen)?.unwrap().1,
        );
    }
    let mut first = tightened.clone();
    first.sort_by_key(|(_, cost)| *cost);
    for (edits, _) in first.into_iter().take(2) {
        if let Some(order) = edits.iter().find_map(|e| {
            if let Edit::Order(v) = e {
                Some(v)
            } else {
                None
            }
        }) && renumbered(order).len() >= 2
        {
            let chosen = edits
                .iter()
                .filter(|e| !matches!(e, Edit::Order(_)))
                .cloned()
                .collect();
            let order = context.merge(order.clone(), &chosen)?;
            let (order, chosen) = context.tighten(order, chosen, 0)?;
            remember(
                &mut tightened,
                context.edits(&order, &chosen),
                context.evaluate(&order, &chosen)?.unwrap().1,
            );
        }
    }
    let mut ranked: Vec<_> = tightened.into_iter().map(|(e, c)| (c, e)).collect();
    ranked.sort_by_key(|(c, e)| (*c, e.len()));
    ranked.truncate(options.keep);
    let best = if ranked.is_empty() {
        let (order, chosen) = context.tighten(best_wrong.1.0, best_wrong.1.1, best_wrong.0)?;
        Some((best_wrong.0, context.edits(&order, &chosen)))
    } else {
        None
    };
    singles.truncate(options.keep);
    Ok(Found {
        exact: ranked,
        best,
        singles,
        nodes,
    })
}
