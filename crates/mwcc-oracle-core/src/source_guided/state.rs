//! Relevant compiler-state normalization. Ordered instructions and raw adjacency
//! retain multiplicity; opaque compiler pointers never become semantic anchors.
use crate::{
    elf,
    evidence::{Evaluation, EvaluationState, EvidenceGap},
    graph_history::Graph,
    provenance::Report,
    runner::{CheckStatus, Compilation},
    trace::{Block, Instruction, Node, Round, Snapshot},
};
use anyhow::{Context, Result, ensure};
use serde::{Deserialize, Serialize};
use serde_json::{Value, json};
use std::collections::BTreeMap;
mod call_arguments;
mod definitions;

pub const FINGERPRINT_FORMAT_VERSION: u32 = 4;

pub use crate::hash::sha256_hex as digest;
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
                1 => {
                    // Inline PSQ_L/PSQ_ST retain a fixed GQR read in a
                    // non-allocatable operand, distinct from kind-0 virtual
                    // registers. All eight selectors were observed with this
                    // exact shape on each supported retail version. Preserve
                    // every byte: GQR identity must never be alpha-renamed.
                    ensure!(
                        matches!(i.opcode, 403 | 407)
                            && raw[..4] == [1, 0, 1, 0]
                            && (912..=919).contains(&u16::from_le_bytes(raw[4..6].try_into()?))
                            && raw[6..] == [0; 6],
                        "unsupported fixed-quantizer operand form at opcode {}",
                        i.opcode
                    );
                }
                7 => {
                    for at in [4, 8] {
                        let label = ordinal(&mut self.labels, pointer(&raw, at));
                        raw[at..at + 4].copy_from_slice(&label.to_le_bytes());
                    }
                }
                2 | 3 | 6 => {}
                _ => anyhow::bail!(
                    "unsupported PCode operand kind {} at opcode {} in instruction-state comparison",
                    raw[0],
                    i.opcode
                ),
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
pub fn fingerprint(g: &Graph, report: &Report, compilation: &Compilation) -> Result<String> {
    ensure!(
        !compilation.trace_options.source_arenas
            && !compilation.trace_options.source_nodes
            && !compilation.trace.contains("pv.arena.")
            && !compilation.trace.contains("pv.source.")
            && report.source_arenas.events.is_empty(),
        "source arena capture normalization is not supported by this fingerprint format"
    );
    ensure!(
        report.trace_sha256 == digest(compilation.trace.as_bytes()),
        "fingerprint provenance is not bound to the exact compiler trace"
    );
    let object = &compilation.object;
    let version = compilation.version;
    let definitions = definitions::normalize(g, report, compilation)?;
    let arguments = call_arguments::normalize(g, report, compilation)?;
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
    let function = report
        .history
        .functions
        .iter()
        .find(|f| f.id == g.function)
        .context("missing fingerprint function history")?;
    let scalarization = crate::scalarization::normalized_function(&report.scalarization, function)?;
    let scalar_origins=report.scalarization.backend_values.iter().map(|value| {
        let binding=value.pass.zip(value.binding).and_then(|(pass,ordinal)|report.scalarization.passes.iter().find(|p|p.id==pass).and_then(|p|p.bindings.iter().find(|b|b.ordinal==ordinal)));
        let normalized=binding.map(|b|json!({"name":b.source.name_bytes,"name_known":b.source.name_known,"name_complete":b.source.name_complete,"datatype":b.source.datatype,"storage":b.source.storage,"source_type_kind":b.source.type_kind,"source_type_size":b.source.type_size,"element_known":b.source.element_known,"element_type_kind":b.source.element_type_kind,"element_type_size":b.source.element_type_size,"byte_offset":b.byte_offset,"byte_size":b.byte_size,"bit_first":b.bit_first,"bit_last":b.bit_last,"flags":b.flags&3,"source_kind":b.source_kind,"temporary_type_kind":b.temporary.type_kind,"temporary_type_size":b.temporary.type_size}));
        (value.value,json!({"origin":normalized,"array_element":value.array_element,"observations":value.observations}))
    }).collect::<BTreeMap<_,_>>();
    let f = report
        .history
        .functions
        .iter()
        .find(|f| f.id == g.function)
        .unwrap();
    let mut nodes=g.nodes.iter().map(|(r,n)| {
        let neighbors=n.recorded_neighbors.iter().map(&rename).collect::<Vec<_>>();
        let origins=n.binding.direct.iter().chain(&n.binding.contributors).map(|id| {
            let b=&f.births[(id.sequence-1) as usize];
            json!({"site":b.site,"phase":b.phase,"generation":b.generation,"object":b.object.as_ref().map(|o|json!({"part":o.part,"datatype":o.datatype,"storage":o.storage,"type_kind":o.type_kind,"type_size":o.type_size,"name":o.name_bytes,"complete":o.name_complete,"known":o.name_known})),
                "materialization":b.materialization.as_ref().map(|m|json!({"part":m.part,"kind":m.kind,"displacement":m.displacement,"datatype":m.datatype,"storage":m.storage,"symbol_known":m.symbol_known,"symbol_complete":m.symbol_complete,"symbol":m.symbol_bytes})),
                "array_promotion":b.array_promotion.as_ref().map(|a|json!({"index":a.element_index,"element_size":a.element_size,"array_size":a.array_size,"element_count":a.element_count,"flags":a.flags&7,"datatype":a.datatype,"storage":a.storage,"type_kind":a.type_kind,"type_size":a.type_size,"name_known":a.name_known,"name_complete":a.name_complete,"name":a.name_bytes})),
                "call_result":b.call_result.as_ref().map(|c|json!({"part":c.part,"kind":c.kind,"type_kind":c.type_kind,"type_size":c.type_size,"tree_known":c.tree_known,"tree_complete":c.tree_complete,"tree":c.tree_bytes})),
                "scalarization":scalar_origins.get(&b.id),
                "expression":b.expression.and_then(|id|f.expressions.iter().find(|e|e.id==id)).map(|e|json!({"scope":e.scope,"kind":e.kind,"dispatch_target":e.dispatch.as_ref().map(|d|d.retail_target),"complete":e.complete,"tree":e.tree_bytes}))})
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
    let value = json!({"format_version":FINGERPRINT_FORMAT_VERSION,"version":version,"class":g.class,"boundary":g.boundary,"phase":g.phase,
        "physical":g.physical_limit,"start":g.start,"count":g.count,"birth_cutoff":g.birth_cutoff,"observations":g.observations,"instructions":instruction_state,"nodes":nodes,
        "bounds":g.range_bounds.as_ref().map(|b|json!({"physical":b.physical_limit,"initial_last":b.initial_last,"first":b.first,"last":b.last,"evidence":b.evidence})),
        "object":object_instructions(object,&g.link)?,"allocated_data":data,"scalarization":scalarization,"definitions":definitions,"call_arguments":arguments});
    Ok(digest(&serde_json::to_vec(&value)?))
}

#[derive(Clone, Debug, Serialize, Deserialize)]
pub struct Analysis {
    pub format_version: u32,
    pub link: String,
    pub class: u8,
    pub round: usize,
    pub source_sha256: String,
    pub object_sha256: String,
    pub fingerprint: Option<String>,
    pub fingerprint_format_version: u32,
    pub normalization: Evaluation,
}
/// Inspect a retained capture/object pair without compiling it again. The
/// object hash and complete selected-function graph capture are mandatory.
/// Unsupported operand representations are explicit normalization gaps.
pub fn inspect(compilation: &Compilation, link: &str, class: u8, round: usize) -> Result<Analysis> {
    ensure!(class < 5, "unknown register class");
    ensure!(
        compilation.version.has_oracle(),
        "unsupported state-inspection compiler version"
    );
    ensure!(
        compilation.check_status == CheckStatus::Verified && compilation.identical == Some(true),
        "state inspection requires a stock-verified compilation"
    );
    ensure!(
        digest(&compilation.object) == compilation.object_sha256,
        "object bytes do not match the retained compilation"
    );
    // Invalid object structure is an input error, separate from an observed
    // compiler representation for which normalization is not yet supported.
    let object = elf::Elf::parse(&compilation.object)?;
    let functions: Vec<_> = object
        .symbols()?
        .into_iter()
        .filter(|function| function.kind == 2 && function.name == link)
        .collect();
    ensure!(functions.len() == 1, "ELF link must identify one function");
    ensure!(
        object
            .function_bytes(&functions[0])?
            .len()
            .is_multiple_of(4),
        "unaligned PowerPC function"
    );
    object.relocations()?;
    let report = crate::provenance::parse(&compilation.trace)?;
    let selected = graph(&report, link, class, round)?;
    ensure!(
        selected.observations.state == EvaluationState::Satisfied
            && report
                .graphs
                .functions
                .iter()
                .any(|f| f.function == selected.function
                    && f.observations.state == EvaluationState::Satisfied),
        "state inspection requires complete selected-function graph capture"
    );
    let (fingerprint, normalization) = match fingerprint(selected, &report, compilation) {
        Ok(fingerprint) => (
            Some(fingerprint),
            Evaluation {
                state: EvaluationState::Satisfied,
                reasons: vec![],
            },
        ),
        Err(error) => (
            None,
            Evaluation::indeterminate(EvidenceGap::CompilerStateCoverage, format!("{error:#}")),
        ),
    };
    Ok(Analysis {
        format_version: 1,
        link: link.into(),
        class,
        round,
        source_sha256: compilation.source_sha256.clone(),
        object_sha256: compilation.object_sha256.clone(),
        fingerprint,
        fingerprint_format_version: FINGERPRINT_FORMAT_VERSION,
        normalization,
    })
}
