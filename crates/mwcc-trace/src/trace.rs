use crate::version::class_name;
use anyhow::{Context, Result, anyhow, ensure};
use regex::Regex;
use serde::{Deserialize, Serialize};
use serde_json::{Value, json};
use std::collections::BTreeMap;
use std::sync::LazyLock;

#[derive(Clone, Debug, PartialEq, Eq, Serialize, Deserialize)]
pub struct Operand {
    pub raw: [u8; 12],
}

impl Operand {
    pub fn kind(&self) -> u8 {
        self.raw[0]
    }
    pub fn class(&self) -> String {
        class_name(self.raw[1])
    }
    pub fn register(&self) -> u32 {
        u16::from_le_bytes([self.raw[4], self.raw[5]]) as u32
    }
    pub fn effect(&self) -> u16 {
        u16::from_le_bytes([self.raw[2], self.raw[3]])
    }
    pub fn is_register(&self) -> bool {
        self.kind() == 0
    }
    pub fn uses(&self) -> bool {
        self.is_register() && self.effect() & 1 != 0
    }
    pub fn defines(&self) -> bool {
        self.is_register() && self.effect() & 2 != 0
    }
    pub fn text(&self) -> String {
        if self.is_register() {
            let class = self.class();
            let prefix = match class.as_str() {
                "GPR" => "r",
                "FPR" => "f",
                "VR" => "v",
                "CR" => "cr",
                "SPR" => "spr",
                _ => "?",
            };
            let virtual_ =
                self.register() >= 32 && matches!(class.as_str(), "GPR" | "FPR" | "VR" | "CR");
            return format!(
                "{}{prefix}{}",
                if virtual_ { "%" } else { "" },
                self.register()
            );
        }
        match self.kind() {
            2 => format!(
                "#{}",
                i32::from_le_bytes(self.raw[2..6].try_into().unwrap())
            ),
            3 => "sym".into(),
            4 => "label".into(),
            6 => "-".into(),
            k => format!("k{k}"),
        }
    }
}

#[derive(Clone, Debug, Serialize, Deserialize)]
pub struct Instruction {
    pub pointer: String,
    pub opcode: i32,
    pub flags: u32,
    pub operands: Vec<Operand>,
    pub line: Option<i32>,
}

impl Instruction {
    pub fn text(&self, names: &BTreeMap<i32, String>) -> String {
        format!(
            "{} {}",
            names
                .get(&self.opcode)
                .cloned()
                .unwrap_or_else(|| format!("op{}", self.opcode)),
            self.operands
                .iter()
                .map(Operand::text)
                .collect::<Vec<_>>()
                .join(", ")
        )
    }
}

#[derive(Clone, Debug, Serialize, Deserialize)]
pub struct Block {
    pub pointer: String,
    pub index: i32,
    pub weight: i32,
    pub successors: Vec<String>,
    pub instructions: Vec<Instruction>,
}

#[derive(Clone, Debug, Serialize, Deserialize)]
pub struct Node {
    pub pointer: String,
    pub degree: i32,
    pub flags: u32,
    pub color: i32,
    pub neighbors: Vec<u32>,
}

#[derive(Clone, Debug, Default, Serialize, Deserialize)]
pub struct Round {
    pub class: u8,
    pub k: i32,
    pub start: u32,
    pub count: u32,
    pub nodes: BTreeMap<u32, Node>,
    pub order: Vec<(u32, i64)>,
    pub mask: Option<u32>,
    pub mask_after: Option<u32>,
    pub ok: Option<bool>,
    pub colors: BTreeMap<u32, (i32, u32)>,
    /// ra.select's order is a legacy report contract (not necessarily numeric).
    pub color_order: Vec<u32>,
}

#[derive(Clone, Debug, Serialize, Deserialize)]
pub struct ScheduledBlock {
    pub pointer: String,
    pub index: i32,
    pub virtual_: bool,
    pub instructions: Vec<Instruction>,
    pub nodes: Vec<(String, Value)>,
    pub picks: Vec<Value>,
    pub after: Vec<String>,
}

#[derive(Clone, Debug, Serialize, Deserialize)]
pub struct Snapshot {
    pub function: String,
    pub link: String,
    pub blocks: Vec<Block>,
    pub rounds: Vec<Round>,
    pub frame: Option<Vec<Value>>,
    pub layout: Option<Value>,
    pub schedule: Vec<ScheduledBlock>,
}

impl Snapshot {
    fn new(function: String, link: Option<&String>) -> Self {
        let link = link
            .filter(|s| !s.is_empty())
            .cloned()
            .unwrap_or_else(|| function.clone());
        Self {
            function,
            link,
            blocks: vec![],
            rounds: vec![],
            frame: None,
            layout: None,
            schedule: vec![],
        }
    }
}

#[derive(Clone, Debug, Serialize, Deserialize)]
pub struct Allocation {
    pub before: Snapshot,
    pub after: Snapshot,
    pub inliner_position: usize,
}

#[derive(Clone, Debug, Serialize, Deserialize)]
pub struct Record {
    pub line: usize,
    pub kind: String,
    pub fields: BTreeMap<String, String>,
    pub raw: String,
}

impl Record {
    pub fn get(&self, key: &str) -> Result<&str> {
        self.fields
            .get(key)
            .map(String::as_str)
            .ok_or_else(|| anyhow!("{} missing {key}", self.kind))
    }
    pub fn number<T: std::str::FromStr>(&self, key: &str) -> Result<T> {
        self.get(key)?
            .parse()
            .map_err(|_| anyhow!("{} invalid number for {key}", self.kind))
    }
    fn hex(&self, key: &str) -> Result<u32> {
        u32::from_str_radix(self.get(key)?, 16)
            .with_context(|| format!("{} invalid hex for {key}", self.kind))
    }
}

#[derive(Clone, Debug, Default, Serialize, Deserialize)]
pub struct Trace {
    pub allocations: Vec<Allocation>,
    pub inliner: Vec<Record>,
    /// All input records, including unknown families, in their original order.
    pub records: Vec<Record>,
}

/// The same first token used by `records`, without allocating field maps.
pub fn record_kind(line: &str) -> Option<&str> {
    line.split_whitespace().next()
}
pub fn records(text: &str) -> Vec<Record> {
    static KEYS: LazyLock<Regex> = LazyLock::new(|| Regex::new(r"(?:^|\s)(\w+)=").unwrap());
    text.lines()
        .enumerate()
        .filter(|(_, l)| !l.trim().is_empty())
        .map(|(index, line)| {
            let kind = record_kind(line).unwrap().to_owned();
            let captures: Vec<_> = KEYS.captures_iter(line).collect();
            let mut fields = BTreeMap::new();
            for (n, c) in captures.iter().enumerate() {
                let end = captures
                    .get(n + 1)
                    .map_or(line.len(), |next| next.get(0).unwrap().start());
                fields.insert(
                    c[1].to_owned(),
                    line[c.get(0).unwrap().end()..end].trim().to_owned(),
                );
            }
            Record {
                line: index + 1,
                kind,
                fields,
                raw: line.to_owned(),
            }
        })
        .collect()
}

fn list(text: &str) -> Vec<String> {
    text.split(',')
        .filter(|s| !s.is_empty())
        .map(str::to_owned)
        .collect()
}

pub fn instruction(record: &Record) -> Result<Instruction> {
    let operands = record
        .get("ops")?
        .split_whitespace()
        .map(|hex| {
            ensure!(
                hex.len() == 24 && hex.is_ascii(),
                "operand must contain 12 bytes"
            );
            let mut raw = [0; 12];
            for (i, b) in raw.iter_mut().enumerate() {
                *b = u8::from_str_radix(&hex[2 * i..2 * i + 2], 16)
                    .context("invalid operand hex")?;
            }
            Ok(Operand { raw })
        })
        .collect::<Result<Vec<_>>>()?;
    ensure!(
        operands.len() == record.number::<usize>("n")?,
        "operand count mismatch"
    );
    Ok(Instruction {
        pointer: record.get("p")?.into(),
        opcode: record.number("op")?,
        flags: record.hex("flags")?,
        operands,
        line: record
            .fields
            .get("line")
            .map(|_| record.number("line"))
            .transpose()?,
    })
}

fn pointer_index(round: &Round, pointer: &str) -> Result<u32> {
    round
        .nodes
        .iter()
        .find_map(|(i, n)| (n.pointer == pointer).then_some(*i))
        .ok_or_else(|| anyhow!("unknown graph node pointer {pointer}"))
}

/// Parse known allocation/scheduling/frame records strictly. Optional event
/// families remain lossless records until their analysis modules consume them.
pub fn parse(text: &str) -> Result<Trace> {
    let mut trace = Trace {
        records: records(text),
        ..Trace::default()
    };
    let mut before: Option<Snapshot> = None;
    let mut current: Option<(bool, Snapshot)> = None;
    let mut rounds: Vec<Round> = vec![];
    let mut frame: Option<Vec<Value>> = None;
    let mut layout: Option<Value> = None;
    let mut scheduled: Option<ScheduledBlock> = None;
    let mut schedule_pending = vec![];
    for record in &trace.records {
        // Keep line numbers on errors without panicking on malformed input.
        let result = (|| -> Result<()> {
            match record.kind.as_str() {
                "ra.before" | "ra.after" => {
                    ensure!(current.is_none(), "snapshot started before ra.end");
                    let is_before = record.kind == "ra.before";
                    if is_before {
                        ensure!(before.is_none(), "unpaired before snapshot");
                        rounds.clear();
                    } else {
                        ensure!(before.is_some(), "after snapshot without before");
                    }
                    let mut snapshot =
                        Snapshot::new(record.get("function")?.into(), record.fields.get("link"));
                    if is_before {
                        snapshot.schedule = std::mem::take(&mut schedule_pending);
                    }
                    current = Some((is_before, snapshot));
                }
                "ra.block" => {
                    let snapshot = &mut current.as_mut().context("block outside snapshot")?.1;
                    let pointer = record.get("p")?.to_owned();
                    ensure!(
                        !snapshot.blocks.iter().any(|b| b.pointer == pointer),
                        "duplicate block {pointer}"
                    );
                    snapshot.blocks.push(Block {
                        pointer,
                        index: record.number("index")?,
                        weight: record.number("weight")?,
                        successors: list(record.get("succ")?),
                        instructions: vec![],
                    });
                }
                "ra.i" => {
                    current
                        .as_mut()
                        .context("instruction outside snapshot")?
                        .1
                        .blocks
                        .last_mut()
                        .context("instruction before first block")?
                        .instructions
                        .push(instruction(record)?);
                }
                "ra.end" => {
                    let (is_before, mut snapshot) =
                        current.take().context("ra.end without snapshot")?;
                    if is_before {
                        before = Some(snapshot);
                    } else {
                        let mut b = before.take().context("after snapshot without before")?;
                        ensure!(
                            b.function == snapshot.function && b.link == snapshot.link,
                            "snapshot function mismatch"
                        );
                        b.rounds = std::mem::take(&mut rounds);
                        // Only the before snapshot owns rounds/frame/scheduling.
                        snapshot.rounds.clear();
                        trace.allocations.push(Allocation {
                            before: b,
                            after: snapshot,
                            inliner_position: trace.inliner.len(),
                        });
                    }
                }
                "ra.graph" => rounds.push(Round {
                    class: record.number("cls")?,
                    k: record.number("k")?,
                    start: record.number("start")?,
                    count: record.number("count")?,
                    ..Round::default()
                }),
                "ra.node" => {
                    let r = rounds.last_mut().context("node before graph")?;
                    let index = record.number("v")?;
                    let node = Node {
                        pointer: record.get("p")?.into(),
                        degree: record.number("deg")?,
                        flags: record.hex("flags")?,
                        color: record.number("color")?,
                        neighbors: list(record.get("n")?)
                            .iter()
                            .map(|x| x.parse().context("invalid neighbor"))
                            .collect::<Result<_>>()?,
                    };
                    ensure!(
                        r.nodes.insert(index, node).is_none(),
                        "duplicate graph node {index}"
                    );
                }
                "ra.order" | "ra.select" => {
                    let r = rounds.last_mut().context("order/select before graph")?;
                    ensure!(
                        r.class == record.number::<u8>("cls")?,
                        "register class mismatch"
                    );
                    if record.kind == "ra.order" {
                        for item in list(record.get("v")?) {
                            let (pointer, cost) =
                                item.split_once(':').context("invalid order entry")?;
                            r.order.push((
                                pointer_index(r, pointer)?,
                                cost.parse().context("invalid spill cost")?,
                            ));
                        }
                    } else {
                        r.mask = Some(record.hex("mask")?);
                        r.mask_after = Some(record.hex("after")?);
                        let ok = record.number::<u8>("ok")?;
                        ensure!(ok <= 1, "invalid select status");
                        r.ok = Some(ok == 1);
                        for item in list(record.get("v")?) {
                            let fields: Vec<_> = item.split(':').collect();
                            ensure!(fields.len() == 3, "invalid select entry");
                            let i = pointer_index(r, fields[0])?;
                            ensure!(!r.colors.contains_key(&i), "duplicate selected graph node");
                            r.color_order.push(i);
                            r.colors.insert(
                                i,
                                (
                                    fields[1].parse().context("invalid color")?,
                                    u32::from_str_radix(fields[2], 16)?,
                                ),
                            );
                        }
                    }
                }
                "fr.begin" => {
                    ensure!(frame.is_none(), "nested frame");
                    frame = Some(vec![]);
                    layout = None;
                }
                "fr.layout" => {
                    ensure!(frame.is_some(), "layout outside frame");
                    let mut value = serde_json::Map::new();
                    for key in ["linkage", "outgoing", "locals", "above", "size"] {
                        value.insert(key.into(), json!(record.number::<i64>(key)?));
                    }
                    layout = Some(Value::Object(value));
                }
                "fr.local" | "fr.temp" => {
                    let used = record.fields.get("used").is_none_or(|v| v != "0");
                    frame.as_mut().context("local outside frame")?.push(json!({"kind": record.kind.trim_start_matches("fr."),
                        "name": record.get("name")?, "offset": record.number::<i64>("offset")?, "size": record.number::<i64>("size")?,
                        "reg": record.number::<i64>("reg")?, "used": used}));
                }
                "fr.end" => {
                    let mut entries = frame.take().context("fr.end without frame")?;
                    let allocation = trace
                        .allocations
                        .last_mut()
                        .context("frame before allocation")?;
                    if let Some(l) = &layout {
                        for entry in &mut entries {
                            if entry["used"] == true && entry["reg"] == 0 {
                                entry["r1"] = json!(
                                    l["linkage"].as_i64().unwrap()
                                        + l["outgoing"].as_i64().unwrap()
                                        + entry["offset"].as_i64().unwrap()
                                );
                            }
                        }
                    }
                    allocation.before.frame = Some(entries);
                    allocation.before.layout = layout.take();
                }
                "sc.block" => {
                    ensure!(scheduled.is_none(), "scheduler block before sc.after");
                    scheduled = Some(ScheduledBlock {
                        pointer: record.get("p")?.into(),
                        index: record.number("index")?,
                        virtual_: record.get("virtual")? != "0",
                        instructions: vec![],
                        nodes: vec![],
                        picks: vec![],
                        after: vec![],
                    });
                }
                "sc.i" => scheduled
                    .as_mut()
                    .context("scheduled instruction outside block")?
                    .instructions
                    .push(instruction(record)?),
                "sc.node" => {
                    let mut successors = vec![];
                    for item in list(record.get("succ")?) {
                        let (p, latency) = item
                            .split_once(':')
                            .context("invalid scheduler successor")?;
                        successors.push(json!([p, latency.parse::<i32>()?]));
                    }
                    let value = json!({"latency": record.number::<i32>("latency")?, "height": record.number::<i32>("height")?,
                        "latest": record.number::<i32>("latest")?, "preds": record.number::<i32>("preds")?, "succ": successors});
                    let nodes = &mut scheduled
                        .as_mut()
                        .context("scheduler node outside block")?
                        .nodes;
                    let pointer = record.get("i")?;
                    ensure!(
                        !nodes.iter().any(|(p, _)| p == pointer),
                        "duplicate scheduler node"
                    );
                    nodes.push((pointer.into(), value));
                }
                "sc.pick" => {
                    let mut ready = vec![];
                    for item in list(record.get("ready")?) {
                        let f: Vec<_> = item.split(':').collect();
                        ensure!(f.len() == 7, "invalid ready candidate");
                        ready.push(json!({"i": f[0], "earliest": f[1].parse::<i32>()?, "latest": f[2].parse::<i32>()?,
                            "height": f[3].parse::<i32>()?, "count": f[4].parse::<i32>()?, "check": f[5].parse::<i32>()?, "rank": f[6].parse::<i32>()?}));
                    }
                    let pointer = record.get("pick")?;
                    let pick = if u32::from_str_radix(pointer, 16)? == 0 {
                        Value::Null
                    } else {
                        json!(pointer)
                    };
                    scheduled.as_mut().context("pick outside scheduled block")?.picks.push(json!({"cycle": record.number::<u32>("cycle")?, "ready": ready, "pick": pick}));
                }
                "sc.after" => {
                    let mut block = scheduled.take().context("sc.after without block")?;
                    block.after = list(record.get("order")?);
                    if block.virtual_ || trace.allocations.is_empty() {
                        schedule_pending.push(block);
                    } else {
                        trace
                            .allocations
                            .last_mut()
                            .unwrap()
                            .before
                            .schedule
                            .push(block);
                    }
                }
                kind if kind.starts_with("inl.") => trace.inliner.push(record.clone()),
                _ => (),
            }
            Ok(())
        })();
        result.with_context(|| format!("trace line {} ({})", record.line, record.kind))?;
    }
    ensure!(
        current.is_none() && before.is_none(),
        "truncated trace: unfinished allocation snapshot"
    );
    ensure!(frame.is_none(), "truncated trace: unfinished frame");
    ensure!(
        scheduled.is_none() && schedule_pending.is_empty(),
        "truncated trace: unfinished scheduling"
    );
    ensure!(
        rounds.is_empty(),
        "truncated trace: graph without completed allocation"
    );
    Ok(trace)
}

pub fn raw_for(text: &str, wanted: Option<&str>) -> Vec<String> {
    raw_selected(text, wanted, None)
}
pub fn raw_selected(text: &str, wanted: Option<&str>, link: Option<&str>) -> Vec<String> {
    static HEADER: LazyLock<Regex> = LazyLock::new(|| {
        Regex::new(r"^ra\.(before|after) function=(\S*)(?: link=(.*))?$").unwrap()
    });
    let mut out = vec![];
    let mut group = vec![];
    let mut name: Option<(String, String)> = None;
    // 0: before/in-between, 1: inside ra.after, 2: its ra.end was seen.
    let mut after_done = 0;
    let close = |group: &[String], name: &Option<(String, String)>, out: &mut Vec<String>| {
        if let Some((function, link_name)) = name
            && wanted.is_none_or(|w| function.contains(w) || link_name.contains(w))
            && link.is_none_or(|l| link_name == l)
        {
            out.extend_from_slice(group);
        }
    };
    for line in text.lines() {
        if after_done == 2 && !line.starts_with("fr.") {
            close(&group, &name, &mut out);
            group.clear();
            name = None;
            after_done = 0;
        }
        group.push(line.to_owned());
        if let Some(c) = HEADER.captures(line) {
            name = Some((
                c[2].to_owned(),
                c.get(3).map_or(String::new(), |m| m.as_str().to_owned()),
            ));
            if &c[1] == "after" {
                after_done = 1;
            }
        } else if line == "ra.end" && after_done == 1 {
            after_done = 2;
        } else if line == "fr.end" && after_done == 2 {
            close(&group, &name, &mut out);
            group.clear();
            name = None;
            after_done = 0;
        }
    }
    if after_done == 2 {
        close(&group, &name, &mut out);
    }
    out
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn malformed_and_truncated_records_are_errors() {
        for input in [
            "ra.before function=f link=f\n",
            "ra.i p=1 op=1 flags=0 n=1 ops=00",
            "ra.graph cls=4 k=8 start=32 count=33\nra.order cls=4 v=bad:0",
            "fr.end",
        ] {
            assert!(parse(input).is_err(), "accepted {input}");
        }
    }
    #[test]
    fn unknown_records_and_empty_values_survive() {
        let trace = parse("future.event text=a sentence with spaces empty=\n").unwrap();
        assert_eq!(trace.records[0].fields["text"], "a sentence with spaces");
        assert_eq!(trace.records[0].fields["empty"], "");
    }
    #[test]
    fn operand_uses_little_endian_compiler_layout() {
        let op = Operand {
            raw: [0, 4, 3, 0, 32, 0, 0, 0, 0, 0, 0, 0],
        };
        assert!(op.uses() && op.defines());
        assert_eq!(op.text(), "%r32");
    }
}
