use crate::trace::{Instruction, ScheduledBlock};
use anyhow::{Context, Result};
use serde_json::{Value, json};
use std::collections::BTreeMap;

fn n(v: &Value, key: &str) -> i64 {
    v[key].as_i64().unwrap_or(0)
}

fn compare(best: &Value, other: &Value, cycle: i64, virtual_: bool) -> (bool, String) {
    if n(best, "latest") <= cycle && n(other, "latest") > cycle {
        return (
            false,
            format!(
                "the pick must issue by this cycle (latest {}), this one need not",
                n(best, "latest")
            ),
        );
    }
    if n(best, "latest") > cycle && n(other, "latest") <= cycle {
        return (
            true,
            format!(
                "this one must issue by this cycle (latest {}), the pick need not",
                n(other, "latest")
            ),
        );
    }
    if n(best, "count") != n(other, "count") {
        return (
            n(other, "count") > n(best, "count"),
            format!(
                "the pick makes {} dependent(s) ready, this one {}",
                n(best, "count"),
                n(other, "count")
            ),
        );
    }
    if n(best, "height") != n(other, "height") {
        return (
            n(other, "height") > n(best, "height"),
            format!(
                "the pick's height (the longest latency path to the end of the block) is {}, this one's {}",
                n(best, "height"),
                n(other, "height")
            ),
        );
    }
    if virtual_ && n(best, "rank") != n(other, "rank") && n(best, "rank") >= 0 {
        return (
            n(other, "rank") < n(best, "rank"),
            format!(
                "the pick's opcode rank is {}, this one's {} (the lower wins)",
                n(best, "rank"),
                n(other, "rank")
            ),
        );
    }
    (false, "a tie: the pick comes first in the block".into())
}

fn text(instruction: &Instruction, names: &BTreeMap<i32, String>) -> String {
    if instruction.operands.len() <= 12 {
        return instruction.text(names);
    }
    let mut short = instruction.clone();
    short.operands.truncate(3);
    format!(
        "{}, ... {} more",
        short.text(names),
        instruction.operands.len() - 3
    )
}

pub fn analyze(blocks: &[ScheduledBlock], names: &BTreeMap<i32, String>) -> Result<Vec<Value>> {
    let mut output = vec![];
    for block in blocks {
        let texts: BTreeMap<_, _> = block
            .instructions
            .iter()
            .map(|i| (i.pointer.as_str(), text(i, names)))
            .collect();
        let positions: BTreeMap<_, _> = block
            .instructions
            .iter()
            .enumerate()
            .map(|(i, v)| (v.pointer.as_str(), i))
            .collect();
        let lines: BTreeMap<_, _> = block
            .instructions
            .iter()
            .map(|i| (i.pointer.as_str(), i.line))
            .collect();
        let mut preds: BTreeMap<&str, Vec<(&str, i64)>> = BTreeMap::new();
        for (pointer, node) in &block.nodes {
            for successor in node["succ"]
                .as_array()
                .context("scheduler successors missing")?
            {
                preds
                    .entry(
                        successor[0]
                            .as_str()
                            .context("scheduler successor missing pointer")?,
                    )
                    .or_default()
                    .push((
                        pointer,
                        successor[1].as_i64().context("scheduler latency missing")?,
                    ));
            }
        }
        let mut issued: BTreeMap<&str, i64> = BTreeMap::new();
        let mut picks = vec![];
        let mut agree = 0;
        for pick in &block.picks {
            let cycle = n(pick, "cycle");
            let ready = pick["ready"]
                .as_array()
                .context("scheduler pick missing candidates")?;
            let mut predicted: Option<&Value> = None;
            for r in ready
                .iter()
                .filter(|r| n(r, "earliest") <= cycle && n(r, "check") != 0)
            {
                if predicted.is_none_or(|p| compare(p, r, cycle, block.virtual_).0) {
                    predicted = Some(r);
                }
            }
            let predicted_pointer = predicted.and_then(|r| r["i"].as_str());
            let picked = pick["pick"].as_str();
            let agrees = predicted_pointer == picked;
            if agrees {
                agree += 1;
            }
            let Some(picked) = picked else {
                continue;
            };
            let winner = ready
                .iter()
                .find(|r| r["i"] == picked)
                .context("scheduler picked an absent candidate")?;
            issued.insert(picked, cycle);
            let mut others = vec![];
            for r in ready.iter().filter(|r| r["i"] != picked) {
                let pointer = r["i"]
                    .as_str()
                    .context("scheduler candidate missing pointer")?;
                let why = if n(r, "earliest") > cycle {
                    let waits = preds.get(pointer).and_then(|p| {
                        p.iter().find(|(p, latency)| {
                            issued.get(p).copied().unwrap_or(0) + latency == n(r, "earliest")
                        })
                    });
                    let mut why = format!("not ready until cycle {}", n(r, "earliest"));
                    if let Some((pointer, _)) = waits {
                        why += &format!(
                            " (it waits {} cycle(s) for #{} {})",
                            n(r, "earliest") - issued.get(pointer).copied().unwrap_or(0),
                            positions.get(pointer).map_or("?".into(), |n| n.to_string()),
                            texts.get(pointer).map_or("?", String::as_str)
                        );
                    }
                    why
                } else if n(r, "check") == 0 {
                    "the machine cannot issue it this cycle (its unit is busy)".into()
                } else {
                    let (wins, criterion) = compare(winner, r, cycle, block.virtual_);
                    if wins {
                        format!("lost to an earlier comparison ({criterion})")
                    } else {
                        criterion
                    }
                };
                others.push(json!({"i":pointer,"position":positions.get(pointer),"why":why}));
            }
            picks.push(json!({"cycle":cycle,"i":picked,"position":positions.get(picked),"line":lines.get(picked).copied().flatten(),"instruction":texts.get(picked).map_or("?",String::as_str),
                "latest":winner["latest"],"height":winner["height"],"count":winner["count"],"rank":winner["rank"],"agrees":agrees,"over":others}));
        }
        let mut edges = vec![];
        for (p, node) in &block.nodes {
            for s in node["succ"]
                .as_array()
                .context("scheduler successors missing")?
            {
                edges.push(json!({"from":positions.get(p.as_str()),"to":positions.get(s[0].as_str().context("scheduler successor missing pointer")?),"latency":s[1]}));
            }
        }
        output.push(json!({"block":block.index,"virtual":block.virtual_,"instructions":block.instructions.len(),"cycles":block.picks.last().map_or(0,|p|n(p,"cycle")+1),
            "replay_agrees":agree,"replayed":block.picks.len(),"picks":picks,"before":block.instructions.iter().map(|i|&texts[i.pointer.as_str()]).collect::<Vec<_>>(),"edges":edges}));
    }
    Ok(output)
}
