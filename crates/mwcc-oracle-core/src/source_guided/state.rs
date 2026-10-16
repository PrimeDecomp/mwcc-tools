//! Relevant compiler-state normalization. Ordered instructions and raw adjacency
//! retain multiplicity; opaque compiler pointers never become semantic anchors.
use crate::{
    elf,
    graph_history::Graph,
    provenance::Report,
    trace::{Block, Instruction, Node, Round, Snapshot},
    version::Version,
};
use anyhow::{Context, Result, ensure};
use serde::{Deserialize, Serialize};
use serde_json::{Value, json};
use sha2::{Digest, Sha256};
use std::collections::BTreeMap;

pub fn digest(bytes: &[u8]) -> String {
    format!("{:x}", Sha256::digest(bytes))
}
pub fn graph<'a>(report: &'a Report, link: &str, class: u8, round: usize) -> Result<&'a Graph> {
    let functions: Vec<_> = report
        .history
        .functions
        .iter()
        .filter(|f| f.link == link)
        .collect();
    ensure!(
        functions.len() == 1,
        "function link must identify one captured function"
    );
    report
        .graphs
        .graphs
        .iter()
        .filter(|g| g.function == functions[0].id && g.class == class)
        .nth(round)
        .context("requested graph round is unavailable")
}
pub fn snapshot(g: &Graph) -> Snapshot {
    Snapshot {
        function: g.link.clone(),
        link: g.link.clone(),
        blocks: vec![Block {
            pointer: String::new(),
            index: 0,
            weight: 0,
            successors: vec![],
            instructions: g.instructions.clone(),
        }],
        rounds: vec![Round {
            class: g.class,
            start: g.start,
            count: g.count,
            nodes: g
                .nodes
                .iter()
                .map(|(r, n)| {
                    (
                        *r,
                        Node {
                            pointer: String::new(),
                            degree: n.stored_degree,
                            flags: n.flags,
                            color: n.color,
                            neighbors: n.recorded_neighbors.clone(),
                        },
                    )
                })
                .collect(),
            ..Round::default()
        }],
        frame: None,
        layout: None,
        schedule: vec![],
    }
}
#[derive(Default)]
struct Names {
    registers: BTreeMap<(u8, u32), u32>,
    objects: BTreeMap<u32, u32>,
    labels: BTreeMap<u32, u32>,
}
fn ordinal(map: &mut BTreeMap<u32, u32>, key: u32) -> u32 {
    if key == 0 {
        return 0;
    }
    let next = map.len() as u32 + 1;
    *map.entry(key).or_insert(next)
}
impl Names {
    fn register(&mut self, class: u8, register: u32) -> u32 {
        let physical = match class {
            0 => 4,
            1 => 8,
            _ => 32,
        };
        if register < physical {
            return register;
        }
        let next = physical + self.registers.keys().filter(|(c, _)| *c == class).count() as u32;
        *self.registers.entry((class, register)).or_insert(next)
    }
    fn instruction(&mut self, i: &Instruction) -> Result<Value> {
        let mut operands = vec![];
        for operand in &i.operands {
            let mut raw = operand.raw;
            let pointer = |raw: &[u8], at| u32::from_le_bytes(raw[at..at + 4].try_into().unwrap());
            match raw[0] {
                0 => {
                    ensure!(raw[1] < 5, "unknown register class");
                    let r = self.register(raw[1], operand.register());
                    raw[4..6].copy_from_slice(&(r as u16).to_le_bytes());
                }
                4 => {
                    let label = ordinal(&mut self.labels, pointer(&raw, 2));
                    raw[2..6].copy_from_slice(&label.to_le_bytes());
                }
                7 => {
                    for at in [4, 8] {
                        let label = ordinal(&mut self.labels, pointer(&raw, at));
                        raw[at..at + 4].copy_from_slice(&label.to_le_bytes());
                    }
                }
                2 | 3 | 6 => {}
                _ => anyhow::bail!("unsupported operand kind in instruction-state comparison"),
            }
            if matches!(raw[0], 0 | 2 | 3 | 4) {
                let object = ordinal(&mut self.objects, pointer(&raw, 6));
                raw[6..10].copy_from_slice(&object.to_le_bytes());
            }
            operands.push(raw);
        }
        Ok(json!({"opcode":i.opcode,"flags":i.flags,"operands":operands}))
    }
}
pub fn instructions(g: &Graph) -> Result<Value> {
    let mut names = Names::default();
    Ok(json!(
        g.instructions
            .iter()
            .map(|i| names.instruction(i))
            .collect::<Result<Vec<_>>>()?
    ))
}
#[derive(Clone, Debug, PartialEq, Eq, Serialize, Deserialize)]
pub struct SymbolicRelocation {
    pub offset: u32,
    pub kind: u8,
    pub symbol: String,
    pub addend: i32,
}
pub fn relocations(object: &[u8], link: &str) -> Result<Vec<SymbolicRelocation>> {
    let elf = elf::Elf::parse(object)?;
    let functions: Vec<_> = elf
        .symbols()?
        .into_iter()
        .filter(|s| s.kind == 2 && s.name == link)
        .collect();
    ensure!(functions.len() == 1, "ELF link must identify one function");
    let f = &functions[0];
    let mut relocations = elf
        .relocations()?
        .into_iter()
        .filter(|r| r.section == f.section && (f.value..f.value + f.size).contains(&r.offset))
        .map(|r| SymbolicRelocation {
            offset: r.offset - f.value,
            kind: r.kind,
            symbol: r.symbol.name,
            addend: r.addend,
        })
        .collect::<Vec<_>>();
    relocations.sort_by(|a, b| {
        (a.offset, a.kind, &a.symbol, a.addend).cmp(&(b.offset, b.kind, &b.symbol, b.addend))
    });
    Ok(relocations)
}
/// Function-level object instructions preserve control-flow displacements and
/// immediates. A consistent injective register renaming is the only normalization.
pub fn object_instructions(object: &[u8], link: &str) -> Result<Value> {
    let elf = elf::Elf::parse(object)?;
    let functions: Vec<_> = elf
        .symbols()?
        .into_iter()
        .filter(|s| s.kind == 2 && s.name == link)
        .collect();
    ensure!(functions.len() == 1, "ELF link must identify one function");
    let bytes = elf.function_bytes(&functions[0])?;
    ensure!(bytes.len().is_multiple_of(4), "unaligned PowerPC function");
    let mut names = BTreeMap::<(String, u32), u32>::new();
    let mut words = vec![];
    for raw in bytes.chunks_exact(4) {
        let word = u32::from_be_bytes(raw.try_into()?);
        let mut normalized = word;
        if let Some(fields) = crate::colouring::target::register_fields(word) {
            for (class, field, register, _) in fields {
                let next = names.keys().filter(|(c, _)| c == class).count() as u32;
                let id = *names.entry((class.to_owned(), register)).or_insert(next);
                let shift = crate::colouring::target::field_shift(field);
                normalized = (normalized & !(31 << shift)) | (id << shift);
            }
        }
        words.push(normalized);
    }
    Ok(json!({"words":words,"relocations":relocations(object,link)?}))
}
/// Conservative fingerprint: instruction-visible registers get canonical IDs;
/// graph-only registers retain their numbers to avoid equating non-isomorphic
/// graphs. This can miss duplicate states but cannot merge solely on WL hashes.
pub fn fingerprint(g: &Graph, report: &Report, object: &[u8], version: Version) -> Result<String> {
    let mut names = Names::default();
    let instruction_state = g
        .instructions
        .iter()
        .map(|i| names.instruction(i))
        .collect::<Result<Vec<_>>>()?;
    let rename = |r: &u32| {
        if *r < g.physical_limit {
            json!(["physical", r])
        } else if let Some(id) = names.registers.get(&(g.class, *r)) {
            json!(["visible", id])
        } else {
            json!(["graph_only", r])
        }
    };
    let mut nodes=g.nodes.iter().map(|(r,n)| {
        let neighbors=n.recorded_neighbors.iter().map(&rename).collect::<Vec<_>>();
        let f=report.history.functions.iter().find(|f|f.id==g.function).unwrap();
        let origins=n.binding.direct.iter().chain(&n.binding.contributors).map(|id| {
            let b=&f.births[(id.sequence-1) as usize];
            json!({"site":b.site,"phase":b.phase,"generation":b.generation,"object":b.object.as_ref().map(|o|json!({"part":o.part,"datatype":o.datatype,"storage":o.storage,"type_kind":o.type_kind,"type_size":o.type_size,"name":o.name_bytes,"complete":o.name_complete,"known":o.name_known})),
                "expression":b.expression.and_then(|id|f.expressions.iter().find(|e|e.id==id)).map(|e|json!({"scope":e.scope,"complete":e.complete,"tree":e.tree_bytes}))})
        }).collect::<Vec<_>>();
        json!({"id":rename(r),"degree":n.stored_degree,"flags":n.flags,"color":n.color,"neighbors":neighbors,"category":n.range_category,
            "origins":origins,"binding":n.binding.evidence,"visible":n.visible_in_instructions})
    }).collect::<Vec<_>>();
    nodes.sort_by_key(|n| n["id"].to_string());
    let elf = elf::Elf::parse(object)?;
    let relocations = elf.relocations()?;
    let data=elf.sections.iter().enumerate().filter(|(_,s)|s.flags&2!=0 && s.flags&4==0)
        .map(|(index,s)|json!({"name":s.name,"kind":s.kind,"size":s.size,"alignment":s.alignment,"bytes":s.data,
            "relocations":relocations.iter().filter(|r|r.section==index).map(|r|json!({"offset":r.offset,"kind":r.kind,"symbol":r.symbol.name,"addend":r.addend})).collect::<Vec<_>>() })).collect::<Vec<_>>();
    let value = json!({"format_version":1,"version":version,"class":g.class,"boundary":g.boundary,"phase":g.phase,
        "physical":g.physical_limit,"start":g.start,"count":g.count,"observations":g.observations,"instructions":instruction_state,"nodes":nodes,
        "bounds":g.range_bounds.as_ref().map(|b|json!({"physical":b.physical_limit,"initial_last":b.initial_last,"first":b.first,"last":b.last,"evidence":b.evidence})),
        "object":object_instructions(object,&g.link)?,"allocated_data":data});
    Ok(digest(&serde_json::to_vec(&value)?))
}
