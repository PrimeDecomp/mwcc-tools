use crate::trace::{self, Instruction, Record};
use anyhow::{Context, Result};
use indexmap::IndexMap;
use serde::Serialize;
use std::collections::{BTreeMap, BTreeSet};

#[derive(Clone, Debug, Serialize)]
pub struct Statement {
    pub line: i32,
    pub before: (String, i32),
    pub after: (String, i32),
}
#[derive(Clone, Debug, Serialize)]
pub struct Block {
    pub p: String,
    pub index: i32,
    pub count: i32,
    pub flags: u32,
    pub succ: u32,
    pub pred: u32,
    pub instructions: Vec<Instruction>,
}
#[derive(Clone, Debug, Serialize)]
pub struct Pair {
    pub a: String,
    pub b: String,
    pub counts: (i32, i32),
    pub why: String,
}
#[derive(Clone, Debug, Serialize)]
pub struct Merge {
    pub prologue: bool,
    pub pairs: Vec<Pair>,
}
#[derive(Clone, Debug, Default, Serialize)]
pub struct Function {
    pub statements: Vec<Statement>,
    pub lists: Vec<(String, Vec<Block>)>,
    pub merges: Vec<Merge>,
}

fn location(text: &str) -> Result<(String, i32)> {
    let (p, n) = text.split_once(':').context("invalid block location")?;
    Ok((p.into(), n.parse()?))
}
pub fn parse(records: &[Record]) -> Result<IndexMap<String, Function>> {
    let mut functions: IndexMap<String, Function> = IndexMap::new();
    let mut statements = vec![];
    let mut current = None;
    let mut list = None;
    for r in records {
        match r.kind.as_str() {
            "bk.stmt" => statements.push(Statement {
                line: r.number("line")?,
                before: location(r.get("before")?)?,
                after: location(r.get("after")?)?,
            }),
            "bk.list" => {
                let link = r.get("link")?.to_owned();
                let f = functions.entry(link.clone()).or_default();
                let mut phase = r.get("phase")?.to_owned();
                if phase == "raw" {
                    f.statements = std::mem::take(&mut statements);
                }
                if phase == "merged" && f.merges.len() > 1 {
                    phase = format!("merged{}", f.merges.len());
                }
                current = Some(link.clone());
                list = Some((link, f.lists.len()));
                f.lists.push((phase, vec![]));
            }
            "bk.block" => {
                if let Some((link, index)) = &list {
                    functions.get_mut(link).unwrap().lists[*index]
                        .1
                        .push(Block {
                            p: r.get("p")?.into(),
                            index: r.number("index")?,
                            count: r.number("count")?,
                            flags: u32::from_str_radix(r.get("flags")?, 16)?,
                            succ: r.number("succ")?,
                            pred: r.number("pred")?,
                            instructions: vec![],
                        });
                }
            }
            "bk.i" => {
                if let Some((link, index)) = &list
                    && let Some(b) = functions.get_mut(link).unwrap().lists[*index].1.last_mut()
                {
                    b.instructions.push(trace::instruction(r)?);
                }
            }
            "bk.merge" => {
                let link = r.get("link")?.to_owned();
                functions
                    .entry(link.clone())
                    .or_default()
                    .merges
                    .push(Merge {
                        prologue: r.get("prologue")? == "1",
                        pairs: vec![],
                    });
                current = Some(link);
                list = None;
            }
            "bk.pair" => {
                if let Some(link) = &current
                    && let Some(merge) = functions.get_mut(link).unwrap().merges.last_mut()
                {
                    let (a, b) = r
                        .get("counts")?
                        .split_once('+')
                        .context("invalid merge counts")?;
                    merge.pairs.push(Pair {
                        a: r.get("a")?.into(),
                        b: r.get("b")?.into(),
                        counts: (a.parse()?, b.parse()?),
                        why: r.get("why")?.into(),
                    });
                }
            }
            _ => (),
        }
    }
    Ok(functions)
}

#[derive(Clone, Debug, Serialize)]
pub struct Cut {
    pub line: i32,
    pub from: String,
    pub count: i32,
    pub to: String,
    pub inside: bool,
    pub cause: &'static str,
}
pub fn cuts(function: &Function, names: &BTreeMap<i32, String>, limit: Option<i32>) -> Vec<Cut> {
    let raw = function
        .lists
        .iter()
        .find(|(phase, _)| phase == "raw")
        .map(|(_, b)| b.as_slice())
        .unwrap_or(&[]);
    let by_pointer: BTreeMap<_, _> = raw.iter().map(|b| (&b.p, b)).collect();
    let points: Vec<_> = function
        .statements
        .iter()
        .flat_map(|s| [(&s.before, s.line, false), (&s.after, s.line, true)])
        .collect();
    let mut cuts = vec![];
    for pair in points.windows(2) {
        let (first, _, _) = pair[0];
        let (second, line, inside) = pair[1];
        if first.0 == second.0 {
            continue;
        }
        let block = by_pointer.get(&first.0);
        let opcode = block
            .and_then(|b| b.instructions.last())
            .and_then(|i| names.get(&i.opcode))
            .map_or("", String::as_str);
        let cause = if limit.is_some_and(|limit| first.1 > limit) {
            "limit"
        } else if block.is_none() {
            "unknown"
        } else if ["bl", "bctrl", "blrl"].contains(&opcode) {
            "call"
        } else if opcode.starts_with('b') {
            "branch"
        } else {
            "label"
        };
        cuts.push(Cut {
            line,
            from: first.0.clone(),
            count: first.1,
            to: second.0.clone(),
            inside,
            cause,
        });
    }
    cuts
}
fn cause(cause: &str, limit: Option<i32>) -> String {
    match cause {
        "limit" => format!(
            "the block held more than {} instructions",
            limit.map_or("none".into(), |n| n.to_string())
        ),
        "call" => "after a call".into(),
        "branch" => "after a branch".into(),
        "label" => "a label starts the next block (a jump target)".into(),
        _ => "the block is gone from the raw list".into(),
    }
}
fn why(reason: &str) -> &str {
    match reason {
        "prologue" => "the prologue block (not merged in this pass)",
        "empty" => "the first is empty",
        "call" => "the first has a call or a side effect",
        "branch" => "the first ends with a conditional branch",
        "successors" => "the first has another successor",
        "predecessors" => "the second has another predecessor",
        "epilogue" => "the second is the epilogue",
        "flag" => "the second is flagged (an exception handler or the like)",
        "over100" => "together more than 100 instructions",
        "-" => "merged",
        other => other,
    }
}
fn tally<'a>(values: impl Iterator<Item = &'a str>) -> Vec<(&'a str, usize)> {
    let mut counts = IndexMap::new();
    for value in values {
        *counts.entry(value).or_insert(0) += 1;
    }
    let mut counts: Vec<_> = counts.into_iter().collect();
    counts.sort_by_key(|(_, n)| std::cmp::Reverse(*n));
    counts
}

pub fn report(
    link: &str,
    function: &Function,
    names: &BTreeMap<i32, String>,
    pcode: bool,
    limit: Option<i32>,
) -> Vec<String> {
    let mut out = vec![format!("== {link}: blocks ==")];
    if function.lists.is_empty() {
        out.push("   no block lists traced (an oracle with the bk.* hooks is needed)".into());
        return out;
    }
    let raw = &function
        .lists
        .iter()
        .find(|(phase, _)| phase == "raw")
        .unwrap_or(&function.lists[0])
        .1;
    let mut counts: BTreeMap<&str, BTreeMap<&str, i32>> = BTreeMap::new();
    let mut phases = vec![];
    for (phase, blocks) in &function.lists {
        if !phases.contains(&phase.as_str()) {
            phases.push(phase.as_str());
        }
        for b in blocks {
            counts
                .entry(&b.p)
                .or_default()
                .entry(phase)
                .or_insert(b.count);
        }
    }
    out.push(format!(
        "   blocks: {}; instructions: {}",
        function
            .lists
            .iter()
            .map(|(p, b)| format!("{p} {}", b.len()))
            .collect::<Vec<_>>()
            .join(", "),
        function
            .lists
            .iter()
            .map(|(p, b)| format!("{p} {}", b.iter().map(|b| i64::from(b.count)).sum::<i64>()))
            .collect::<Vec<_>>()
            .join(", ")
    ));
    let mut lines_of: BTreeMap<&str, BTreeSet<i32>> = BTreeMap::new();
    for s in &function.statements {
        for (p, _) in [&s.before, &s.after] {
            lines_of.entry(p).or_default().insert(s.line);
        }
    }
    let cuts = cuts(function, names, limit);
    if !function.statements.is_empty() {
        out.push(format!(
            "   {} statements, {} cuts: {}",
            function.statements.len(),
            cuts.len(),
            tally(cuts.iter().map(|c| c.cause))
                .iter()
                .map(|(c, n)| format!("{n} {}", cause(c, limit)))
                .collect::<Vec<_>>()
                .join("; ")
        ));
    }
    for (number, merge) in function.merges.iter().enumerate() {
        out.push(format!(
            "   merge pass {}{}: {}",
            number + 1,
            if merge.prologue {
                " (prologue too)"
            } else {
                ""
            },
            tally(merge.pairs.iter().map(|p| p.why.as_str()))
                .iter()
                .map(|(w, n)| format!("{n} {}", why(w)))
                .collect::<Vec<_>>()
                .join("; ")
        ));
    }
    let mut why_next = BTreeMap::new();
    for merge in &function.merges {
        for pair in &merge.pairs {
            why_next.insert(pair.a.as_str(), pair.why.as_str());
        }
    }
    let cut_after: BTreeMap<_, _> = cuts.iter().map(|c| (c.from.as_str(), c)).collect();
    let row = |b: &Block| {
        let sizes = phases
            .iter()
            .map(|phase| {
                counts[b.p.as_str()]
                    .get(phase)
                    .map_or("gone".into(), i32::to_string)
            })
            .collect::<Vec<_>>()
            .join(" -> ");
        let end = b.instructions.last().map_or("-".into(), |i| {
            names
                .get(&i.opcode)
                .cloned()
                .unwrap_or_else(|| format!("op{}", i.opcode))
        });
        let cut = cut_after
            .get(b.p.as_str())
            .map_or("-".into(), |c| cause(c.cause, limit));
        let why = why_next
            .get(b.p.as_str())
            .map_or("-", |reason| match *reason {
                "prologue" | "empty" | "call" | "branch" | "successors" | "predecessors"
                | "epilogue" | "flag" | "over100" | "-" => why(reason),
                _ => "last",
            });
        (sizes, end, cut, why.to_owned())
    };
    out.push(format!("   per block: instructions ({}), the last instruction, why it was cut, why it was not merged with the next",phases.join(" -> ")));
    let flush = |group: &[&Block], out: &mut Vec<String>| {
        let Some(first) = group.first() else {
            return;
        };
        let last = group.last().unwrap();
        let lines: BTreeSet<_> = group
            .iter()
            .flat_map(|b| lines_of.get(b.p.as_str()).into_iter().flatten())
            .copied()
            .filter(|n| *n > 0)
            .collect();
        let where_ = if lines.len() > 1 {
            format!(
                " lines {}..{}",
                lines.first().unwrap(),
                lines.last().unwrap()
            )
        } else {
            lines
                .first()
                .map_or(String::new(), |n| format!(" line {n}"))
        };
        let label = if group.len() == 1 {
            format!("block {}", first.index)
        } else {
            format!("blocks {}..{} ({})", first.index, last.index, group.len())
        };
        let (sizes, end, cut, why) = row(first);
        out.push(format!(
            "      {label}{where_}: {sizes}; ends {end}; cut: {cut}; next: {why}"
        ));
    };
    let mut group: Vec<&Block> = vec![];
    for b in raw {
        if group.last().is_some_and(|last| row(last) != row(b)) {
            flush(&group, &mut out);
            group.clear();
        }
        group.push(b);
    }
    flush(&group, &mut out);
    if pcode {
        for b in raw {
            let lines: Vec<_> = lines_of
                .get(b.p.as_str())
                .into_iter()
                .flatten()
                .filter(|n| **n > 0)
                .map(i32::to_string)
                .collect();
            out.push(format!(
                "   raw block {} ({} instructions{})",
                b.index,
                b.count,
                if lines.is_empty() {
                    String::new()
                } else {
                    format!(", lines {}", lines.join(", "))
                }
            ));
            for i in &b.instructions {
                let mut operands: Vec<_> = i.operands.iter().map(|o| o.text()).collect();
                if operands.len() > 12 {
                    let n = operands.len() - 3;
                    operands.truncate(3);
                    operands.push(format!("... {n} more"));
                }
                out.push(format!(
                    "      {} {}",
                    names
                        .get(&i.opcode)
                        .cloned()
                        .unwrap_or_else(|| format!("op{}", i.opcode)),
                    operands.join(", ")
                ));
            }
        }
    }
    out
}
