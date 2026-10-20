//! What a traced compile records: the oracle's streams, each switched on by a variable of the compiler's environment
//! (`MWCC_ORACLE_*`), and the functions whose records the trace keeps.
use serde::{Deserialize, Serialize};
use std::collections::BTreeMap;

/// The variables [`TraceOptions::environment`] sets: a traced compile's environment has none of them but its own.
pub const TRACE_ENV: &[&str] = &[
    "MWCC_ORACLE_BACKEND",
    "MWCC_ORACLE_SOURCE_ARENAS",
    "MWCC_ORACLE_SOURCE_NODES",
    "MWCC_ORACLE_SCHEDULE",
    "MWCC_ORACLE_INLINE_COST",
    "MWCC_ORACLE_FULL_COST",
    "MWCC_ORACLE_INSTANTIATION",
    "MWCC_ORACLE_LOCALS",
    "MWCC_ORACLE_BLOCKS",
    "MWCC_ORACLE_CSE",
    "MWCC_ORACLE_STAGES",
    "MWCC_ORACLE_NAMES",
    "MWCC_ORACLE_COPYPROP",
    "MWCC_ORACLE_PEEPHOLE",
    "MWCC_ORACLE_ALIAS",
    "MWCC_ORACLE_VN",
    "MWCC_ORACLE_LISTING",
    "MWCC_ORACLE_IRO_DUMP",
    "MWCC_ORACLE_IRO_PHASES",
    "MWCC_ORACLE_FUNCTIONS",
];

fn is_false(value: &bool) -> bool {
    !value
}

/// The streams a compile traces beyond the default ones (allocation, inlining, frames), and the functions it keeps.
#[derive(Clone, Debug, Default, Serialize, Deserialize, PartialEq, Eq)]
#[serde(default, deny_unknown_fields)]
pub struct TraceOptions {
    pub backend: bool,
    pub source_arenas: bool,
    pub source_nodes: bool,
    pub schedule: bool,
    pub inline_cost: bool,
    pub full_cost: bool,
    pub instantiation: bool,
    pub locals: bool,
    pub blocks: bool,
    pub cse: bool,
    pub stages: bool,
    /// The names of opcodes and of the objects operands refer to (`pc.opcode`, `pc.object`).
    #[serde(skip_serializing_if = "is_false")]
    pub names: bool,
    /// Copy propagation's decisions (`cp.*`).
    #[serde(skip_serializing_if = "is_false")]
    pub copy_propagation: bool,
    /// The peephole's rules (`ph.*`).
    #[serde(skip_serializing_if = "is_false")]
    pub peephole: bool,
    /// The alias analysis's table and each access's alias (`al.*`).
    #[serde(skip_serializing_if = "is_false")]
    pub alias: bool,
    /// With it, every query of the alias analysis (`al.query`; many).
    #[serde(skip_serializing_if = "is_false")]
    pub alias_queries: bool,
    /// The value numbering's decisions on each load and operation, and what killed the memory's values (`vn.*`).
    #[serde(skip_serializing_if = "is_false")]
    pub value_numbering: bool,
    /// The PCode at each stage the compiler's debug listing names (`st.stage`, `st.listing`).
    #[serde(skip_serializing_if = "is_false")]
    pub listing: bool,
    /// The file the optimizer's own dump goes to, and the phases it dumps after (`all`, or a phase's name).
    #[serde(skip_serializing_if = "Option::is_none")]
    pub iro_dump: Option<String>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub iro_phases: Option<String>,
    /// The functions whose records the trace keeps (empty: every function's): those whose link name or name has one
    /// of these in it. The records of another function's inline expansion and code generation are dropped (a run in
    /// process only; mwcc-wasm's `TRACE_FUNCTIONS`).
    #[serde(skip_serializing_if = "Vec::is_empty")]
    pub functions: Vec<String>,
}

impl TraceOptions {
    /// The variables of the compiler's environment these options set (the others of [`TRACE_ENV`] are not set).
    pub fn environment(&self) -> BTreeMap<String, String> {
        let mut env = BTreeMap::new();
        for (key, enabled, value) in [
            ("MWCC_ORACLE_BACKEND", self.backend, "1"),
            ("MWCC_ORACLE_SOURCE_ARENAS", self.source_arenas, "1"),
            ("MWCC_ORACLE_SOURCE_NODES", self.source_nodes, "1"),
            ("MWCC_ORACLE_SCHEDULE", self.schedule, "1"),
            ("MWCC_ORACLE_INLINE_COST", self.inline_cost, "1"),
            ("MWCC_ORACLE_FULL_COST", self.full_cost, "1"),
            ("MWCC_ORACLE_INSTANTIATION", self.instantiation, "1"),
            ("MWCC_ORACLE_LOCALS", self.locals, "1"),
            ("MWCC_ORACLE_BLOCKS", self.blocks, "2"),
            ("MWCC_ORACLE_CSE", self.cse, "2"),
            ("MWCC_ORACLE_STAGES", self.stages, "1"),
            ("MWCC_ORACLE_NAMES", self.names, "1"),
            ("MWCC_ORACLE_COPYPROP", self.copy_propagation, "1"),
            ("MWCC_ORACLE_PEEPHOLE", self.peephole, "1"),
            ("MWCC_ORACLE_VN", self.value_numbering, "1"),
            ("MWCC_ORACLE_LISTING", self.listing, "1"),
        ] {
            if enabled {
                env.insert(key.into(), value.into());
            }
        }
        if self.alias || self.alias_queries {
            env.insert(
                "MWCC_ORACLE_ALIAS".into(),
                if self.alias_queries { "2" } else { "1" }.into(),
            );
        }
        for (key, value) in [
            ("MWCC_ORACLE_IRO_DUMP", &self.iro_dump),
            ("MWCC_ORACLE_IRO_PHASES", &self.iro_phases),
        ] {
            if let Some(value) = value {
                env.insert(key.into(), value.clone());
            }
        }
        if !self.functions.is_empty() {
            env.insert("MWCC_ORACLE_FUNCTIONS".into(), self.functions.join("\n"));
        }
        env
    }
}
