//! The oracle's host-side data model and analysis. Compiler pointers are opaque
//! trace identities; this library never dereferences them.
pub mod allocation_category;
pub mod backend_audit;
pub mod backend_probe;
pub mod birth_origin;
pub mod blocks;
pub mod build_config;
pub mod build_verify;
mod bytes;
pub mod call_argument_probe;
pub mod call_arguments;
pub mod coff;
pub mod compare;
pub mod corpus;
pub mod counter_probe;
pub mod counter_sources;
pub mod cse;
pub mod data_diff;
pub mod derive_splits;
pub mod disassembly;
pub mod dispatch_audit;
pub mod dispatch_probe;
pub mod document;
pub mod download;
pub mod elf;
pub mod executable;
mod fsutil;
pub mod graph_history;
mod hash;
pub mod header_impact;
#[cfg(feature = "wasm")]
pub mod in_process;
pub mod inline_calibrate;
pub mod inline_diff;
pub mod inline_window;
pub mod investigate;
pub mod link;
pub mod operand_observation;
pub mod pe;
pub mod ppc;
pub use mwcc_project::process_tree;
pub mod producer;
pub mod provenance;
pub mod regdiff;
pub mod release;
pub mod relink_check;
pub mod remap_binary;
pub mod report;
pub mod runner;
pub mod scalarization;
pub mod source_arena_audit;
pub mod source_arena_probe;
pub mod source_arenas;
pub mod source_guided;
pub mod source_lifetimes;
pub mod source_node_probe;
pub mod source_nodes;
pub mod source_only;
pub mod source_oracle;
pub mod source_tree;
pub mod split;
mod temporary;
pub mod value_history;
pub mod views;
pub mod wasm_build;
pub mod wasm_oracle;
pub mod x86;

// (a project's units and build: mwcc-project)
pub use mwcc_project::{ninja, paths as project};
// (the traces and their models: mwcc-trace)
pub use mwcc_trace::{
    allocation, colouring, evidence, inlining, locals, python_random, schedule, sequence, trace,
    version,
};

pub const SCHEMA_VERSION: u32 = 6;
